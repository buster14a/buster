#pragma once

#include <buster/lib/compiler/codegen/machine.h>

typedef enum MachineScheduleMemoryKind
{
    MACHINE_SCHEDULE_MEMORY_UNKNOWN,
    MACHINE_SCHEDULE_MEMORY_STACK_RANGE,
} MachineScheduleMemoryKind;

typedef struct MachineScheduleMemoryAccess MachineScheduleMemoryAccess;
struct MachineScheduleMemoryAccess
{
    MachineScheduleMemoryKind kind;
    u32 stack_slot;
    u32 offset;
    u32 size;
};

// A proof for one bounded, nonvolatile access. Unknown rows, missing producer
// certification, malformed slot references and invalid ranges grant no freedom.
BUSTER_F_DECL MachineScheduleMemoryAccess machine_schedule_memory_access(MachineFunction const* function,
                                                                          MachineInstruction const* instruction);
// Bounds/address proof only, before volatile certification. The selector uses
// this to taint frame objects accessed by volatile pointer rows; it does not
// grant permission to reorder an operation.
BUSTER_F_DECL MachineScheduleMemoryAccess machine_schedule_frame_access(MachineFunction const* function,
                                                                         MachineInstruction const* instruction);

// Private scheduler publication seam, shared with source-location regressions.
// Copy marks into arena, remap rows below row_count, and stably order the copy.
// Rows at or beyond row_count (including terminal marks) retain their keys.
// Input arrays are borrowed and never modified. scratch_arena may equal arena;
// temporary merge storage is released before returning the retained copy.
BUSTER_F_DECL MachineLineMark* machine_schedule_remap_line_marks(Arena* arena, Arena* scratch_arena,
                                                                 MachineLineMark const* marks, u32 mark_count,
                                                                 u32 const* new_rows, u32 row_count);

typedef struct MachineScheduleTrace MachineScheduleTrace;
#if BUSTER_INCLUDE_TESTS
typedef enum MachineScheduleTraceKind
{
    // subject = block index, value = unit count.
    MACHINE_SCHEDULE_TRACE_BLOCK,
    // subject = unit, value = growth, extra = new unit sequence.
    MACHINE_SCHEDULE_TRACE_PUSH,
    // subject = unit that found the entry arrays full.
    MACHINE_SCHEDULE_TRACE_OVERFLOW,
    // subject = unit, value = entry sequence, extra = unit state << 24 | unit sequence.
    MACHINE_SCHEDULE_TRACE_STALE,
    // subject = unit placed next (bottom-up).
    MACHINE_SCHEDULE_TRACE_SELECT,
    // subject = virtual register, value = direction (0 satisfied, 1 demanded), extra = toucher count.
    MACHINE_SCHEDULE_TRACE_TRANSITION,
    // subject = unit whose last successor was just placed.
    MACHINE_SCHEDULE_TRACE_READY,
    // subject = block index restored to source order.
    MACHINE_SCHEDULE_TRACE_FALLBACK,
    // value = scheduled excess, extra = compared source-order excess.
    MACHINE_SCHEDULE_TRACE_GATE,
} MachineScheduleTraceKind;

typedef struct MachineScheduleTraceEvent MachineScheduleTraceEvent;
struct MachineScheduleTraceEvent
{
    u32 kind;
    u32 subject;
    s32 value;
    u32 extra;
};

// Test-only observation of the ready-queue decisions. `reference` enumerates
// operand occurrences from the opcode descriptors at every use, as the
// scheduler did before the per-block occurrence stream. A nonzero
// `entry_capacity_limit` lowers the queue capacity to exercise overflow.
// Events past `event_capacity` are counted in `dropped` only.
struct MachineScheduleTrace
{
    MachineScheduleTraceEvent* events;
    u32 event_capacity;
    u32 event_count;
    u32 dropped;
    u32 entry_capacity_limit;
    bool reference;
};

BUSTER_F_DECL MachineScheduleResult machine_schedule_function_traced(Arena* arena, MachineFunction* function, MachineScheduleTrace* trace);
#endif
