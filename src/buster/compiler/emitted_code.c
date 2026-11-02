// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The tier-neutral emitted function (#1056): what a code generator hands the C driver's emission (check_program_emit),
// which sizes its branches, encodes it, publishes its atom, symbols and fixups, and from its metadata writes its source
// rows (-g) and its unwind table. Tier 0's code (tier0_compile's, its helpers', the assembly templates') adapts to it
// (emitted_from_tier0), and Tier 1's publishes it itself (check_tier1_compile): no consumer reads a Tier0Code, and no
// producer describes its frame as another's.
//
// Its slices, all borrowed from the producer's arenas, which outlive the emission's reading of them:
//
// - the code: the instruction records, the branch columns (each branch's record and its target's record, ascending,
//   which branch sizing reads), the symbolic references (LinkReference, by marked instruction) and each label's first
//   record, where the emission defines the label's symbol;
// - optional source events (-g only; none otherwise, so a compile without -g pays for none): `source_count` events, event
//   e at the RIGHT side of record boundary source_records[e] (nondecreasing), whose origin is source_origins[e], an
//   index into the body's typed entries (the lowering's entry tokens), or event e itself where source_origins is null
//   (Tier 0's: an event per typed entry), or BUSTER_EMISSION_NO_ORIGIN for generated code, which the rows give the
//   declaration's line (c_check_debug.c); and one more boundary, source_records[source_count], where the epilogue's
//   closing row starts (the record count for none). The declaration's row at (0, LEFT) is the consumer's, which knows
//   the declaration;
// - frame events (emission_metadata.c's EmissionFrameEvent, in the x86-64 unwind-register namespace): the CFA's and the
//   saved registers' rule changes at record boundaries, in order, from the CIE's initial rules (the CFA rsp + 8, the
//   return address at CFA - 8, the six callee-saved registers their own), with remembered and restored states around
//   early returns and tail calls. The emission resolves them through the final layout after branch sizing
//   (emitted_unwind: the records' starts and ends, which the protocol checks), and writes the fixed-frame CFI where
//   they spell a fixed shape and a row program otherwise (unwind.c), the same bytes either way for a fixed shape.
//
// emitted_verify_frames proves the frame events against the records and their control flow (with --verify, and in the
// tests): see its comment. Producers' own checks run at their production; a frame a producer can't describe is
// EMITTED_FRAME_UNDESCRIBED, which the emission reports as the backend's failure at the function.

#define BUSTER_EMITTED_UNKNOWN INT32_MIN
#define BUSTER_EMITTED_NONE 0xffu
// The registers whose entry values the verifier follows: the general ones, and the return address's slot (16).
#define BUSTER_EMITTED_TRACKED 17u
// The deepest remembered state the verifier follows, as the writer does (BUSTER_UNWIND_ROW_STATES).
#define BUSTER_EMITTED_STATES BUSTER_UNWIND_ROW_STATES

typedef enum EmittedFrameStatus
{
    // No frame events were asked for (a compile without unwind tables).
    EMITTED_FRAME_NONE,
    // The events describe the frame: none for the CIE's rules throughout.
    EMITTED_FRAME_DESCRIBED,
    // The producer's frame is one its events can't describe, or isn't the frame it published.
    EMITTED_FRAME_UNDESCRIBED,
    // No room for the events.
    EMITTED_FRAME_MEMORY,
} EmittedFrameStatus;

typedef struct EmittedCode EmittedCode;
struct EmittedCode
{
    X86Instruction* records;
    uint32_t count;
    uint32_t* branches;
    uint32_t* targets;
    uint32_t branch_count;
    LinkReference* references;
    uint32_t reference_count;
    uint32_t* label_records;
    uint32_t label_count;
    const uint32_t* source_records;
    const uint32_t* source_origins;
    uint32_t source_count;
    const EmissionFrameEvent* frames;
    uint32_t frame_count;
    uint8_t frame_status;
    // An assembly template's code (a naked function's, an asm statement's helper's): its frame events, if any, are the
    // frame the compiler built around the templates, whose own instructions the verifier doesn't read.
    bool assembly;
    // The producer's failure (Tier 0's Tier0Error, which the emission reports; 0 for none) and its node.
    uint32_t error;
    uint32_t node;
    // Tier 0's entries it deferred, for the emission's statistics.
    uint32_t deferred;
};

typedef enum EmittedVerifyError
{
    EMITTED_VERIFY_OK,
    // The events break the protocol (an anchor out of order or past the code, an unknown kind or register, a state
    // restored that wasn't remembered).
    EMITTED_VERIFY_INVALID,
    // A rule the code doesn't establish where it holds: the CFA's register isn't the CFA less the rule's offset, or a
    // saved register isn't where its rule says.
    EMITTED_VERIFY_REFUTED,
    // States remembered deeper than the verifier follows.
    EMITTED_VERIFY_UNSUPPORTED,
    // A rule the verifier can't establish where a record of a form x86_semantics.c doesn't cover is on a path that
    // reaches it: that form's conservative record may write any register, rsp among them, and any byte, so the rule is
    // neither proved nor refuted (as Tier 0's analyzer leaves such code, UNWIND_ERROR_UNSUPPORTED). An x86 builtin's
    // leaf of such a form (monitorx's) is one.
    EMITTED_VERIFY_UNPROVEN,
    EMITTED_VERIFY_MEMORY,
} EmittedVerifyError;

