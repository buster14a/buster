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
// deltas, the function range, the CIE pointer and five CFA bytes are the only holes; the CIE is the same bytes for
// every function (unwind_cie). A function with a frame pointer, the rare shape (a C function that allocates on the
// stack as it runs), and one with tail calls (#147's musttail), rarer, the vector path leaves to the exact writer, lane by
// lane. Neither inputs nor outputs need
// padding; masked tails touch only their slices. Input, byte output and location output must be disjoint. Output
// sizes and every representability limit are checked before stores. CAPACITY publishes the required size but
// preserves bytes/locations; other errors preserve the output descriptor too. Empty batches need no byte or location
// storage.
//
// The encodings are format facts from DWARF call-frame instructions and the AMD64 .eh_frame convention. These
// templates and both writers are original; no external implementation is included.

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

static void unwind_integer(UnwindWriter* writer, uint32_t value, uint32_t bytes)
{
    memcpy(writer->bytes + writer->size, &value, bytes);
    writer->size += bytes;
}

static void unwind_uleb(UnwindWriter* writer, uint32_t value)
{
    do
    {
        const uint32_t byte = (value & 0x7f) | (value > 0x7f ? 0x80 : 0);
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

// A function's FDE's bytes as the exact writer writes them: its header and its transitions, to a multiple of 8; its
// pair's are BUSTER_UNWIND_CIE_BYTES more. The batch writer works the same sizes out in lanes (unwind_batch), but for a
// function with tail calls, each of which adds its remembered state, its teardown's transitions as the epilogue's and its
// jump's restored state (unwind_tails_scalar), and for an open one, which has no epilogue's transitions at its end: with
// a frame pointer each pop's 6 bytes and rbp's 9, and without one each pop's 8 and the release's 7.
static uint64_t unwind_fde_bytes(const UnwindFunction* function)
{
    const uint32_t saved = (uint32_t)__builtin_popcount(function->saved);
    const uint32_t cfa = 8 + 8 * saved + function->frame_size;
    const uint64_t bytes = function->pointer != 0 ? 42 + 13 * saved : 17 + 17 * saved + (function->frame_size != 0 ? 13 + unwind_uleb_size(cfa) : 0);
    const uint64_t epilogue = function->open == 0 ? 0 : function->pointer != 0 ? 9 + 6 * saved : 8 * saved + (function->frame_size != 0 ? 7 : 0);
    const uint64_t tail = function->pointer != 0 ? 16 + 6 * saved : 7 + 8 * saved + (function->frame_size != 0 ? 7 : 0);
    return (bytes - epilogue + tail * function->tail_count + 7) & ~UINT64_C(7);
}

static void unwind_advance(UnwindWriter* writer, uint32_t offset, uint32_t* previous)
{
    unwind_integer(writer, 4, 1); // DW_CFA_advance_loc4
    unwind_integer(writer, offset - *previous, 4);
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

// A function's FDE, pointing back to the CIE at `cie`.
static void unwind_fde_scalar(UnwindWriter* writer, const UnwindFunction* function, uint64_t cie)
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
static uint8_t unwind_templates[BUSTER_UNWIND_SHAPES][BUSTER_UNWIND_TEMPLATE_BYTES] __attribute__((aligned(64)));
static uint8_t unwind_indices[BUSTER_UNWIND_SHAPES][BUSTER_UNWIND_TEMPLATE_BYTES] __attribute__((aligned(64)));
static uint64_t unwind_patches[BUSTER_UNWIND_SHAPES][BUSTER_UNWIND_TEMPLATE_BYTES / 64];
static uint32_t unwind_sizes[BUSTER_UNWIND_SHAPES];
static bool unwind_ready;

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
    unwind_template_byte(builder, 4, 128);
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
            // FDE length, backward CIE pointer (the second vector's dword 1), relocated initial PC, range (the first's
            // dword 14), and zero augmentation length.
            for (uint32_t byte = 0; byte < 17; byte += 1)
            {
                const uint32_t source = byte >= 4 && byte < 8 ? 64 + byte : byte >= 12 && byte < 16 ? 56 + byte - 12 : 128;
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
            const uint32_t length = builder.size - 4;
            memcpy(unwind_templates[builder.shape], &length, 4);
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
    const __m512i frame = _mm512_srli_epi64(heads, 32);
    const __m512i range = _mm512_and_si512(heads, _mm512_set1_epi64(UINT32_MAX));
    const __m512i cfa = _mm512_add_epi64(frame, _mm512_add_epi64(_mm512_set1_epi64(8), _mm512_slli_epi64(_mm512_popcnt_epi64(saved), 3)));
    __m512i width = _mm512_set1_epi64(1);
    for (uint32_t bits = 7; bits <= 28; bits += 7)
    {
        width = _mm512_mask_add_epi64(width, _mm512_cmpgt_epu64_mask(cfa, _mm512_set1_epi64((1ull << bits) - 1)), width, _mm512_set1_epi64(1));
    }
    width = _mm512_maskz_mov_epi64(_mm512_cmpneq_epi64_mask(frame, zero), width);
    const __m512i shapes = _mm512_add_epi64(_mm512_mullo_epi64(saved, _mm512_set1_epi64(BUSTER_UNWIND_SHAPE_WIDTHS)), width);
    // A function with a frame pointer: the exact writer's FDE, 42 + 13n bytes to a multiple of 8 (unwind_fde_bytes). One
    // with tail calls (#147), or an open one (#177), is the exact writer's too, its size the scalar pass's below.
    const __m512i flags = _mm512_and_si512(_mm512_srli_epi64(metadata, 16), _mm512_set1_epi64(0xff));
    const __mmask8 opened = _mm512_mask_cmpneq_epi64_mask(lanes, _mm512_and_si512(_mm512_srli_epi64(metadata, 24), _mm512_set1_epi64(0xff)), zero);
    const __mmask8 tails = _mm512_mask_cmpneq_epi64_mask(lanes, _mm512_srli_epi64(metadata, 32), zero) | opened;
    const __mmask8 pointers = _mm512_mask_cmpneq_epi64_mask(lanes, flags, zero) & (__mmask8)~tails;
    const __mmask8 exact = pointers | tails;
    const __m512i pointer_bytes = _mm512_and_si512(_mm512_add_epi64(_mm512_mullo_epi64(_mm512_popcnt_epi64(saved), _mm512_set1_epi64(13)), _mm512_set1_epi64(42 + 7)),
                                                   _mm512_set1_epi64(~7ll));
    const __m256i shape_indices = _mm256_mask_mov_epi32(_mm512_cvtepi64_epi32(shapes), exact, _mm256_set1_epi32(BUSTER_UNWIND_SHAPE_POINTER));
    const __m512i sizes = _mm512_maskz_mov_epi32((__mmask16)(lanes & ~tails),
                                                 _mm512_mask_mov_epi32(_mm512_mask_i32gather_epi32(zero, lanes & ~exact, _mm512_castsi256_si512(shape_indices), unwind_sizes, 4),
                                                                       pointers, _mm512_castsi256_si512(_mm512_cvtepi64_epi32(pointer_bytes))));
    const __m512i byte_positions = _mm512_set1_epi64(0x0706050403020100ull);
    const __m512i widths = _mm512_mullo_epi64(width, _mm512_set1_epi64(0x0101010101010101ull));
    const __mmask64 continuation = _mm512_cmplt_epu8_mask(_mm512_add_epi8(byte_positions, _mm512_set1_epi8(1)), widths);
    __m512i groups = _mm512_and_si512(_mm512_multishift_epi64_epi8(_mm512_set1_epi64(0x312a231c150e0700ull), cfa), _mm512_set1_epi8(0x7f));
    groups = _mm512_or_si512(groups, _mm512_maskz_set1_epi8(continuation, (char)0x80));
    UnwindBatch batch;
    _mm256_storeu_si256((__m256i*)batch.shapes, shape_indices);
    _mm256_storeu_si256((__m256i*)batch.sizes, _mm512_castsi512_si256(sizes));
    _mm512_storeu_si512(batch.lebs, groups);
    batch.bytes = (uint32_t)_mm512_reduce_add_epi32(sizes);
    batch.limited = ((_mm512_cmpgt_epu64_mask(range, _mm512_set1_epi64(INT32_MAX)) |
                      _mm512_cmpgt_epu64_mask(frame, _mm512_set1_epi64(INT32_MAX))) & lanes) != 0;
    // The tail calls' and open functions, rare: each one's FDE's bytes, counted as the exact writer writes them, in a
    // scalar pass.
    for (uint32_t lane = 0; tails != 0 && lane < count; lane += 1)
    {
        if ((tails >> lane & 1) != 0)
        {
            const uint64_t bytes = unwind_fde_bytes(functions + lane);
            batch.sizes[lane] = bytes <= UINT32_MAX ? (uint32_t)bytes : 0;
            batch.limited |= bytes > UINT32_MAX;
            batch.bytes += bytes;
        }
    }
    return batch;
}

// A function's FDE from its shape's template, its CIE `cie_pointer` bytes back from its CIE pointer's field.
static void unwind_function_vector(uint8_t* output, const UnwindFunction* function, uint32_t shape, uint64_t leb, uint32_t cie_pointer)
{
    const __m512i offsets = _mm512_maskz_loadu_epi32(0x3fff, function->offsets);
    const __m512i before = _mm512_maskz_permutexvar_epi32(0xfffe, _mm512_setr_epi32(0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14), offsets);
    __m512i holes = _mm512_sub_epi32(offsets, before);
    holes = _mm512_mask_set1_epi32(holes, 0x4000, (int32_t)function->size);
    holes = _mm512_mask_set1_epi32(holes, 0x8000, (int32_t)leb);
    const __m512i high = _mm512_mask_set1_epi32(_mm512_maskz_set1_epi32(1, (int32_t)(leb >> 32)), 2, (int32_t)cie_pointer);
    const uint32_t length = unwind_sizes[shape];
    for (uint32_t at = 0; at < length; at += 64)
    {
        const __m512i bytes = _mm512_load_si512(unwind_templates[shape] + at);
        const __m512i indices = _mm512_load_si512(unwind_indices[shape] + at);
        const __m512i values = _mm512_permutex2var_epi8(holes, indices, high);
        const __m512i complete = _mm512_mask_mov_epi8(bytes, unwind_patches[shape][at / 64], values);
        const uint32_t remaining = length - at;
        const __mmask64 lanes = remaining >= 64 ? (__mmask64)UINT64_MAX : (__mmask64)((1ull << remaining) - 1);
        _mm512_mask_storeu_epi8(output + at, lanes, complete);
    }
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
                                               (uint32_t)(writer.size + 4 - cie));
                        writer.size += batch.sizes[lane];
                    }
                }
            }
            first += lanes;
        }
    }
    return result;
}
