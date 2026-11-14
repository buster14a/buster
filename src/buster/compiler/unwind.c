// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Native fixed-frame unwind descriptions (#88). The producer proves an ordered subset of six pushes, an optional
// fixed allocation, its matching release and reverse pops (and the same release and pops before each tail call's jump,
// #147), and supplies their instruction-end PCs after branch sizing.
// A function with a frame pointer (UnwindFunction.pointer, #395) pushes rbp and points it at the push first, then an
// ordered subset of the other five, and its frame and anything its body allocates are below them, so its CFA is rbp's
// until its last pop, rbp's, whatever rsp does. unwind_write consumes that trusted description; unwind_check is the
// separate diagnostic boundary. No instruction bytes, typed nodes or source rows are inspected here. Initialize once
// before concurrent readers use the tables.
//
// Each function gets its own eight-byte-aligned CIE/FDE pair: version 1, zR, code alignment 1, data alignment -8 and
// pcrel sdata4 addresses. locations[i] names the zero initial-location field for a Link PC32 relocation with addend 0.
// A pair begins BUSTER_UNWIND_LOCATION bytes before that field and ends at the next pair or output.size. Keep pairs
// separate as EH_FRAME atoms for function ownership and the native indexer's bounded CIE walk. With UnwindOutput.shared
// (#395), the batch is one CIE and the FDEs that point back to it, as a compiler's .eh_frame is and its linkers' fast
// paths take it: the CIE first, each FDE BUSTER_UNWIND_FDE_LOCATION bytes before its field, to the next FDE or the end;
// keep the whole batch as one EH_FRAME atom (an object's .eh_frame holds its atoms as one section, whose CIEs a linker's
// walk would otherwise count by the function). No terminator is added.
//
// The exact writer independently appends the CIE and CFI. The vector path gathers eight descriptors, forms their CFA
// ULEBs and shape indices in lanes, then expands original templates of FDEs with byte permutations. Fourteen PC
// deltas and their smallest-form opcodes, the FDE length, function range, CIE pointer and five CFA bytes are the holes.
// Byte compaction drops each advance's unused operand bytes; the CIE is the same bytes for every function (unwind_cie).
// A function with a frame pointer, the rare shape (a C function that allocates on the
// stack as it runs), and one with tail calls (#147's musttail), rarer, the vector path leaves to the exact writer, lane by
// lane. Neither inputs nor outputs need
// padding; masked tails touch only their slices. Input, byte output and location output must be disjoint. Output
// sizes and every representability limit are checked before stores. CAPACITY publishes the required size but
// preserves bytes/locations; other errors preserve the output descriptor too. Empty batches need no byte or location
// storage.
//
// The encodings are format facts from DWARF call-frame instructions and the AMD64 .eh_frame convention. These
// templates and both writers are original; no external implementation is included.

#include "baked.h"

#define BUSTER_UNWIND_EVENTS 14
#define BUSTER_UNWIND_SAVED 6
#define BUSTER_UNWIND_CIE_BYTES 40
// An FDE's initial location field, from its start (past its length and its CIE pointer), and a pair's, from its CIE's.
#define BUSTER_UNWIND_FDE_LOCATION 8
#define BUSTER_UNWIND_LOCATION (BUSTER_UNWIND_CIE_BYTES + BUSTER_UNWIND_FDE_LOCATION)
#define BUSTER_UNWIND_PAIR_MIN 64
#define BUSTER_UNWIND_TEMPLATE_BYTES 192
#define BUSTER_UNWIND_SHAPE_WIDTHS 6
#define BUSTER_UNWIND_SHAPES (64 * BUSTER_UNWIND_SHAPE_WIDTHS)
#define BUSTER_UNWIND_BATCH 8
// A batch lane's shape when its function has a frame pointer, which the exact writer writes.
#define BUSTER_UNWIND_SHAPE_POINTER BUSTER_UNWIND_SHAPES

// A function's frame: its size, its allocation and the instruction-end PCs of its transitions, in order. Without a
// frame pointer: each push of `saved`'s registers, the allocation, its release and each pop, the allocation's two
// events only when frame_size isn't 0 (2n + 2 or 2n events). With one (`pointer`, saved without RBP's bit and frame_size
// 0, which no transition moves): rbp's push, its mov from rsp, each other push, each pop and rbp's pop (2n + 3).
//
// Its tail calls (#147's musttail), `tail_count` of them, 0 for none: each one's teardown, the epilogue's release and
// pops (with a frame pointer, rbp's last) before a jump to its callee rather than the ret, somewhere in its body, after
// which the body's frame holds again. `tails` has, for each in order, the instruction-end PCs of its teardown's
// transitions as the epilogue's are (without a frame pointer the release, when frame_size isn't 0, and each pop; with
// one each pop and rbp's) and of its jump: unwind_tail_events of them each, all after the prologue's last transition
// and before the epilogue's first (or the size). Their CFI remembers the body's rules before the teardown and restores
// them after the jump (DW_CFA_remember_state and DW_CFA_restore_state); the exact writer writes such a function's FDE.
// A tail's jump may be a ret too: an early return's epilogue, Tier 1's (#177), is the same teardown and leaves the same
// rules behind it.
//
// An `open` function (1; 0 otherwise) has no epilogue at its end: its code ends elsewhere than its epilogue's ret (a
// loop's latch, a call that doesn't return, a trap), so its events are the prologue's alone (n + 1 or n, and with a
// frame pointer n + 2), the body's rules hold to its end, and each of its returns is a tail. The exact writer writes
// such a function's FDE too.
//
// A row program (#1056: `open` BUSTER_UNWIND_ROWS) is the general form: `row_count` resolved frame rows
// (emission_metadata.c's EmissionFrameRow, the producers' frame events at their final offsets), whose offsets don't
// decrease and are less than the size, written by the exact writer as call-frame instructions in their order
// (unwind_rows_scalar). Its saved, frame_size, offsets, event_count, pointer and tail_count are 0. For a fixed shape's
// rows (unwind_function_rows) its bytes are the fixed descriptor's exactly, which test-unwind checks for every shape it
// writes; unwind_rows_function lowers rows to the fixed descriptor where they are its simple shape, which the vector
// writer takes, and otherwise to a row program. A shape the fixed descriptor can't describe (a probe loop's CFA in
// another register, #1056's) is a row program only.
#define BUSTER_UNWIND_ROWS 2
typedef struct UnwindFunction UnwindFunction;
struct UnwindFunction
{
    uint32_t size;
    uint32_t frame_size;
    uint32_t offsets[BUSTER_UNWIND_EVENTS];
    uint8_t saved;
    uint8_t event_count;
    uint8_t pointer;
    uint8_t open;
    uint32_t tail_count;
    const uint32_t* tails;
    const EmissionFrameRow* rows;
    uint32_t row_count;
    uint32_t row_reserved;
};

typedef enum UnwindError
{
    UNWIND_OK,
    UNWIND_ERROR_INVALID,
    UNWIND_ERROR_UNSUPPORTED,
    UNWIND_ERROR_CAPACITY,
    UNWIND_ERROR_LIMIT,
} UnwindError;

typedef struct UnwindOutput UnwindOutput;
struct UnwindOutput
{
    uint8_t* bytes;
    uint64_t capacity;
    uint64_t size;
    uint32_t* locations;
    uint32_t location_capacity;
    // One CIE for the batch rather than one per function (see the file's comment).
    bool shared;
};

// Mask order is the producer's RBX, RBP, R12, R13, R14, R15. These are DWARF numbers, not encoder register numbers.
static const uint8_t unwind_registers[BUSTER_UNWIND_SAVED] = { 3, 6, 12, 13, 14, 15 };

static __attribute__((unused)) bool unwind_initialize(void);
static __attribute__((unused)) UnwindError unwind_check(const UnwindFunction* functions, uint32_t count);
static __attribute__((unused)) UnwindError unwind_write(const UnwindFunction* functions, uint32_t count, UnwindOutput* output, bool exact);
static __attribute__((unused)) uint32_t unwind_function_rows(const UnwindFunction* function, EmissionFrameRow* rows, uint32_t capacity);
static __attribute__((unused)) uint32_t unwind_rows_canonical(EmissionFrameRow* rows, uint32_t count, uint32_t size);
static __attribute__((unused)) UnwindError unwind_rows_function(const EmissionFrameRow* rows, uint32_t count, uint32_t size, UnwindFunction* output);
static UnwindError unwind_rows_check(const UnwindFunction* function);