typedef struct EmittedVerdict EmittedVerdict;
struct EmittedVerdict
{
    EmittedVerifyError error;
    // The record where the rules first fail, and the last event applied before it (frame_count for none).
    uint32_t record;
    uint32_t event;
};

static __attribute__((unused)) EmittedCode emitted_from_tier0(const Tier0Code* code, bool frames, bool sources, bool assembly, Arena* arena);
static __attribute__((unused)) UnwindError emitted_unwind(const EmittedCode* code, const uint32_t* offsets, uint32_t length, bool scalar, EmissionFrameRow* rows,
                                                          UnwindFunction* output);
static __attribute__((unused)) bool emitted_unwind_same(const UnwindFunction* a, const UnwindFunction* b, Arena* scratch);
static __attribute__((unused)) EmittedVerdict emitted_verify_frames(const EmittedCode* code, Arena* scratch);

// Whether a fixed frame's largest CFA offset, its pushes' and its allocation's, is one the events' signed 32-bit values
// carry: a frame within 56 bytes of 2 GB isn't, which the fixed descriptor alone could describe.
static bool emitted_cfa_fits(uint32_t saved, uint32_t frame_size)
{
    return 8 + 8 * (uint64_t)__builtin_popcount(saved & 63) + frame_size <= INT32_MAX;
}

// The rows of a fixed descriptor at most: the prologue's (2 per push and rbp's pointing, 15), the epilogue's (2 per
// pop, 16) and each tail's (its bracket and the epilogue's, 18).
static uint64_t emitted_tier0_frame_room(const Tier0Code* code)
{
    return 31 + 18 * (uint64_t)code->tail_count;
}

// Tier 0's code as the emission takes it: its records, columns, references and labels as they are; with `sources` its
// entries' first records as source events (an event per typed entry, the epilogue's first record after them); and with
// `frames` its frame's events, from the frame it published (Tier0Code.frame_*, tail_records) and the records that make
// it, which unwind_tier0_frame_tails checks are the frame's: its transitions at their records' boundaries, in the
// fixed writer's order (unwind_function_rows), from `arena`. A frame that isn't the published one is
// EMITTED_FRAME_UNDESCRIBED; no room, EMITTED_FRAME_MEMORY, with nothing left in `arena`.
static EmittedCode emitted_from_tier0(const Tier0Code* code, bool frames, bool sources, bool assembly, Arena* arena)
{
    EmittedCode result = {
        .records = code->records,
        .count = code->count,
        .branches = code->branches,
        .targets = code->targets,
        .branch_count = code->branch_count,
        .references = code->references,
        .reference_count = code->reference_count,
        .label_records = code->label_records,
        .label_count = code->label_count,
        .assembly = assembly,
        .error = (uint32_t)code->error,
        .node = code->node,
        .deferred = code->deferred,
    };
    if (code->error == TIER0_OK && sources)
    {
        result.source_records = code->entry_records;
        result.source_count = code->entry_count;
    }
    if (code->error == TIER0_OK && frames)
    {
        const uint64_t room = emitted_tier0_frame_room(code);
        EmissionFrameEvent* events = BUSTER_ARENA_PUSH_ARRAY(arena, EmissionFrameEvent, room);
        const uint64_t mark = arena->length;
        const uint64_t tail_room = code->tail_count != 0 ? unwind_tier0_tail_room(code) : 0;
        uint32_t* tails = tail_room != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, tail_room) : 0;
        EmissionFrameRow* rows = BUSTER_ARENA_PUSH_ARRAY(arena, EmissionFrameRow, room);
        result.frame_status = events != 0 && rows != 0 && (tail_room == 0 || tails != 0) ? EMITTED_FRAME_DESCRIBED : EMITTED_FRAME_MEMORY;
        UnwindFunction boundaries;
        // Each transition at its record's boundary: unwind_tier0_frame_tails without final offsets. A CFA past the
        // events' offsets (emitted_cfa_fits) is one no event describes.
        if (result.frame_status == EMITTED_FRAME_DESCRIBED &&
            (unwind_tier0_frame_tails(code, 0, code->count, &boundaries, tails) != UNWIND_OK || !emitted_cfa_fits(boundaries.saved, boundaries.frame_size)))
        {
            result.frame_status = EMITTED_FRAME_UNDESCRIBED;
        }
        const uint32_t count = result.frame_status == EMITTED_FRAME_DESCRIBED ? unwind_function_rows(&boundaries, rows, (uint32_t)room) : 0;
        result.frame_status = count <= room ? result.frame_status : EMITTED_FRAME_UNDESCRIBED;
        for (uint32_t i = 0; result.frame_status == EMITTED_FRAME_DESCRIBED && i < count; i += 1)
        {
            events[i] = (EmissionFrameEvent){
                .at = { .boundary = rows[i].offset, .side = EMISSION_LEFT },
                .value = rows[i].value,
                .reg = rows[i].reg,
                .kind = rows[i].kind,
            };
        }
        arena_pop_to(arena, mark);
        if (result.frame_status == EMITTED_FRAME_DESCRIBED)
        {
            result.frames = count != 0 ? events : 0;
            result.frame_count = count;
        }
    }
    return result;
}

