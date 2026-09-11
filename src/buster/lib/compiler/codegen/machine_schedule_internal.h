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