// The most states a row program's CFI remembers at once (DW_CFA_remember_state): the writer keeps each one's CFA.
#define BUSTER_UNWIND_ROW_STATES 16

// How many of a function's transitions its prologue makes (its pushes, and the allocation or rbp's push and mov): its
// offsets' first, before its tail calls' and its epilogue's.
static uint32_t unwind_prologue_events(const UnwindFunction* function)
{
    const uint32_t pushes = (uint32_t)__builtin_popcount(function->saved);
    return function->pointer != 0 ? 2 + pushes : pushes + (function->frame_size != 0 ? 1 : 0);
}

// A function's transitions: its prologue's, and unless it is open its epilogue's as many again (and rbp's pop).
static uint32_t unwind_events(const UnwindFunction* function)
{
    const uint32_t pushes = (uint32_t)__builtin_popcount(function->saved);
    const uint32_t prologue = unwind_prologue_events(function);
    return function->open != 0 ? prologue : function->pointer != 0 ? 2 * pushes + 3 : 2 * pushes + 2 * (function->frame_size != 0);
}

// The events of each of a function's tail calls (UnwindFunction.tails): its epilogue's transitions, and its jump's end.
static uint32_t unwind_tail_events(const UnwindFunction* function)
{
    const uint32_t pushes = (uint32_t)__builtin_popcount(function->saved);
    return (function->pointer != 0 ? pushes + 1 : pushes + (function->frame_size != 0 ? 1 : 0)) + 1;
}