// A function's unwind descriptor from its frame events after branch sizing: `offsets` has each record's start and
// offsets[count] the code's length, `length`, so that record r's end is offsets[r + 1]; resolved through that layout
// (emission_metadata_resolve, which checks the protocol and the layout), put in canonical form (unwind_rows_canonical)
// and lowered (unwind_rows_function) to the fixed descriptor where they spell its simple shape, and otherwise to a row
// program over the rows, in `rows` (frame_count of them), which must outlive the descriptor. UNWIND_ERROR_INVALID for
// events the protocol refuses, UNWIND_ERROR_UNSUPPORTED for rows the writer can't encode and UNWIND_ERROR_LIMIT past the
// format's limits, leaving `output` alone.
static UnwindError emitted_unwind(const EmittedCode* code, const uint32_t* offsets, uint32_t length, bool scalar, EmissionFrameRow* rows, UnwindFunction* output)
{
    const EmissionLayout layout = {
        .starts = offsets,
        .ends = offsets + 1,
        .count = code->count,
        .size = length,
    };
    const EmissionMetadata metadata = {
        .frames = code->frames,
        .frame_count = code->frame_count,
        .register_count = BUSTER_EMISSION_X86_REGISTERS,
    };
    UnwindError error = length == 0 || (code->frame_count != 0 && rows == 0) ? UNWIND_ERROR_INVALID : length > INT32_MAX ? UNWIND_ERROR_LIMIT : UNWIND_OK;
    if (error == UNWIND_OK && code->frame_count != 0)
    {
        const EmissionStorage storage = {
            .frames = rows,
            .frame_capacity = code->frame_count,
        };
        EmissionResolved resolved;
        const EmissionCheck check = emission_metadata_resolve(&metadata, &layout, &storage, scalar, &resolved);
        error = check.error == EMISSION_OK ? UNWIND_OK : UNWIND_ERROR_INVALID;
    }
    else if (error == UNWIND_OK)
    {
        // No events: the layout alone, which the frameless descriptor needs nothing of but its length.
        error = layout.count == 0 || offsets[layout.count] == length ? UNWIND_OK : UNWIND_ERROR_INVALID;
    }
    const uint32_t count = error == UNWIND_OK && code->frame_count != 0 ? unwind_rows_canonical(rows, code->frame_count, length) : 0;
    UnwindFunction built;
    error = error == UNWIND_OK ? unwind_rows_function(rows, count, length, &built) : error;
    if (error == UNWIND_OK)
    {
        *output = built;
    }
    return error;
}

// A descriptor's rows: a row program's, or a fixed shape's (unwind_function_rows), from `scratch`; null without room.
static const EmissionFrameRow* emitted_descriptor_rows(const UnwindFunction* function, Arena* scratch, uint32_t* count)
{
    const EmissionFrameRow* rows = function->rows;
    *count = function->row_count;
    if (function->open != BUSTER_UNWIND_ROWS)
    {
        const uint32_t needed = unwind_function_rows(function, 0, 0);
        EmissionFrameRow* written = needed != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, EmissionFrameRow, needed) : 0;
        *count = written != 0 || needed == 0 ? unwind_function_rows(function, written, needed) : UINT32_MAX;
        rows = written;
    }
    return rows;
}

// Whether two descriptors are the same CFI: the same size and the same rows in order, a remembered state's offset aside
// (the writers take none for it: unwind_rows_scalar). Leaves `scratch` as it was.
static bool emitted_unwind_same(const UnwindFunction* a, const UnwindFunction* b, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    uint32_t a_count = 0;
    uint32_t b_count = 0;
    const EmissionFrameRow* a_rows = emitted_descriptor_rows(a, scratch, &a_count);
    const EmissionFrameRow* b_rows = emitted_descriptor_rows(b, scratch, &b_count);
    bool same = a->size == b->size && a_count == b_count && a_count != UINT32_MAX;
    for (uint32_t i = 0; same && i < a_count; i += 1)
    {
        const EmissionFrameRow x = a_rows[i];
        const EmissionFrameRow y = b_rows[i];
        same = x.kind == y.kind && x.reg == y.reg && x.value == y.value && (x.offset == y.offset || x.kind == EMISSION_FRAME_REMEMBER_STATE);
    }
    arena_pop_to(scratch, mark);
    return same;
}

// ---------------------------------------------------------------------------------------------------------------------
// The frame verifier: events against the records and their control flow

// What the verifier knows at a point of the code, beside the rules: each general register's value as the CFA plus an
// offset (BUSTER_EMITTED_UNKNOWN where it isn't one), which register's value at entry each holds (BUSTER_EMITTED_NONE
// for none), and where each register's entry value is saved in the frame, as an offset from the CFA (the return
// address's, 16, at CFA - 8 from the entry). Registers by the encoder's numbering. `unproven`: a record of a form
// x86_semantics.c doesn't cover is on a path that reaches the point (EMITTED_VERIFY_UNPROVEN).
typedef struct EmittedState EmittedState;
struct EmittedState
{
    int32_t value[16];
    uint8_t holds[16];
    int32_t slots[BUSTER_EMITTED_TRACKED];
    bool reached;
    bool unproven;
};

// The rules the events give at a point: the CFA's register (DWARF) and offset, and each register's rule.
typedef enum EmittedRuleKind
{
    // Nothing checked: a caller-saved register's (the CIE has none), or undefined.
    EMITTED_RULE_UNCHECKED,
    EMITTED_RULE_SAME,
    EMITTED_RULE_OFFSET,
    EMITTED_RULE_REGISTER,
} EmittedRuleKind;

typedef struct EmittedRules EmittedRules;
struct EmittedRules
{
    uint32_t cfa_register;
    int32_t cfa_offset;
    uint8_t kinds[BUSTER_EMITTED_TRACKED];
    int32_t values[BUSTER_EMITTED_TRACKED];
};

static uint32_t emitted_dwarf_gpr(uint32_t dwarf)
{
    uint32_t gpr = 16;
    for (uint32_t g = 0; g < 16; g += 1)
    {
        gpr = emission_x86_registers[g] == dwarf ? g : gpr;
    }
    return gpr;
}

// The CIE's rules: CFA rsp + 8, the return address at CFA - 8, the callee-saved registers their own.
static EmittedRules emitted_initial_rules(void)
{
    EmittedRules rules = { .cfa_register = BUSTER_EMISSION_X86_RSP, .cfa_offset = 8 };
    rules.kinds[BUSTER_EMISSION_X86_RETURN] = EMITTED_RULE_OFFSET;
    rules.values[BUSTER_EMISSION_X86_RETURN] = -8;
    for (uint32_t g = 0; g < 16; g += 1)
    {
        rules.kinds[emission_x86_registers[g]] = ((BUSTER_ABI_CALLEE_SAVED >> g) & 1) != 0 && g != X86_REGISTER_RSP ? EMITTED_RULE_SAME : EMITTED_RULE_UNCHECKED;
    }
    return rules;
}

static EmittedState emitted_entry_state(void)
{
    EmittedState state = { .reached = true };
    for (uint32_t g = 0; g < 16; g += 1)
    {
        state.value[g] = BUSTER_EMITTED_UNKNOWN;
        state.holds[g] = (uint8_t)g;
    }
    for (uint32_t h = 0; h < BUSTER_EMITTED_TRACKED; h += 1)
    {
        state.slots[h] = BUSTER_EMITTED_UNKNOWN;
    }
    state.value[X86_REGISTER_RSP] = -8;
    state.slots[16] = -8;
    return state;
}

// The meet of two states (what holds on both paths); true if `into` changed.
static bool emitted_meet(EmittedState* into, const EmittedState* from)
{
    bool changed = false;
    if (!into->reached)
    {
        *into = *from;
        changed = from->reached;
    }
    else if (from->reached)
    {
        for (uint32_t g = 0; g < 16; g += 1)
        {
            const bool value = into->value[g] != from->value[g] && into->value[g] != BUSTER_EMITTED_UNKNOWN;
            const bool holds = into->holds[g] != from->holds[g] && into->holds[g] != BUSTER_EMITTED_NONE;
            into->value[g] = value ? BUSTER_EMITTED_UNKNOWN : into->value[g];
            into->holds[g] = holds ? BUSTER_EMITTED_NONE : into->holds[g];
            changed |= value || holds;
        }
        for (uint32_t h = 0; h < BUSTER_EMITTED_TRACKED; h += 1)
        {
            const bool slot = into->slots[h] != from->slots[h] && into->slots[h] != BUSTER_EMITTED_UNKNOWN;
            into->slots[h] = slot ? BUSTER_EMITTED_UNKNOWN : into->slots[h];
            changed |= slot;
        }
        changed |= from->unproven && !into->unproven;
        into->unproven |= from->unproven;
    }
    return changed;
}

static int32_t emitted_add(int32_t value, int64_t amount)
{
    const int64_t sum = (int64_t)value + amount;
    return value == BUSTER_EMITTED_UNKNOWN || sum <= INT32_MIN || sum > INT32_MAX ? BUSTER_EMITTED_UNKNOWN : (int32_t)sum;
}

// A store of `bytes` at CFA + `at` overwrites the slots it overlaps.
static void emitted_clobber_slots(EmittedState* state, int32_t at, uint32_t bytes)
{
    for (uint32_t h = 0; at != BUSTER_EMITTED_UNKNOWN && h < BUSTER_EMITTED_TRACKED; h += 1)
    {
        const int64_t slot = state->slots[h];
        state->slots[h] = slot != BUSTER_EMITTED_UNKNOWN && slot < (int64_t)at + bytes && (int64_t)at < slot + 8 ? BUSTER_EMITTED_UNKNOWN : state->slots[h];
    }
}

// Slots below rsp aren't the frame's: a signal handler's frame, or a call's return address, may overwrite them.
static void emitted_release_slots(EmittedState* state)
{
    const int32_t rsp = state->value[X86_REGISTER_RSP];
    for (uint32_t h = 0; rsp != BUSTER_EMITTED_UNKNOWN && h < BUSTER_EMITTED_TRACKED; h += 1)
    {
        state->slots[h] = state->slots[h] != BUSTER_EMITTED_UNKNOWN && state->slots[h] < rsp ? BUSTER_EMITTED_UNKNOWN : state->slots[h];
    }
}