// The checker is intentionally outside the trusted writer's hot path. Function ranges use signed four-byte FDE
// fields, and this producer's positive stack allocation is a signed immediate. Each transition precedes the final RET.
// A tail call's events are distinct PCs within the function, so that there are fewer tail calls than bytes.
static UnwindError unwind_check(const UnwindFunction* functions, uint32_t count)
{
    UnwindError result = count != 0 && functions == 0 ? UNWIND_ERROR_INVALID : UNWIND_OK;
    for (uint32_t i = 0; result == UNWIND_OK && i < count; i += 1)
    {
        const UnwindFunction* function = &functions[i];
        if (function->open == BUSTER_UNWIND_ROWS)
        {
            result = unwind_rows_check(function);
            continue;
        }
        const uint32_t events = unwind_events(function);
        // With a frame pointer, rbp is its push and the allocation moves no CFA: RBP's bit (1) and frame_size are 0.
        const bool pointer_valid = function->pointer == 0 || (function->pointer == 1 && (function->saved & 2) == 0 && function->frame_size == 0);
        const bool tails_valid = function->tail_count == 0 ? function->tails == 0 : function->tails != 0 && function->tail_count < function->size;
        if (function->size == 0 || function->saved >= 64 || function->open > 1 || function->event_count != events || !pointer_valid || !tails_valid)
        {
            result = UNWIND_ERROR_INVALID;
        }
        else if (function->size > INT32_MAX || function->frame_size > INT32_MAX)
        {
            result = UNWIND_ERROR_LIMIT;
        }
        uint32_t previous = 0;
        for (uint32_t event = 0; result == UNWIND_OK && event < BUSTER_UNWIND_EVENTS; event += 1)
        {
            const uint32_t offset = function->offsets[event];
            if (event < events)
            {
                result = offset > previous && offset < function->size ? UNWIND_OK : UNWIND_ERROR_INVALID;
                previous = offset;
            }
            else if (offset != 0)
            {
                result = UNWIND_ERROR_INVALID;
            }
        }
        // The tail calls' events, in order, between the prologue's last and the epilogue's first.
        const uint32_t prologue = unwind_prologue_events(function);
        const uint64_t tail_events = result == UNWIND_OK ? (uint64_t)function->tail_count * unwind_tail_events(function) : 0;
        const uint32_t bound = events > prologue ? function->offsets[prologue] : function->size;
        previous = prologue != 0 ? function->offsets[prologue - 1] : 0;
        for (uint64_t event = 0; result == UNWIND_OK && event < tail_events; event += 1)
        {
            const uint32_t offset = function->tails[event];
            result = offset > previous && offset < bound ? UNWIND_OK : UNWIND_ERROR_INVALID;
            previous = offset;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Independent scalar emission

typedef struct UnwindWriter UnwindWriter;
struct UnwindWriter
{
    uint8_t* bytes;
    uint64_t size;
};

// A writer without bytes measures: the row programs' sizes are their writer's (unwind_rows_bytes).
static void unwind_integer(UnwindWriter* writer, uint32_t value, uint32_t bytes)
{
    if (writer->bytes != 0)
    {
        memcpy(writer->bytes + writer->size, &value, bytes);
    }
    writer->size += bytes;
}

static void unwind_sleb(UnwindWriter* writer, int64_t value)
{
    bool more = true;
    while (more)
    {
        const uint32_t byte = (uint32_t)(value & 0x7f);
        value >>= 7;
        more = !((value == 0 && (byte & 0x40) == 0) || (value == -1 && (byte & 0x40) != 0));
        unwind_integer(writer, byte | (more ? 0x80 : 0), 1);
    }
}

static void unwind_uleb(UnwindWriter* writer, uint64_t value)
{
    do
    {
        const uint32_t byte = (uint32_t)(value & 0x7f) | (value > 0x7f ? 0x80 : 0);
        unwind_integer(writer, byte, 1);
        value >>= 7;
    } while (value != 0);
}

static uint32_t unwind_uleb_size(uint32_t value)
{
    uint32_t size = 1;
    while (value > 0x7f)
    {
        value >>= 7;
        size += 1;
    }
    return size;
}

// Code alignment is 1: a primary advance carries six bits, and the extended forms carry 1, 2 or 4 operand bytes.
static uint32_t unwind_advance_size(uint32_t delta)
{
    return 1 + (delta >= 64) + (delta >= 256) + 2 * (delta >= 65536);
}

// A function's FDE's bytes as the exact writer writes them: its header and rule changes, plus the smallest advances,
// rounded to eight bytes. Tail calls interrupt the offsets column between prologue and epilogue: the epilogue's first
// delta is from the last tail's jump, not the prologue. Count them in the same PC order as unwind_fixed_scalar.
static uint64_t unwind_fixed_bytes(const UnwindFunction* function)
{
    const uint32_t saved = (uint32_t)__builtin_popcount(function->saved);
    const uint32_t cfa = 8 + 8 * saved + function->frame_size;
    const uint64_t rules = function->pointer != 0 ? 27 + 3 * saved : 17 + 7 * saved + (function->frame_size != 0 ? 3 + unwind_uleb_size(cfa) : 0);
    const uint64_t epilogue = function->open == 0 ? 0 : function->pointer != 0 ? 4 + saved : 3 * saved + (function->frame_size != 0 ? 2 : 0);
    const uint64_t tail = function->pointer != 0 ? 6 + saved : 2 + 3 * saved + (function->frame_size != 0 ? 2 : 0);
    const uint32_t prologue = unwind_prologue_events(function);
    const uint32_t event_count = function->event_count;
    const uint64_t tail_events = (uint64_t)function->tail_count * unwind_tail_events(function);
    const uint32_t* const tails = function->tails;
    uint64_t bytes = rules - epilogue + tail * function->tail_count;
    uint32_t previous = 0;
    for (uint32_t event = 0; event < prologue; event += 1)
    {
        const uint32_t offset = function->offsets[event];
        bytes += unwind_advance_size(offset - previous);
        previous = offset;
    }
    for (uint64_t event = 0; event < tail_events; event += 1)
    {
        const uint32_t offset = tails[event];
        bytes += unwind_advance_size(offset - previous);
        previous = offset;
    }
    for (uint32_t event = prologue; event < event_count; event += 1)
    {
        const uint32_t offset = function->offsets[event];
        bytes += unwind_advance_size(offset - previous);
        previous = offset;
    }
    return (bytes + 7) & ~UINT64_C(7);
}

// A row program's bytes (unwind_rows_scalar's), or a fixed descriptor's.
static uint64_t unwind_rows_bytes(const UnwindFunction* function);
static uint64_t unwind_fde_bytes(const UnwindFunction* function)
{
    return function->open == BUSTER_UNWIND_ROWS ? unwind_rows_bytes(function) : unwind_fixed_bytes(function);
}

static void unwind_advance(UnwindWriter* writer, uint32_t offset, uint32_t* previous)
{
    const uint32_t delta = offset - *previous;
    if (delta < 64)
    {
        unwind_integer(writer, 0x40 | delta, 1); // DW_CFA_advance_loc
    }
    else
    {
        const uint32_t width = unwind_advance_size(delta) - 1;
        const uint32_t opcode = delta < 256 ? 2 : delta < 65536 ? 3 : 4;
        unwind_integer(writer, opcode, 1); // DW_CFA_advance_loc1, DW_CFA_advance_loc2 or DW_CFA_advance_loc4
        unwind_integer(writer, delta, width);
    }
    *previous = offset;
}

// The CIE every FDE points back to: its pair's, or the batch's shared one.
static void unwind_cie_scalar(UnwindWriter* writer)
{
    const uint64_t begin = writer->size;
    unwind_integer(writer, BUSTER_UNWIND_CIE_BYTES - 4, 4);
    unwind_integer(writer, 0, 4); // CIE id
    unwind_integer(writer, 1, 1); // version
    unwind_integer(writer, 'z', 1);
    unwind_integer(writer, 'R', 1);
    unwind_integer(writer, 0, 1);
    unwind_uleb(writer, 1); // code alignment
    unwind_integer(writer, 0x78, 1); // SLEB128(-8)
    unwind_integer(writer, 16, 1); // return-address register, RIP
    unwind_uleb(writer, 1); // augmentation data length
    unwind_integer(writer, 0x1b, 1); // DW_EH_PE_pcrel | DW_EH_PE_sdata4
    unwind_integer(writer, 0x0c, 1); // DW_CFA_def_cfa
    unwind_uleb(writer, 7); // RSP
    unwind_uleb(writer, 8);
    unwind_integer(writer, 0x90, 1); // DW_CFA_offset RIP
    unwind_uleb(writer, 1);
    for (uint32_t reg = 0; reg < BUSTER_UNWIND_SAVED; reg += 1)
    {
        unwind_integer(writer, 0x08, 1); // DW_CFA_same_value
        unwind_uleb(writer, unwind_registers[reg]);
    }
    while (writer->size < begin + BUSTER_UNWIND_CIE_BYTES)
    {
        unwind_integer(writer, 0, 1); // DW_CFA_nop
    }
}

// A function's tail calls' CFI (UnwindFunction.tails), between its prologue's and its epilogue's: for each, the body's
// rules remembered, its teardown's transitions as the epilogue's (the CFA back to rsp + 8, each register restored by
// its pop), and after its jump the body's rules again, which hold from there on.
static void unwind_tails_scalar(UnwindWriter* writer, const UnwindFunction* function, uint32_t* previous)
{
    const uint32_t per = unwind_tail_events(function);
    const uint32_t pushes = (uint32_t)__builtin_popcount(function->saved);
    for (uint32_t tail = 0; tail < function->tail_count; tail += 1)
    {
        const uint32_t* events = function->tails + (uint64_t)tail * per;
        uint32_t event = 0;
        unwind_integer(writer, 0x0a, 1); // DW_CFA_remember_state
        uint32_t cfa = 8 + 8 * pushes;
        if (function->pointer == 0 && function->frame_size != 0)
        {
            unwind_advance(writer, events[event], previous);
            event += 1;
            unwind_integer(writer, 0x0e, 1); // DW_CFA_def_cfa_offset
            unwind_uleb(writer, cfa);
        }
        for (uint32_t reg = BUSTER_UNWIND_SAVED; reg > 0; reg -= 1)
        {
            if ((function->saved & (1u << (reg - 1))) != 0)
            {
                unwind_advance(writer, events[event], previous);
                event += 1;
                if (function->pointer == 0)
                {
                    cfa -= 8;
                    unwind_integer(writer, 0x0e, 1);
                    unwind_uleb(writer, cfa);
                }
                unwind_integer(writer, 0xc0 | unwind_registers[reg - 1], 1); // DW_CFA_restore
            }
        }
        if (function->pointer != 0)
        {
            unwind_advance(writer, events[event], previous);
            event += 1;
            unwind_integer(writer, 0x0c, 1); // DW_CFA_def_cfa rsp, 8
            unwind_uleb(writer, 7);
            unwind_uleb(writer, 8);
            unwind_integer(writer, 0xc0 | unwind_registers[1], 1);
        }
        unwind_advance(writer, events[event], previous);
        unwind_integer(writer, 0x0b, 1); // DW_CFA_restore_state
    }
}

// A fixed descriptor's FDE, pointing back to the CIE at `cie`.
static void unwind_fixed_scalar(UnwindWriter* writer, const UnwindFunction* function, uint64_t cie)
{
    const uint64_t fde = writer->size;
    unwind_integer(writer, 0, 4); // patched record length
    unwind_integer(writer, (uint32_t)(fde + 4 - cie), 4); // backward CIE pointer, from this field
    unwind_integer(writer, 0, 4); // relocated initial location
    unwind_integer(writer, function->size, 4);
    unwind_uleb(writer, 0); // FDE augmentation data length
    uint32_t cfa = 8;
    uint32_t previous = 0;
    uint32_t event = 0;
    if (function->pointer != 0)
    {
        // rbp pushed (the CFA 16 bytes up, rbp at its top) and pointed at its push: the CFA rbp's from then on.
        unwind_advance(writer, function->offsets[event], &previous);
        event += 1;
        unwind_integer(writer, 0x0e, 1); // DW_CFA_def_cfa_offset
        unwind_uleb(writer, 16);
        unwind_integer(writer, 0x80 | unwind_registers[1], 1); // DW_CFA_offset rbp
        unwind_uleb(writer, 2);
        unwind_advance(writer, function->offsets[event], &previous);
        event += 1;
        unwind_integer(writer, 0x0d, 1); // DW_CFA_def_cfa_register
        unwind_uleb(writer, unwind_registers[1]);
        // The others below it, which move no CFA; their pops restore them, then rbp's makes the CFA rsp's again.
        uint32_t slot = 2;
        for (uint32_t reg = 0; reg < BUSTER_UNWIND_SAVED; reg += 1)
        {
            if ((function->saved & (1u << reg)) != 0)
            {
                unwind_advance(writer, function->offsets[event], &previous);
                event += 1;
                slot += 1;
                unwind_integer(writer, 0x80 | unwind_registers[reg], 1);
                unwind_uleb(writer, slot);
            }
        }
        unwind_tails_scalar(writer, function, &previous);
        for (uint32_t reg = BUSTER_UNWIND_SAVED; function->open == 0 && reg > 0; reg -= 1)
        {
            if ((function->saved & (1u << (reg - 1))) != 0)
            {
                unwind_advance(writer, function->offsets[event], &previous);
                event += 1;
                unwind_integer(writer, 0xc0 | unwind_registers[reg - 1], 1);
            }
        }
        if (function->open == 0)
        {
            unwind_advance(writer, function->offsets[event], &previous);
            event += 1;
            unwind_integer(writer, 0x0c, 1); // DW_CFA_def_cfa rsp, 8
            unwind_uleb(writer, 7);
            unwind_uleb(writer, 8);
            unwind_integer(writer, 0xc0 | unwind_registers[1], 1);
        }
    }
    for (uint32_t reg = 0; function->pointer == 0 && reg < BUSTER_UNWIND_SAVED; reg += 1)
    {
        if ((function->saved & (1u << reg)) != 0)
        {
            unwind_advance(writer, function->offsets[event], &previous);
            event += 1;
            cfa += 8;
            unwind_integer(writer, 0x0e, 1); // DW_CFA_def_cfa_offset
            unwind_uleb(writer, cfa);
            unwind_integer(writer, 0x80 | unwind_registers[reg], 1); // DW_CFA_offset
            unwind_uleb(writer, cfa / 8);
        }
    }
    if (function->pointer == 0 && function->frame_size != 0)
    {
        unwind_advance(writer, function->offsets[event], &previous);
        event += 1;
        unwind_integer(writer, 0x0e, 1);
        unwind_uleb(writer, cfa + function->frame_size);
    }
    if (function->pointer == 0)
    {
        unwind_tails_scalar(writer, function, &previous);
    }
    if (function->pointer == 0 && function->open == 0 && function->frame_size != 0)
    {
        unwind_advance(writer, function->offsets[event], &previous);
        event += 1;
        unwind_integer(writer, 0x0e, 1);
        unwind_uleb(writer, cfa);
    }
    for (uint32_t reg = BUSTER_UNWIND_SAVED; function->pointer == 0 && function->open == 0 && reg > 0; reg -= 1)
    {
        if ((function->saved & (1u << (reg - 1))) != 0)
        {
            unwind_advance(writer, function->offsets[event], &previous);
            event += 1;
            cfa -= 8;
            unwind_integer(writer, 0x0e, 1);
            unwind_uleb(writer, cfa);
            unwind_integer(writer, 0xc0 | unwind_registers[reg - 1], 1); // DW_CFA_restore, to the CIE's same_value
        }
    }
    while ((writer->size & 7) != 0)
    {
        unwind_integer(writer, 0, 1);
    }
    const uint32_t length = (uint32_t)(writer->size - fde - 4);
    memcpy(writer->bytes + fde, &length, 4);
}

// A function's FDE, pointing back to the CIE at `cie`: a row program's or a fixed descriptor's.
static void unwind_rows_scalar(UnwindWriter* writer, const UnwindFunction* function, uint64_t cie);
static void unwind_fde_scalar(UnwindWriter* writer, const UnwindFunction* function, uint64_t cie)
{
    if (function->open == BUSTER_UNWIND_ROWS)
    {
        unwind_rows_scalar(writer, function, cie);
    }
    else
    {
        unwind_fixed_scalar(writer, function, cie);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Row programs (#1056): resolved frame rows as call-frame instructions

// Whether a row program's rows are ones its writer encodes: known kinds, registers in the x86-64 namespace (but rsp's
// and the return address's rules, which the CFA and the CIE give), saved-register offsets that are multiples of the CIE's
// data alignment (8), a CFA offset that isn't negative, states remembered at most BUSTER_UNWIND_ROW_STATES deep and
// restored no more than remembered, and offsets that don't decrease and are less than the size.
static UnwindError unwind_rows_check(const UnwindFunction* function)
{
    // A program of no rows may cover no bytes: an empty naked function's (#1845), whose FDE has no range, as clang's.
    UnwindError result = (function->size == 0 && function->row_count != 0) || function->saved != 0 || function->frame_size != 0 || function->event_count != 0 || function->pointer != 0 ||
                                 function->tail_count != 0 || function->tails != 0 || (function->row_count != 0 && function->rows == 0) ? UNWIND_ERROR_INVALID : UNWIND_OK;
    result = result == UNWIND_OK && function->size > INT32_MAX ? UNWIND_ERROR_LIMIT : result;
    uint32_t previous = 0;
    uint32_t depth = 0;
    const EmissionFrameRow* rows = function->rows;
    const uint32_t row_count = function->row_count;
    for (uint32_t i = 0; result == UNWIND_OK && i < row_count; i += 1)
    {
        const EmissionFrameRow row = rows[i];
        result = row.offset >= previous && row.offset < function->size && row.reserved == 0 ? UNWIND_OK : UNWIND_ERROR_INVALID;
        const bool rule = row.reg < BUSTER_EMISSION_X86_REGISTERS && row.reg != BUSTER_EMISSION_X86_RSP && row.reg != BUSTER_EMISSION_X86_RETURN;
        // A vector or mask register's slot or caller's value (#147, a preserving frame's); DW_CFA_restore's one byte names
        // registers below 64 alone, and no rule copies one register into another here.
        const bool vector = emission_x86_saved_vector(row.reg);
        switch (row.kind)
        {
            case EMISSION_FRAME_CFA:
            {
                result = row.reg < 16 && row.value >= 0 ? result : UNWIND_ERROR_UNSUPPORTED;
            } break;
            case EMISSION_FRAME_CFA_OFFSET:
            {
                result = row.reg == 0 && row.value >= 0 ? result : UNWIND_ERROR_UNSUPPORTED;
            } break;
            case EMISSION_FRAME_OFFSET:
            {
                result = (rule || vector) && (row.value & 7) == 0 ? result : UNWIND_ERROR_UNSUPPORTED;
            } break;
            case EMISSION_FRAME_REGISTER:
            {
                result = rule && row.value >= 0 && row.value < 16 ? result : UNWIND_ERROR_UNSUPPORTED;
            } break;
            case EMISSION_FRAME_RESTORE:
            {
                result = rule && row.value == 0 ? result : UNWIND_ERROR_UNSUPPORTED;
            } break;
            case EMISSION_FRAME_SAME_VALUE:
            case EMISSION_FRAME_UNDEFINED:
            {
                result = (rule || vector) && row.value == 0 ? result : UNWIND_ERROR_UNSUPPORTED;
            } break;
            case EMISSION_FRAME_REMEMBER_STATE:
            {
                result = row.reg == 0 && row.value == 0 && depth < BUSTER_UNWIND_ROW_STATES ? result : UNWIND_ERROR_UNSUPPORTED;
                depth += 1;
            } break;
            case EMISSION_FRAME_RESTORE_STATE:
            {
                result = row.reg == 0 && row.value == 0 && depth != 0 ? result : UNWIND_ERROR_INVALID;
                depth -= depth != 0 ? 1 : 0;
            } break;
            default:
            {
                result = UNWIND_ERROR_INVALID;
            } break;
        }
        previous = row.offset;
    }
    return result;
}

// A row program's FDE, pointing back to the CIE at `cie`: its rows in order, each change of offset an advance (always
// the smallest advance form, as the fixed writer's), but a state's remembering, which takes no advance: the rules in force
// there are the ones in force since the last row, which are the ones to remember. A CFA's register or offset alone
// changes by DW_CFA_def_cfa_register or DW_CFA_def_cfa_offset, both by DW_CFA_def_cfa, as the fixed writer changes
// them; so the fixed shapes' rows (unwind_function_rows) are its bytes exactly. The rows satisfy unwind_rows_check.
static void unwind_rows_scalar(UnwindWriter* writer, const UnwindFunction* function, uint64_t cie)
{
    const uint64_t fde = writer->size;
    unwind_integer(writer, 0, 4); // patched record length
    unwind_integer(writer, (uint32_t)(fde + 4 - cie), 4);
    unwind_integer(writer, 0, 4); // relocated initial location
    unwind_integer(writer, function->size, 4);
    unwind_uleb(writer, 0);
    uint32_t previous = 0;
    uint32_t cfa_register = BUSTER_EMISSION_X86_RSP;
    int64_t cfa_offset = 8;
    uint32_t saved_registers[BUSTER_UNWIND_ROW_STATES];
    int64_t saved_offsets[BUSTER_UNWIND_ROW_STATES];
    uint32_t depth = 0;
    // The rows are the writer's input, apart from its output (unwind_write's contract): read once, not after each store.
    const EmissionFrameRow* rows = function->rows;
    const uint32_t row_count = function->row_count;
    for (uint32_t i = 0; i < row_count; i += 1)
    {
        const EmissionFrameRow row = rows[i];
        if (row.kind != EMISSION_FRAME_REMEMBER_STATE && row.offset != previous)
        {
            unwind_advance(writer, row.offset, &previous);
        }
        switch (row.kind)
        {
            case EMISSION_FRAME_CFA:
            {
                if (row.reg == cfa_register && row.value != cfa_offset)
                {
                    unwind_integer(writer, 0x0e, 1); // DW_CFA_def_cfa_offset
                    unwind_uleb(writer, (uint64_t)row.value);
                }
                else if (row.reg != cfa_register && row.value == cfa_offset)
                {
                    unwind_integer(writer, 0x0d, 1); // DW_CFA_def_cfa_register
                    unwind_uleb(writer, row.reg);
                }
                else if (row.reg != cfa_register)
                {
                    unwind_integer(writer, 0x0c, 1); // DW_CFA_def_cfa
                    unwind_uleb(writer, row.reg);
                    unwind_uleb(writer, (uint64_t)row.value);
                }
                cfa_register = row.reg;
                cfa_offset = row.value;
            } break;
            case EMISSION_FRAME_CFA_OFFSET:
            {
                unwind_integer(writer, 0x0e, 1);
                unwind_uleb(writer, (uint64_t)row.value);
                cfa_offset = row.value;
            } break;
            case EMISSION_FRAME_OFFSET:
            {
                // The CIE's data alignment is -8: a slot at CFA - 8n is n.
                const int64_t factored = -(row.value / 8);
                if (factored >= 0 && row.reg < 64)
                {
                    unwind_integer(writer, 0x80 | row.reg, 1); // DW_CFA_offset
                    unwind_uleb(writer, (uint64_t)factored);
                }
                else
                {
                    unwind_integer(writer, 0x11, 1); // DW_CFA_offset_extended_sf
                    unwind_uleb(writer, row.reg);
                    unwind_sleb(writer, factored);
                }
            } break;
            case EMISSION_FRAME_REGISTER:
            {
                unwind_integer(writer, 0x09, 1); // DW_CFA_register
                unwind_uleb(writer, row.reg);
                unwind_uleb(writer, (uint64_t)row.value);
            } break;
            case EMISSION_FRAME_RESTORE:
            {
                unwind_integer(writer, 0xc0 | row.reg, 1); // DW_CFA_restore, to the CIE's rule
            } break;
            case EMISSION_FRAME_SAME_VALUE:
            {
                unwind_integer(writer, 0x08, 1);
                unwind_uleb(writer, row.reg);
            } break;
            case EMISSION_FRAME_UNDEFINED:
            {
                unwind_integer(writer, 0x07, 1);
                unwind_uleb(writer, row.reg);
            } break;
            case EMISSION_FRAME_REMEMBER_STATE:
            {
                unwind_integer(writer, 0x0a, 1);
                saved_registers[depth] = cfa_register;
                saved_offsets[depth] = cfa_offset;
                depth += 1;
            } break;
            case EMISSION_FRAME_RESTORE_STATE:
            {
                unwind_integer(writer, 0x0b, 1);
                depth -= 1;
                cfa_register = saved_registers[depth];
                cfa_offset = saved_offsets[depth];
            } break;
        }
    }
    while ((writer->size & 7) != 0)
    {
        unwind_integer(writer, 0, 1);
    }
    if (writer->bytes != 0)
    {
        const uint32_t length = (uint32_t)(writer->size - fde - 4);
        memcpy(writer->bytes + fde, &length, 4);
    }
}

// A row program's FDE's bytes: its writer's, measured.
static uint64_t unwind_rows_bytes(const UnwindFunction* function)
{
    UnwindWriter writer = { 0 };
    unwind_rows_scalar(&writer, function, 0);
    return writer.size;
}

static void unwind_row(EmissionFrameRow* rows, uint32_t* count, uint32_t capacity, uint32_t offset, uint32_t kind, uint32_t reg, int64_t value)
{
    if (*count < capacity)
    {
        rows[*count] = (EmissionFrameRow){ .offset = offset, .value = value, .reg = (uint16_t)reg, .kind = (uint8_t)kind };
    }
    *count += 1;
}

// The rows of a fixed shape's descriptor (UnwindFunction without rows, which unwind_check accepts) in the fixed writer's
// order, which the row writer writes as its bytes: each push's CFA offset and slot, rbp's pointing, the allocation, each
// tail call's remembered state, teardown and restored state, then the epilogue's release, pops and restored registers.
// Writes at most `capacity` of them; returns how many there are.
static uint32_t unwind_function_rows(const UnwindFunction* function, EmissionFrameRow* rows, uint32_t capacity)
{
    uint32_t count = 0;
    uint32_t event = 0;
    const uint32_t pushes = (uint32_t)__builtin_popcount(function->saved);
    const uint32_t per = unwind_tail_events(function);
    int64_t cfa = 8;
    if (function->pointer != 0)
    {
        unwind_row(rows, &count, capacity, function->offsets[event], EMISSION_FRAME_CFA_OFFSET, 0, 16);
        unwind_row(rows, &count, capacity, function->offsets[event], EMISSION_FRAME_OFFSET, unwind_registers[1], -16);
        event += 1;
        unwind_row(rows, &count, capacity, function->offsets[event], EMISSION_FRAME_CFA, unwind_registers[1], 16);
        event += 1;
        int32_t slot = 16;
        for (uint32_t reg = 0; reg < BUSTER_UNWIND_SAVED; reg += 1)
        {
            if ((function->saved & (1u << reg)) != 0)
            {
                slot += 8;
                unwind_row(rows, &count, capacity, function->offsets[event], EMISSION_FRAME_OFFSET, unwind_registers[reg], -slot);
                event += 1;
            }
        }
    }
    for (uint32_t reg = 0; function->pointer == 0 && reg < BUSTER_UNWIND_SAVED; reg += 1)
    {
        if ((function->saved & (1u << reg)) != 0)
        {
            cfa += 8;
            unwind_row(rows, &count, capacity, function->offsets[event], EMISSION_FRAME_CFA_OFFSET, 0, cfa);
            unwind_row(rows, &count, capacity, function->offsets[event], EMISSION_FRAME_OFFSET, unwind_registers[reg], -cfa);
            event += 1;
        }
    }
    if (function->pointer == 0 && function->frame_size != 0)
    {
        unwind_row(rows, &count, capacity, function->offsets[event], EMISSION_FRAME_CFA_OFFSET, 0, cfa + function->frame_size);
        event += 1;
    }
    // The tail calls' and the epilogue's teardowns: the same transitions at their own ends.
    for (uint32_t tail = 0; tail <= function->tail_count; tail += 1)
    {
        const bool epilogue = tail == function->tail_count;
        const uint32_t* ends = epilogue ? function->offsets + event : function->tails + (uint64_t)tail * per;
        if (epilogue && function->open != 0)
        {
            break;
        }
        uint32_t at = 0;
        if (!epilogue)
        {
            const uint32_t before = count != 0 && count <= capacity ? rows[count - 1].offset : 0;
            unwind_row(rows, &count, capacity, before, EMISSION_FRAME_REMEMBER_STATE, 0, 0);
        }
        int32_t teardown = 8 + 8 * (int32_t)pushes;
        if (function->pointer == 0 && function->frame_size != 0)
        {
            unwind_row(rows, &count, capacity, ends[at], EMISSION_FRAME_CFA_OFFSET, 0, teardown);
            at += 1;
        }
        for (uint32_t reg = BUSTER_UNWIND_SAVED; reg > 0; reg -= 1)
        {
            if ((function->saved & (1u << (reg - 1))) != 0)
            {
                if (function->pointer == 0)
                {
                    teardown -= 8;
                    unwind_row(rows, &count, capacity, ends[at], EMISSION_FRAME_CFA_OFFSET, 0, teardown);
                }
                unwind_row(rows, &count, capacity, ends[at], EMISSION_FRAME_RESTORE, unwind_registers[reg - 1], 0);
                at += 1;
            }
        }
        if (function->pointer != 0)
        {
            unwind_row(rows, &count, capacity, ends[at], EMISSION_FRAME_CFA, BUSTER_EMISSION_X86_RSP, 8);
            unwind_row(rows, &count, capacity, ends[at], EMISSION_FRAME_RESTORE, unwind_registers[1], 0);
            at += 1;
        }
        if (!epilogue)
        {
            unwind_row(rows, &count, capacity, ends[at], EMISSION_FRAME_RESTORE_STATE, 0, 0);
        }
    }
    return count;
}

// Rows' canonical form, in place: the rows at the function's end describe no instruction, and a state restored there
// restores nothing, so they go, each restored state's remembering with it (a function whose last return is its last
// record has no state to remember for its epilogue, whose rules no instruction follows). Returns how many rows remain.
// The rows' offsets don't decrease, as resolution leaves them.
static uint32_t unwind_rows_canonical(EmissionFrameRow* rows, uint32_t count, uint32_t size)
{
    uint32_t end = count;
    uint32_t unmatched = 0;
    while (end > 0 && rows[end - 1].offset >= size)
    {
        unmatched += rows[end - 1].kind == EMISSION_FRAME_RESTORE_STATE ? 1 : 0;
        end -= 1;
    }
    // Each restored state's remembering: the latest ones that no restoring before the end matches, as many as went.
    for (uint32_t i = end; unmatched != 0 && i > 0; i -= 1)
    {
        if (rows[i - 1].kind == EMISSION_FRAME_REMEMBER_STATE)
        {
            int64_t balance = 1;
            for (uint32_t k = i; balance != 0 && k < end; k += 1)
            {
                balance += rows[k].kind == EMISSION_FRAME_REMEMBER_STATE ? 1 : rows[k].kind == EMISSION_FRAME_RESTORE_STATE ? -1 : 0;
            }
            if (balance != 0)
            {
                memmove(rows + i - 1, rows + i, (uint64_t)(end - i) * sizeof(EmissionFrameRow));
                end -= 1;
                unmatched -= 1;
            }
        }
    }
    return end;
}

// A function's descriptor from its rows (unwind_rows_canonical's): its simple fixed shape where the rows are exactly
// one's (pushes, an allocation, its release and pops: unwind_function_rows of the descriptor they spell), which the
// vector writer takes; otherwise a row program over them, which the exact writer writes, and whose rows must outlive
// the descriptor. Refuses rows unwind_rows_check refuses.
static UnwindError unwind_rows_function(const EmissionFrameRow* rows, uint32_t count, uint32_t size, UnwindFunction* output)
{
    UnwindFunction program = {
        .size = size,
        .open = BUSTER_UNWIND_ROWS,
        .rows = count != 0 ? rows : 0,
        .row_count = count,
    };
    UnwindError error = unwind_rows_check(&program);
    // The simple shape's candidate: its pushes' registers, the allocation past them and its transitions' offsets.
    UnwindFunction simple = { .size = size };
    bool candidate = error == UNWIND_OK && count != 0;
    uint32_t events = 0;
    for (uint32_t i = 0; candidate && i < count; i += 1)
    {
        const EmissionFrameRow row = rows[i];
        candidate = row.kind == EMISSION_FRAME_CFA_OFFSET || row.kind == EMISSION_FRAME_OFFSET || row.kind == EMISSION_FRAME_RESTORE;
        if (candidate && row.kind == EMISSION_FRAME_OFFSET)
        {
            uint32_t bit = BUSTER_UNWIND_SAVED;
            for (uint32_t r = 0; r < BUSTER_UNWIND_SAVED; r += 1)
            {
                bit = unwind_registers[r] == row.reg ? r : bit;
            }
            candidate = bit < BUSTER_UNWIND_SAVED && (simple.saved & (1u << bit)) == 0;
            simple.saved |= candidate ? (uint8_t)(1u << bit) : 0;
        }
        if (candidate && (i == 0 || row.offset != rows[i - 1].offset))
        {
            candidate = events < BUSTER_UNWIND_EVENTS;
            simple.offsets[candidate ? events : 0] = row.offset;
            events += 1;
        }
    }
    if (candidate)
    {
        // The allocation: the largest CFA offset past the pushes'.
        int64_t largest = 8;
        for (uint32_t i = 0; i < count; i += 1)
        {
            largest = rows[i].kind == EMISSION_FRAME_CFA_OFFSET && rows[i].value > largest ? rows[i].value : largest;
        }
        const int64_t pushed = 8 + 8 * __builtin_popcount(simple.saved);
        // Rows can express more than the fixed imm32 allocation: check before narrowing.
        candidate = largest >= pushed && largest - pushed <= INT32_MAX;
        simple.frame_size = candidate ? (uint32_t)(largest - pushed) : 0;
        simple.event_count = (uint8_t)events;
        candidate = candidate && events == unwind_events(&simple) && unwind_check(&simple, 1) == UNWIND_OK;
    }
    if (candidate)
    {
        EmissionFrameRow canonical[3 * BUSTER_UNWIND_EVENTS];
        const uint32_t expected = unwind_function_rows(&simple, canonical, BUSTER_ARRAY_LENGTH(canonical));
        candidate = expected == count && memcmp(canonical, rows, (uint64_t)count * sizeof(EmissionFrameRow)) == 0;
    }
    if (error == UNWIND_OK)
    {
        *output = candidate ? simple : program;
        if (count == 0)
        {
            // No rows: the CIE's rules throughout, the frameless shape.
            *output = (UnwindFunction){ .size = size };
        }
    }
    return error;
}

// ---------------------------------------------------------------------------------------------------------------------
// Original template construction, once before the batch writers

// The same CIE the scalar writer constructs field by field. Six trailing nops align its successor to eight bytes.
static const uint8_t unwind_cie[BUSTER_UNWIND_CIE_BYTES] = {
    36, 0, 0, 0, 0, 0, 0, 0, 1, 'z', 'R', 0, 1, 0x78, 16, 1,
    0x1b, 0x0c, 7, 8, 0x90, 1, 0x08, 3, 0x08, 6, 0x08, 12, 0x08, 13, 0x08, 14,
    0x08, 15, 0, 0, 0, 0, 0, 0,
};

// Each shape's FDE: its bytes, which hole (a byte of the two vectors unwind_function_vector permutes) fills each byte
// that has one, a bit per such byte, and its size.
static BUSTER_BAKED_UNIT uint8_t unwind_templates[BUSTER_UNWIND_SHAPES][BUSTER_UNWIND_TEMPLATE_BYTES] __attribute__((aligned(64)));
static BUSTER_BAKED_UNIT uint8_t unwind_indices[BUSTER_UNWIND_SHAPES][BUSTER_UNWIND_TEMPLATE_BYTES] __attribute__((aligned(64)));
static BUSTER_BAKED_UNIT uint64_t unwind_patches[BUSTER_UNWIND_SHAPES][BUSTER_UNWIND_TEMPLATE_BYTES / 64];
static BUSTER_BAKED_UNIT uint32_t unwind_sizes[BUSTER_UNWIND_SHAPES];
static BUSTER_BAKED_UNIT bool unwind_ready;

typedef struct UnwindTemplateBuilder UnwindTemplateBuilder;
struct UnwindTemplateBuilder
{
    uint32_t shape;
    uint32_t size;
    bool valid;
};

static void unwind_template_byte(UnwindTemplateBuilder* builder, uint8_t value, uint32_t source)
{
    const uint32_t at = builder->size;
    if (at >= BUSTER_UNWIND_TEMPLATE_BYTES)
    {
        builder->valid = false;
    }
    else
    {
        unwind_templates[builder->shape][at] = value;
        if (source < 128)
        {
            unwind_indices[builder->shape][at] = (uint8_t)source;
            unwind_patches[builder->shape][at / 64] |= 1ull << (at & 63);
        }
    }
    builder->size += 1;
}

static void unwind_template_advance(UnwindTemplateBuilder* builder, uint32_t event)
{
    // The second input vector's bytes 12..25 hold the opcodes. Its first three dwords hold the final CFA byte, CIE
    // pointer and compact FDE length. The first input vector keeps the four-byte delta slots even for short forms.
    unwind_template_byte(builder, 0, 76 + event);
    for (uint32_t byte = 0; byte < 4; byte += 1)
    {
        unwind_template_byte(builder, 0, 4 * event + byte);
    }
}

static bool unwind_initialize(void)
{
    memset(unwind_templates, 0, sizeof(unwind_templates));
    memset(unwind_indices, 0, sizeof(unwind_indices));
    memset(unwind_patches, 0, sizeof(unwind_patches));
    bool valid = true;
    for (uint32_t saved = 0; saved < 64; saved += 1)
    {
        for (uint32_t width = 0; width < BUSTER_UNWIND_SHAPE_WIDTHS; width += 1)
        {
            UnwindTemplateBuilder builder = {
                .shape = saved * BUSTER_UNWIND_SHAPE_WIDTHS + width,
                .valid = true,
            };
            // Compact FDE length (the second vector's dword 2), backward CIE pointer (its dword 1), relocated initial
            // PC, range (the first vector's dword 14), and zero augmentation length.
            for (uint32_t byte = 0; byte < 17; byte += 1)
            {
                const uint32_t source = byte < 4 ? 72 + byte : byte < 8 ? 64 + byte : byte >= 12 && byte < 16 ? 56 + byte - 12 : 128;
                unwind_template_byte(&builder, 0, source);
            }
            uint32_t cfa = 8;
            uint32_t event = 0;
            for (uint32_t reg = 0; reg < BUSTER_UNWIND_SAVED; reg += 1)
            {
                if ((saved & (1u << reg)) != 0)
                {
                    unwind_template_advance(&builder, event);
                    event += 1;
                    cfa += 8;
                    unwind_template_byte(&builder, 0x0e, 128);
                    unwind_template_byte(&builder, (uint8_t)cfa, 128);
                    unwind_template_byte(&builder, (uint8_t)(0x80 | unwind_registers[reg]), 128);
                    unwind_template_byte(&builder, (uint8_t)(cfa / 8), 128);
                }
            }
            if (width != 0)
            {
                unwind_template_advance(&builder, event);
                event += 1;
                unwind_template_byte(&builder, 0x0e, 128);
                for (uint32_t byte = 0; byte < width; byte += 1)
                {
                    unwind_template_byte(&builder, 0, 60 + byte);
                }
                unwind_template_advance(&builder, event);
                event += 1;
                unwind_template_byte(&builder, 0x0e, 128);
                unwind_template_byte(&builder, (uint8_t)cfa, 128);
            }
            for (uint32_t reg = BUSTER_UNWIND_SAVED; reg > 0; reg -= 1)
            {
                if ((saved & (1u << (reg - 1))) != 0)
                {
                    unwind_template_advance(&builder, event);
                    event += 1;
                    cfa -= 8;
                    unwind_template_byte(&builder, 0x0e, 128);
                    unwind_template_byte(&builder, (uint8_t)cfa, 128);
                    unwind_template_byte(&builder, (uint8_t)(0xc0 | unwind_registers[reg - 1]), 128);
                }
            }
            while ((builder.size & 7) != 0)
            {
                unwind_template_byte(&builder, 0, 128);
            }
            unwind_sizes[builder.shape] = builder.size;
            valid = valid && builder.valid && event <= BUSTER_UNWIND_EVENTS;
        }
    }
    unwind_ready = valid;
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// Eight-descriptor classification and bounded byte-template expansion

typedef struct UnwindBatch UnwindBatch;
struct UnwindBatch
{
    uint32_t shapes[BUSTER_UNWIND_BATCH];
    // Each FDE's bytes, and all of them.
    uint32_t sizes[BUSTER_UNWIND_BATCH];
    uint64_t lebs[BUSTER_UNWIND_BATCH];
    uint64_t bytes;
    bool limited;
};

// All fourteen offsets are inside the descriptor. Inactive events are masked out by the size pass; the template
// names only the active ones during emission. No padding beyond the descriptor is read.
static __m512i unwind_deltas(const UnwindFunction* function)
{
    const __m512i offsets = _mm512_maskz_loadu_epi32(0x3fff, function->offsets);
    const __m512i before = _mm512_maskz_permutexvar_epi32(0xfffe, _mm512_setr_epi32(0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14), offsets);
    return _mm512_sub_epi32(offsets, before);
}

// Bytes removed from a fixed shape's loc4 template: 4, 3, 2 or 0 per event. Threshold masks classify all events at
// once; the independent scalar counter instead sums each event's actual width in PC order.
static uint32_t unwind_advance_reduction(const UnwindFunction* function)
{
    const __mmask16 events = (__mmask16)((1u << function->event_count) - 1);
    const __m512i deltas = unwind_deltas(function);
    const __mmask16 primary = events & _mm512_cmplt_epu32_mask(deltas, _mm512_set1_epi32(64));
    const __mmask16 byte = events & _mm512_cmplt_epu32_mask(deltas, _mm512_set1_epi32(256));
    const __mmask16 word = events & _mm512_cmplt_epu32_mask(deltas, _mm512_set1_epi32(65536));
    return (uint32_t)__builtin_popcount(primary) + (uint32_t)__builtin_popcount(byte) + 2 * (uint32_t)__builtin_popcount(word);
}

static UnwindBatch unwind_batch(const UnwindFunction* functions, uint32_t count)
{
    const __m512i zero = _mm512_setzero_si512();
    const __mmask8 lanes = (__mmask8)((1u << count) - 1);
    const uint32_t stride = sizeof(UnwindFunction);
    const __m256i positions = _mm256_setr_epi32(0, stride, 2 * stride, 3 * stride, 4 * stride, 5 * stride, 6 * stride, 7 * stride);
    const __m512i heads = _mm512_mask_i32gather_epi64(zero, lanes, positions, functions, 1);
    // Each function's saved, event_count, pointer and open bytes, and its tail_count above them.
    _Static_assert(offsetof(UnwindFunction, tail_count) == offsetof(UnwindFunction, saved) + 4, "the metadata's 8 bytes");
    const __m256i metadata_positions = _mm256_add_epi32(positions, _mm256_set1_epi32(offsetof(UnwindFunction, saved)));
    const __m512i metadata = _mm512_mask_i32gather_epi64(zero, lanes, metadata_positions, functions, 1);
    const __m512i saved = _mm512_and_si512(metadata, _mm512_set1_epi64(63));
    const __m512i pushes = _mm512_popcnt_epi64(saved);
    const __m512i frame = _mm512_srli_epi64(heads, 32);
    const __m512i range = _mm512_and_si512(heads, _mm512_set1_epi64(UINT32_MAX));
    const __m512i cfa = _mm512_add_epi64(frame, _mm512_add_epi64(_mm512_set1_epi64(8), _mm512_slli_epi64(pushes, 3)));
    __m512i width = _mm512_set1_epi64(1);
    for (uint32_t bits = 7; bits <= 28; bits += 7)
    {
        width = _mm512_mask_add_epi64(width, _mm512_cmpgt_epu64_mask(cfa, _mm512_set1_epi64((1ull << bits) - 1)), width, _mm512_set1_epi64(1));
    }
    width = _mm512_maskz_mov_epi64(_mm512_cmpneq_epi64_mask(frame, zero), width);
    const __m512i shapes = _mm512_add_epi64(_mm512_mullo_epi64(saved, _mm512_set1_epi64(BUSTER_UNWIND_SHAPE_WIDTHS)), width);
    // A frame pointer, tail calls (#147), open frame (#177) or row program uses the exact writer and size counter.
    const __m512i flags = _mm512_and_si512(_mm512_srli_epi64(metadata, 16), _mm512_set1_epi64(0xff));
    const __mmask8 opened = _mm512_mask_cmpneq_epi64_mask(lanes, _mm512_and_si512(_mm512_srli_epi64(metadata, 24), _mm512_set1_epi64(0xff)), zero);
    const __mmask8 tails = _mm512_mask_cmpneq_epi64_mask(lanes, _mm512_srli_epi64(metadata, 32), zero) | opened;
    const __mmask8 pointers = _mm512_mask_cmpneq_epi64_mask(lanes, flags, zero);
    const __mmask8 exact = pointers | tails;
    const __m256i shape_indices = _mm256_mask_mov_epi32(_mm512_cvtepi64_epi32(shapes), exact, _mm256_set1_epi32(BUSTER_UNWIND_SHAPE_POINTER));
    // The loc4 template's unaligned size: compact before rounding, otherwise original padding can waste eight bytes.
    __m512i sizes = _mm512_add_epi64(_mm512_set1_epi64(17), _mm512_mullo_epi64(pushes, _mm512_set1_epi64(17)));
    sizes = _mm512_mask_add_epi64(sizes, _mm512_cmpneq_epi64_mask(width, zero), sizes, _mm512_add_epi64(width, _mm512_set1_epi64(13)));
    const __m512i byte_positions = _mm512_set1_epi64(0x0706050403020100ull);
    const __m512i widths = _mm512_mullo_epi64(width, _mm512_set1_epi64(0x0101010101010101ull));
    const __mmask64 continuation = _mm512_cmplt_epu8_mask(_mm512_add_epi8(byte_positions, _mm512_set1_epi8(1)), widths);
    __m512i groups = _mm512_and_si512(_mm512_multishift_epi64_epi8(_mm512_set1_epi64(0x312a231c150e0700ull), cfa), _mm512_set1_epi8(0x7f));
    groups = _mm512_or_si512(groups, _mm512_maskz_set1_epi8(continuation, (char)0x80));
    UnwindBatch batch;
    _mm256_storeu_si256((__m256i*)batch.shapes, shape_indices);
    _mm256_storeu_si256((__m256i*)batch.sizes, _mm512_cvtepi64_epi32(sizes));
    _mm512_storeu_si512(batch.lebs, groups);
    batch.bytes = 0;
    batch.limited = ((_mm512_cmpgt_epu64_mask(range, _mm512_set1_epi64(INT32_MAX)) |
                      _mm512_cmpgt_epu64_mask(frame, _mm512_set1_epi64(INT32_MAX))) & lanes) != 0;
    // Fixed descriptors have contiguous event columns, so each lane classifies fourteen deltas together. Exceptional
    // shapes retain the exact counter, including the tail-event columns between their prologue and epilogue.
    for (uint32_t lane = 0; lane < count; lane += 1)
    {
        const uint64_t bytes = (exact >> lane & 1) != 0 ? unwind_fde_bytes(functions + lane) :
                               (batch.sizes[lane] - unwind_advance_reduction(functions + lane) + 7) & ~UINT32_C(7);
        batch.sizes[lane] = bytes <= UINT32_MAX ? (uint32_t)bytes : 0;
        batch.limited |= bytes > UINT32_MAX;
        batch.bytes += bytes;
    }
    return batch;
}

// A function's FDE from its shape's loc4 template, compacted to `size`, with its CIE `cie_pointer` bytes back from its
// CIE pointer's field. The input slots retain four bytes per delta; a parallel keep mask removes unused operand bytes
// after expansion. The old template padding may be too long or too short after compaction, so clip it or append nops.
static void unwind_function_vector(uint8_t* output, const UnwindFunction* function, uint32_t shape, uint64_t leb, uint32_t cie_pointer, uint32_t size)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i ones = _mm512_set1_epi32(-1);
    const __m512i deltas = unwind_deltas(function);
    const __mmask16 primary = _mm512_cmplt_epu32_mask(deltas, _mm512_set1_epi32(64));
    const __mmask16 byte = _mm512_cmplt_epu32_mask(deltas, _mm512_set1_epi32(256));
    const __mmask16 word = _mm512_cmplt_epu32_mask(deltas, _mm512_set1_epi32(65536));
    __m512i opcodes = _mm512_mask_set1_epi32(_mm512_set1_epi32(4), word, 3);
    opcodes = _mm512_mask_set1_epi32(opcodes, byte, 2);
    opcodes = _mm512_mask_mov_epi32(opcodes, primary, _mm512_or_si512(deltas, _mm512_set1_epi32(0x40)));
    __m512i operands = _mm512_mask_set1_epi32(ones, word, 0xffff);
    operands = _mm512_mask_set1_epi32(operands, byte, 0xff);
    operands = _mm512_mask_set1_epi32(operands, primary, 0);
    operands = _mm512_mask_set1_epi32(operands, 0xc000, -1); // Range and CFA bytes are always retained.
    __m512i holes = deltas;
    holes = _mm512_mask_set1_epi32(holes, 0x4000, (int32_t)function->size);
    holes = _mm512_mask_set1_epi32(holes, 0x8000, (int32_t)leb);
    __m512i high = _mm512_mask_set1_epi32(_mm512_maskz_set1_epi32(1, (int32_t)(leb >> 32)), 2, (int32_t)cie_pointer);
    high = _mm512_mask_set1_epi32(high, 4, (int32_t)(size - 4));
    high = _mm512_or_si512(high, _mm512_maskz_expand_epi8(UINT64_C(0x3fff) << 12, _mm512_castsi128_si512(_mm512_cvtepi32_epi8(opcodes))));
    const uint32_t length = unwind_sizes[shape];
    uint32_t written = 0;
    for (uint32_t at = 0; at < length; at += 64)
    {
        const __m512i bytes = _mm512_load_si512(unwind_templates[shape] + at);
        const __m512i indices = _mm512_load_si512(unwind_indices[shape] + at);
        const __m512i values = _mm512_permutex2var_epi8(holes, indices, high);
        const __mmask64 patches = unwind_patches[shape][at / 64];
        const __m512i complete = _mm512_mask_mov_epi8(bytes, patches, values);
        const __m512i keep_values = _mm512_permutex2var_epi8(operands, indices, ones);
        const uint32_t remaining = length - at;
        const __mmask64 lanes = remaining >= 64 ? (__mmask64)UINT64_MAX : (__mmask64)((1ull << remaining) - 1);
        const __mmask64 keep = lanes & (~patches | _mm512_cmpneq_epi8_mask(keep_values, zero));
        const uint32_t kept = (uint32_t)__builtin_popcountll(keep);
        const uint32_t store = kept < size - written ? kept : size - written;
        const __mmask64 stores = store >= 64 ? (__mmask64)UINT64_MAX : (__mmask64)((1ull << store) - 1);
        const __m512i compact = _mm512_maskz_compress_epi8(keep, complete);
        _mm512_mask_storeu_epi8(output + written, stores, compact);
        written += store;
    }
    memset(output + written, 0, size - written);
}

// Descriptors satisfy unwind_check, and tables are initialized. Only limits and output capacities are checked here;
// the producer's per-event proof is not repeated. A size query is a normal CAPACITY result when storage is absent.
static UnwindError unwind_write(const UnwindFunction* functions, uint32_t count, UnwindOutput* output, bool exact)
{
    UnwindError result = output == 0 || (count != 0 && functions == 0) || !unwind_ready ? UNWIND_ERROR_INVALID : UNWIND_OK;
    if (result == UNWIND_OK && count > UINT32_MAX / BUSTER_UNWIND_PAIR_MIN)
    {
        result = UNWIND_ERROR_LIMIT;
    }
    // The CIEs: the shared one, or one before each FDE.
    const bool shared = output != 0 && output->shared;
    uint64_t required = shared ? (count != 0 ? BUSTER_UNWIND_CIE_BYTES : 0) : (uint64_t)count * BUSTER_UNWIND_CIE_BYTES;
    for (uint32_t first = 0; result == UNWIND_OK && first < count;)
    {
        const uint32_t remaining = count - first;
        const uint32_t lanes = exact ? 1 : remaining < BUSTER_UNWIND_BATCH ? remaining : BUSTER_UNWIND_BATCH;
        if (exact)
        {
            const UnwindFunction* function = &functions[first];
            if (function->size > INT32_MAX || function->frame_size > INT32_MAX)
            {
                result = UNWIND_ERROR_LIMIT;
            }
            else
            {
                required += unwind_fde_bytes(function);
            }
        }
        else
        {
            const UnwindBatch batch = unwind_batch(functions + first, lanes);
            required += batch.bytes;
            result = batch.limited ? UNWIND_ERROR_LIMIT : UNWIND_OK;
        }
        result = required > UINT32_MAX ? UNWIND_ERROR_LIMIT : result;
        first += lanes;
    }
    if (result == UNWIND_OK)
    {
        output->size = required;
        if (required > output->capacity || count > output->location_capacity ||
            (required != 0 && output->bytes == 0) || (count != 0 && output->locations == 0))
        {
            result = UNWIND_ERROR_CAPACITY;
        }
    }
    if (result == UNWIND_OK)
    {
        UnwindWriter writer = {
            .bytes = output->bytes,
        };
        uint64_t cie = 0;
        if (shared && count != 0)
        {
            if (exact)
            {
                unwind_cie_scalar(&writer);
            }
            else
            {
                memcpy(writer.bytes, unwind_cie, BUSTER_UNWIND_CIE_BYTES);
                writer.size = BUSTER_UNWIND_CIE_BYTES;
            }
        }
        for (uint32_t first = 0; first < count;)
        {
            const uint32_t remaining = count - first;
            const uint32_t lanes = exact ? 1 : remaining < BUSTER_UNWIND_BATCH ? remaining : BUSTER_UNWIND_BATCH;
            if (exact)
            {
                cie = shared ? 0 : writer.size;
                if (!shared)
                {
                    unwind_cie_scalar(&writer);
                }
                output->locations[first] = (uint32_t)(writer.size + BUSTER_UNWIND_FDE_LOCATION);
                unwind_fde_scalar(&writer, &functions[first], cie);
            }
            else
            {
                const UnwindBatch batch = unwind_batch(functions + first, lanes);
                for (uint32_t lane = 0; lane < lanes; lane += 1)
                {
                    cie = shared ? 0 : writer.size;
                    if (!shared)
                    {
                        memcpy(writer.bytes + writer.size, unwind_cie, BUSTER_UNWIND_CIE_BYTES);
                        writer.size += BUSTER_UNWIND_CIE_BYTES;
                    }
                    output->locations[first + lane] = (uint32_t)(writer.size + BUSTER_UNWIND_FDE_LOCATION);
                    if (batch.shapes[lane] == BUSTER_UNWIND_SHAPE_POINTER)
                    {
                        unwind_fde_scalar(&writer, &functions[first + lane], cie);
                    }
                    else
                    {
                        unwind_function_vector(writer.bytes + writer.size, &functions[first + lane], batch.shapes[lane], batch.lebs[lane],
                                               (uint32_t)(writer.size + 4 - cie), batch.sizes[lane]);
                        writer.size += batch.sizes[lane];
                    }
                }
            }
            first += lanes;
        }
    }
    return result;
}