static void emitted_forget(EmittedState* state, uint32_t g)
{
    state->value[g] = BUSTER_EMITTED_UNKNOWN;
    state->holds[g] = BUSTER_EMITTED_NONE;
}

static bool emitted_direct(const X86Instruction* record)
{
    return record->mode == BUSTER_X86_REGISTER_DIRECT;
}

static bool emitted_jump(uint32_t form)
{
    form &= ~BUSTER_X86_FORM_FIXUP;
    return form == X86_FORM_JMP_REL8 || form == X86_FORM_JMP_REL32;
}

// Whether a record ends its path: a ret, a trap, or a jump out of the function (a tail call: a direct jmp with a
// reference, or Tier 0's and Tier 1's jmp through r11).
static bool emitted_exit(const X86Instruction* record, bool branch)
{
    const uint32_t form = record->form;
    return form == X86_FORM_RET || form == X86_FORM_UD2 || form == X86_FORM_INT3 || (!branch && form == (X86_FORM_JMP_REL32 | BUSTER_X86_FORM_FIXUP)) ||
           (form == X86_FORM_JMP_RM64 && emitted_direct(record) && record->rm == X86_REGISTER_R11);
}

// An indirect jump within the function (a computed goto's, a jump table's): it may go to any of its labels.
static bool emitted_indirect(const X86Instruction* record)
{
    return record->form == X86_FORM_JMP_RM64 && !(emitted_direct(record) && record->rm == X86_REGISTER_R11);
}

// A record's effect on the state, as far as the rules depend on it: pushes and pops, moves, additions, subtractions and
// realignments of the stack and frame registers, loads' effective addresses, calls (which preserve the callee-saved
// registers and rsp, and leave the others unknown), stores into the frame (which overwrite the slots they reach), and
// any other write of a register (its value unknown), by x86_semantics.c's resolved effects. A store through a register
// the verifier doesn't follow is trusted not to reach the save area (the frame's own layout keeps it apart). A form
// x86_semantics.c doesn't cover writes every register and slot, and marks the state unproven.
static void emitted_transfer(const X86Instruction* record, EmittedState* state)
{
    const uint32_t form = record->form & ~BUSTER_X86_FORM_FIXUP;
    const uint32_t rsp = X86_REGISTER_RSP;
    const bool direct = emitted_direct(record);
    if (form == X86_FORM_PUSH_R64 && record->rm < 16)
    {
        state->value[rsp] = emitted_add(state->value[rsp], -8);
        emitted_clobber_slots(state, state->value[rsp], 8);
        const uint32_t held = state->holds[record->rm];
        if (held != BUSTER_EMITTED_NONE && state->value[rsp] != BUSTER_EMITTED_UNKNOWN)
        {
            state->slots[held] = state->value[rsp];
        }
    }
    else if (form == X86_FORM_POP_R64 && record->rm < 16)
    {
        const int32_t at = state->value[rsp];
        uint8_t held = BUSTER_EMITTED_NONE;
        for (uint32_t h = 0; at != BUSTER_EMITTED_UNKNOWN && h < 16; h += 1)
        {
            held = state->slots[h] == at ? (uint8_t)h : held;
        }
        emitted_forget(state, record->rm);
        state->holds[record->rm] = held;
        state->value[rsp] = record->rm == rsp ? BUSTER_EMITTED_UNKNOWN : emitted_add(at, 8);
        emitted_release_slots(state);
    }
    else if ((form == X86_FORM_MOV_RM64_R64 || form == X86_FORM_MOV_R64_RM64) && direct)
    {
        const uint32_t target = form == X86_FORM_MOV_RM64_R64 ? record->rm : record->reg;
        const uint32_t source = form == X86_FORM_MOV_RM64_R64 ? record->reg : record->rm;
        state->value[target] = state->value[source];
        state->holds[target] = state->holds[source];
        emitted_release_slots(state);
    }
    else if (form == X86_FORM_LEA_R64_M && record->mode == BUSTER_X86_MEMORY_BASE)
    {
        const int32_t base = state->value[record->rm];
        emitted_forget(state, record->reg);
        state->value[record->reg] = emitted_add(base, record->displacement);
        emitted_release_slots(state);
    }
    else if ((form == X86_FORM_SUB_RM64_IMM8 || form == X86_FORM_SUB_RM64_IMM32 || form == X86_FORM_ADD_RM64_IMM8 || form == X86_FORM_ADD_RM64_IMM32) && direct)
    {
        const int64_t amount = form == X86_FORM_SUB_RM64_IMM8 || form == X86_FORM_SUB_RM64_IMM32 ? -(int64_t)record->immediate : (int64_t)record->immediate;
        const int32_t value = state->value[record->rm];
        emitted_forget(state, record->rm);
        state->value[record->rm] = emitted_add(value, amount);
        emitted_release_slots(state);
    }
    else if (form == X86_FORM_CALL_REL32 || form == X86_FORM_CALL_RM64)
    {
        // The callee returns with rsp and the callee-saved registers as they were, the others its own.
        for (uint32_t g = 0; g < 16; g += 1)
        {
            if (((BUSTER_ABI_CALLEE_SAVED >> g) & 1) == 0 && g != rsp)
            {
                emitted_forget(state, g);
            }
        }
    }
    else
    {
        uint64_t reads = 0;
        uint64_t writes = 0;
        // With the opmask column, so that a record of opmask registers (AVX-512's masks) writes only what it writes.
        uint16_t opmasks = 0;
        x86_instruction_resolve(record, &reads, &writes, &opmasks);
        const X86Effects* effects = x86_effects(record->form);
        state->unproven |= effects->operation == X86_OPERATION_NONE;
        for (uint32_t g = 0; g < 16; g += 1)
        {
            if (((writes >> g) & 1) != 0)
            {
                emitted_forget(state, g);
            }
        }
        if ((writes & BUSTER_X86_RESOURCE_MEMORY) != 0)
        {
            const bool frame_store = !direct && (record->mode == BUSTER_X86_MEMORY_BASE) && record->rm < 16;
            if (frame_store)
            {
                const uint32_t bytes = effects->width != 0 ? effects->width : 64;
                emitted_clobber_slots(state, emitted_add(state->value[record->rm], record->displacement), bytes);
            }
            else if (effects->operation == X86_OPERATION_NONE)
            {
                // An uncovered form may write anywhere.
                for (uint32_t h = 0; h < BUSTER_EMITTED_TRACKED; h += 1)
                {
                    state->slots[h] = BUSTER_EMITTED_UNKNOWN;
                }
            }
        }
        emitted_release_slots(state);
    }
}

// The equality a compare and the conditional jump right after it establish on one of the jump's edges: `cmp a, b`
// (registers) then jne falls through, or je jumps, where a equals b, the probe loop's exit (#1056). Returns false for an
// edge no path takes: both values known and apart (the loop's first pass, before the back edge's meet).
static bool emitted_refine(const X86Instruction* records, uint32_t jump, bool taken, EmittedState* state)
{
    const uint32_t form = records[jump].form & ~BUSTER_X86_FORM_FIXUP;
    const bool equal = taken ? form == X86_FORM_JE_REL8 || form == X86_FORM_JE_REL32 : form == X86_FORM_JNE_REL8 || form == X86_FORM_JNE_REL32;
    const X86Instruction* compare = jump > 0 ? &records[jump - 1] : 0;
    bool feasible = true;
    if (equal && compare != 0 && compare->form == X86_FORM_CMP_RM64_R64 && emitted_direct(compare))
    {
        const uint32_t a = compare->rm;
        const uint32_t b = compare->reg;
        feasible = state->value[a] == BUSTER_EMITTED_UNKNOWN || state->value[b] == BUSTER_EMITTED_UNKNOWN || state->value[a] == state->value[b];
        state->value[a] = state->value[a] == BUSTER_EMITTED_UNKNOWN ? state->value[b] : state->value[a];
        state->value[b] = state->value[b] == BUSTER_EMITTED_UNKNOWN ? state->value[a] : state->value[b];
    }
    return feasible;
}

// Applies an event to the rules, with its remembered states. Returns false for an event the protocol refuses.
static bool emitted_apply(const EmissionFrameEvent* event, EmittedRules* rules, EmittedRules* states, uint32_t* depth, bool* deep)
{
    const EmittedRules initial = emitted_initial_rules();
    const uint32_t reg = event->reg;
    bool valid = event->reserved == 0;
    switch (event->kind)
    {
        case EMISSION_FRAME_CFA:
        {
            valid &= reg < 16 && event->value >= 0;
            rules->cfa_register = reg;
            rules->cfa_offset = event->value;
        } break;
        case EMISSION_FRAME_CFA_OFFSET:
        {
            valid &= reg == 0 && event->value >= 0;
            rules->cfa_offset = event->value;
        } break;
        case EMISSION_FRAME_OFFSET:
        case EMISSION_FRAME_REGISTER:
        case EMISSION_FRAME_RESTORE:
        case EMISSION_FRAME_SAME_VALUE:
        case EMISSION_FRAME_UNDEFINED:
        {
            valid &= reg < BUSTER_EMITTED_TRACKED && reg != BUSTER_EMISSION_X86_RSP;
            const uint32_t r = valid ? reg : 0;
            rules->kinds[r] = event->kind == EMISSION_FRAME_OFFSET ? EMITTED_RULE_OFFSET : event->kind == EMISSION_FRAME_REGISTER ? EMITTED_RULE_REGISTER :
                              event->kind == EMISSION_FRAME_SAME_VALUE ? EMITTED_RULE_SAME : event->kind == EMISSION_FRAME_RESTORE ? initial.kinds[r] :
                                                                                                                              EMITTED_RULE_UNCHECKED;
            rules->values[r] = event->kind == EMISSION_FRAME_RESTORE ? initial.values[r] : event->value;
            valid &= event->kind != EMISSION_FRAME_REGISTER || (event->value >= 0 && event->value < 16);
        } break;
        case EMISSION_FRAME_REMEMBER_STATE:
        {
            *deep |= *depth >= BUSTER_EMITTED_STATES;
            if (*depth < BUSTER_EMITTED_STATES)
            {
                states[*depth] = *rules;
            }
            *depth += 1;
        } break;
        case EMISSION_FRAME_RESTORE_STATE:
        {
            valid &= *depth != 0;
            *depth -= *depth != 0 ? 1 : 0;
            if (valid && *depth < BUSTER_EMITTED_STATES)
            {
                *rules = states[*depth];
            }
        } break;
        default:
        {
            valid = false;
        } break;
    }
    return valid;
}

// Whether the state establishes the rules: the CFA's register is the CFA less its offset, each callee-saved register
// the rules keep holds its entry value, each saved one is at its slot (the return address's too) or in its register.
static bool emitted_holds(const EmittedRules* rules, const EmittedState* state)
{
    const uint32_t cfa = emitted_dwarf_gpr(rules->cfa_register);
    bool valid = cfa < 16 && state->value[cfa] != BUSTER_EMITTED_UNKNOWN && (int64_t)state->value[cfa] + rules->cfa_offset == 0;
    for (uint32_t d = 0; valid && d < BUSTER_EMITTED_TRACKED; d += 1)
    {
        const uint32_t g = d < 16 ? emitted_dwarf_gpr(d) : 16;
        switch (rules->kinds[d])
        {
            case EMITTED_RULE_SAME:
            {
                valid = g < 16 && state->holds[g] == g;
            } break;
            case EMITTED_RULE_OFFSET:
            {
                valid = state->slots[g] == rules->values[d];
            } break;
            case EMITTED_RULE_REGISTER:
            {
                const uint32_t holder = emitted_dwarf_gpr((uint32_t)rules->values[d]);
                valid = holder < 16 && state->holds[holder] == g;
            } break;
            default:
            {
            } break;
        }
    }
    return valid;
}

// Proves a function's frame events against its records and their control flow, as an unwinder reads them: at every
// record the code reaches, the rules in force there (the events anchored at or before its start, in order, from the
// CIE's) must hold of every path that reaches it. A forward dataflow over the code's blocks (from its entry, each
// branch to its target and on, each indirect jump to every label, a ret, a trap or a tail call's jump ending its path)
// follows each register's value as the CFA plus an offset, which register's entry value each holds and where in the
// frame each entry value is saved (EmittedState), to its fixpoint, the paths' meet at each block; then each reached
// record is checked against the rules (emitted_holds); a rule that fails on a path through a form x86_semantics.c
// doesn't cover is unproven rather than refuted. Code nothing reaches isn't checked. Events at a boundary's two sides
// both hold from the record after it: a layout's padding between them executes nothing. An assembly template's code
// isn't the compiler's: its caller doesn't verify it. Scalar: this runs with --verify and in the tests, off the
// compile's path. Its memory, the leaders' states and two words per record, from `scratch`, is given back.
static EmittedVerdict emitted_verify_frames(const EmittedCode* code, Arena* scratch)
{
    EmittedVerdict verdict = { .error = EMITTED_VERIFY_OK, .event = code->frame_count };
    const uint32_t count = code->count;
    const uint64_t mark = scratch->length;
    // Each record's branch target (UINT32_MAX for none), and the leaders: the blocks' first records, each's state.
    uint32_t* targets = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    uint32_t* leaders = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    verdict.error = count != 0 && (targets == 0 || leaders == 0) ? EMITTED_VERIFY_MEMORY : verdict.error;
    for (uint32_t i = 0; verdict.error == EMITTED_VERIFY_OK && i < count; i += 1)
    {
        targets[i] = UINT32_MAX;
        leaders[i] = UINT32_MAX;
    }
    for (uint32_t k = 0; verdict.error == EMITTED_VERIFY_OK && k < code->branch_count; k += 1)
    {
        const uint32_t record = code->branches[k];
        const uint32_t target = code->targets[k];
        verdict.error = record < count && target < count ? verdict.error : EMITTED_VERIFY_INVALID;
        if (verdict.error == EMITTED_VERIFY_OK)
        {
            targets[record] = target;
            leaders[target] = 0;
            if (record + 1 < count)
            {
                leaders[record + 1] = 0;
            }
        }
    }
    for (uint32_t k = 0; verdict.error == EMITTED_VERIFY_OK && k < code->label_count; k += 1)
    {
        if (code->label_records[k] < count)
        {
            leaders[code->label_records[k]] = 0;
        }
    }
    for (uint32_t i = 0; verdict.error == EMITTED_VERIFY_OK && i + 1 < count; i += 1)
    {
        const X86Instruction* record = &code->records[i];
        if (emitted_exit(record, targets[i] != UINT32_MAX) || emitted_indirect(record))
        {
            leaders[i + 1] = 0;
        }
    }
    uint32_t leader_count = 0;
    if (verdict.error == EMITTED_VERIFY_OK && count != 0)
    {
        leaders[0] = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            leaders[i] = leaders[i] != UINT32_MAX ? leader_count++ : UINT32_MAX;
        }
    }
    EmittedState* states = leader_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, EmittedState, leader_count) : 0;
    uint32_t* starts = leader_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, leader_count) : 0;
    uint32_t* work = leader_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, leader_count) : 0;
    uint8_t* queued = leader_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, leader_count) : 0;
    verdict.error = leader_count != 0 && (states == 0 || starts == 0 || work == 0 || queued == 0) ? EMITTED_VERIFY_MEMORY : verdict.error;
    uint32_t pending = 0;
    if (verdict.error == EMITTED_VERIFY_OK && leader_count != 0)
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (leaders[i] != UINT32_MAX)
            {
                starts[leaders[i]] = i;
                states[leaders[i]] = (EmittedState){ .reached = false };
                queued[leaders[i]] = 0;
            }
        }
        states[0] = emitted_entry_state();
        work[pending++] = 0;
        queued[0] = 1;
    }
    // The fixpoint: each block from its state to its end, each successor's state met with what reaches it.
    while (verdict.error == EMITTED_VERIFY_OK && pending != 0)
    {
        pending -= 1;
        const uint32_t leader = work[pending];
        queued[leader] = 0;
        EmittedState state = states[leader];
        for (uint32_t i = starts[leader]; i < count; i += 1)
        {
            const X86Instruction* record = &code->records[i];
            const bool branch = targets[i] != UINT32_MAX;
            emitted_transfer(record, &state);
            uint32_t successors[2];
            EmittedState edges[2];
            uint32_t successor_count = 0;
            bool stop = emitted_exit(record, branch);
            if (branch)
            {
                edges[successor_count] = state;
                const bool taken = emitted_refine(code->records, i, true, &edges[successor_count]);
                successors[successor_count] = leaders[targets[i]];
                successor_count += taken ? 1 : 0;
                stop = emitted_jump(record->form) || !emitted_refine(code->records, i, false, &state);
            }
            if (!stop && emitted_indirect(record))
            {
                for (uint32_t k = 0; k < code->label_count; k += 1)
                {
                    const uint32_t to = code->label_records[k] < count ? leaders[code->label_records[k]] : UINT32_MAX;
                    if (to != UINT32_MAX && emitted_meet(&states[to], &state) && !queued[to])
                    {
                        queued[to] = 1;
                        work[pending++] = to;
                    }
                }
                stop = true;
            }
            if (!stop && i + 1 < count && leaders[i + 1] != UINT32_MAX)
            {
                edges[successor_count] = state;
                successors[successor_count++] = leaders[i + 1];
                stop = true;
            }
            for (uint32_t k = 0; k < successor_count; k += 1)
            {
                const uint32_t to = successors[k];
                if (emitted_meet(&states[to], &edges[k]) && !queued[to])
                {
                    queued[to] = 1;
                    work[pending++] = to;
                }
            }
            if (stop)
            {
                break;
            }
        }
    }
    // The check: the records in order, the rules as the events give them, each reached record's state against them.
    EmittedRules rules = emitted_initial_rules();
    EmittedRules remembered[BUSTER_EMITTED_STATES];
    uint32_t depth = 0;
    bool deep = false;
    uint32_t event = 0;
    EmittedState state = { .reached = false };
    EmissionAnchor previous = { 0 };
    // The first rule left unproven, which a refutation anywhere in the code overrides.
    EmittedVerdict unproven = { .error = EMITTED_VERIFY_OK };
    for (uint32_t i = 0; verdict.error == EMITTED_VERIFY_OK && i <= count; i += 1)
    {
        for (; verdict.error == EMITTED_VERIFY_OK && event < code->frame_count && code->frames[event].at.boundary <= i; event += 1)
        {
            const EmissionFrameEvent* applied = &code->frames[event];
            const bool ordered = !emission_anchor_before(applied->at, previous) && applied->at.side <= EMISSION_RIGHT;
            verdict.error = ordered && emitted_apply(applied, &rules, remembered, &depth, &deep) ? verdict.error : EMITTED_VERIFY_INVALID;
            verdict.error = verdict.error == EMITTED_VERIFY_OK && deep ? EMITTED_VERIFY_UNSUPPORTED : verdict.error;
            verdict.record = verdict.error == EMITTED_VERIFY_OK ? verdict.record : i;
            verdict.event = verdict.error == EMITTED_VERIFY_OK ? verdict.event : event;
            previous = applied->at;
        }
        if (verdict.error != EMITTED_VERIFY_OK || i == count)
        {
            break;
        }
        state = leaders[i] != UINT32_MAX ? states[leaders[i]] : state;
        if (state.reached && !emitted_holds(&rules, &state))
        {
            const EmittedVerdict failed = { .error = state.unproven ? EMITTED_VERIFY_UNPROVEN : EMITTED_VERIFY_REFUTED,
                                            .record = i,
                                            .event = event != 0 ? event - 1 : code->frame_count };
            verdict = state.unproven ? verdict : failed;
            unproven = state.unproven && unproven.error == EMITTED_VERIFY_OK ? failed : unproven;
        }
        if (state.reached)
        {
            emitted_transfer(&code->records[i], &state);
            if (targets[i] != UINT32_MAX)
            {
                (void)emitted_refine(code->records, i, false, &state);
            }
        }
    }
    // Events past the code: an anchor beyond its end.
    if (verdict.error == EMITTED_VERIFY_OK && event != code->frame_count)
    {
        verdict = (EmittedVerdict){ .error = EMITTED_VERIFY_INVALID, .record = count, .event = event };
    }
    verdict = verdict.error == EMITTED_VERIFY_OK ? unproven : verdict;
    arena_pop_to(scratch, mark);
    return verdict;
}
