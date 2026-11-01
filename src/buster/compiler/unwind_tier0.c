// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Derive one bounded native CFI descriptor from Tier 0's fixed frame, without changing its producer. Include after
// unwind.c, tier0.c and x86_semantics.c. unwind_tier0 uses batched resolved effects; unwind_tier0_scalar is its twin.
// Both leave the descriptor untouched on failure and allocate nothing.
//
// This is an opt-in consumer of successful, valid baseline Tier0Code, after successful branch sizing. The caller
// supplies count final byte offsets and the sized function length, and freezes the records through encoding. Encoder
// and semantics tables are initialized. These producer contracts replace generic record validation or measuring the
// instructions again; the checks here establish the supported frame shape and its control-flow edges.
//
// Entry and calls use the ordinary SysV called-function convention. Compiler-private save slots and the return slot
// are not corrupted through escaped pointers or callees. Arbitrary pointer aliases are not proved here. Explicit
// RSP-relative stores must still have a fixed, bounded interval disjoint from those slots. Raw entry points, special
// calling conventions, inline assembly and records transformed after sizing are outside this contract.

#define BUSTER_UNWIND_TIER0_BATCH 8

static const uint8_t unwind_tier0_saved_registers[] = {
    X86_REGISTER_RBX,
    X86_REGISTER_RBP,
    X86_REGISTER_R12,
    X86_REGISTER_R13,
    X86_REGISTER_R14,
    X86_REGISTER_R15,
};

// Only the unprefixed direct branches that Tier 0 lists for branch sizing. A condition's adjacent catalogue forms
// also include SETcc and CMOV, so a numerical range over the condition forms would not prove this property.
static bool unwind_tier0_branch(uint32_t form)
{
    bool result = true;
    switch (form)
    {
        case X86_FORM_JMP_REL8:
        case X86_FORM_JMP_REL32:
        case X86_FORM_JO_REL8:
        case X86_FORM_JO_REL32:
        case X86_FORM_JNO_REL8:
        case X86_FORM_JNO_REL32:
        case X86_FORM_JB_REL8:
        case X86_FORM_JB_REL32:
        case X86_FORM_JAE_REL8:
        case X86_FORM_JAE_REL32:
        case X86_FORM_JE_REL8:
        case X86_FORM_JE_REL32:
        case X86_FORM_JNE_REL8:
        case X86_FORM_JNE_REL32:
        case X86_FORM_JBE_REL8:
        case X86_FORM_JBE_REL32:
        case X86_FORM_JA_REL8:
        case X86_FORM_JA_REL32:
        case X86_FORM_JS_REL8:
        case X86_FORM_JS_REL32:
        case X86_FORM_JNS_REL8:
        case X86_FORM_JNS_REL32:
        case X86_FORM_JP_REL8:
        case X86_FORM_JP_REL32:
        case X86_FORM_JNP_REL8:
        case X86_FORM_JNP_REL32:
        case X86_FORM_JL_REL8:
        case X86_FORM_JL_REL32:
        case X86_FORM_JGE_REL8:
        case X86_FORM_JGE_REL32:
        case X86_FORM_JLE_REL8:
        case X86_FORM_JLE_REL32:
        case X86_FORM_JG_REL8:
        case X86_FORM_JG_REL32:
            break;
        default:
            result = false;
            break;
    }
    return result;
}

static bool unwind_tier0_adjustment(const X86Instruction* record, bool subtract, uint32_t* amount)
{
    const bool form = subtract ? record->form == X86_FORM_SUB_RM64_IMM8 || record->form == X86_FORM_SUB_RM64_IMM32 :
                                record->form == X86_FORM_ADD_RM64_IMM8 || record->form == X86_FORM_ADD_RM64_IMM32;
    const uint64_t value = form ? x86_immediate_value(record->form, record->immediate, 0) : 0;
    const bool result = form && record->mode == BUSTER_X86_REGISTER_DIRECT && record->rm == X86_REGISTER_RSP && value > 0 && value <= INT32_MAX;
    if (result)
    {
        *amount = (uint32_t)value;
    }
    return result;
}

// Structural bounds only: final_offsets is branch sizing's already finalized column, not a claim about independently
// supplied machine bytes. The last record has no sentinel offset; its end is the explicit function size.
static bool unwind_tier0_offset(const Tier0Code* code, const uint32_t* final_offsets, uint32_t size, uint32_t record)
{
    const uint32_t end = record + 1 < code->count ? final_offsets[record + 1] : size;
    return final_offsets[record] < end && end <= size;
}

// Whether a function's last record ends it: the epilogue's ret, or for a frameless function (no push, reservation or
// frame pointer: the CFA rsp + 8 throughout) a ud2, which never returns, as the terminal builtins' helper is
// (c_builtin_terminal_helpers.c, __builtin_trap's and __builtin_unreachable's, #523).
static bool unwind_tier0_end(const X86Instruction* record, bool frameless)
{
    return record->form == X86_FORM_RET || (frameless && record->form == X86_FORM_UD2);
}

static bool unwind_tier0_store(const X86Instruction* record, const X86Effects* effects, uint32_t frame_size, uint32_t pushes)
{
    const bool memory = (record->mode & BUSTER_X86_MODE_MEMORY) != 0;
    const bool writes = (x86_effects_role(effects, BUSTER_X86_ROLE_RM) & BUSTER_X86_ROLE_WRITE) != 0;
    const bool base = (record->mode & BUSTER_X86_MODE_BASE) != 0 && record->rm == X86_REGISTER_RSP;
    const bool index = (record->mode & BUSTER_X86_MODE_INDEX) != 0 && record->index == X86_REGISTER_RSP;
    bool result = true;
    if (memory && writes && (base || index))
    {
        // A symbolic displacement, segment base or indexed bit-string can move the store outside its nominal span.
        const uint32_t bytes = x86_form_description(record->form)->operand_bytes;
        const bool fixed = record->mode == BUSTER_X86_MEMORY_BASE && record->form == x86_semantics_row(record->form) &&
                           (effects->memory & BUSTER_X86_MEMORY_EFFECT_BIT_STRING) == 0 && bytes != 0;
        const int64_t first = record->displacement;
        const int64_t end = first + bytes;
        const int64_t protected_end = (int64_t)frame_size + 8 * (pushes + 1);
        result = fixed && (end <= frame_size || first >= protected_end);
    }
    return result;
}

static UnwindError unwind_tier0_build(const Tier0Code* code, const uint32_t* final_offsets, uint32_t size, UnwindFunction* output, bool exact)
{
    UnwindError error = UNWIND_OK;
    UnwindFunction built = {
        .size = size,
    };
    uint32_t ends[BUSTER_ARRAY_LENGTH(built.offsets)] = { 0 };
    uint32_t begin = 0;
    uint32_t pushes = 0;
    uint32_t next_saved = 0;
    uint64_t saved_registers = 0;
    uint64_t nonvolatile = 0;
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(unwind_tier0_saved_registers); r += 1)
    {
        nonvolatile |= BUSTER_X86_RESOURCE_GPR(unwind_tier0_saved_registers[r]);
    }
    if (code == 0 || final_offsets == 0 || output == 0 || code->error != TIER0_OK || code->records == 0 || code->count == 0 ||
        size == 0 || code->count > size || code->branch_count > code->count ||
        (code->branch_count != 0 && (code->branches == 0 || code->targets == 0)) || final_offsets[0] != 0)
    {
        error = UNWIND_ERROR_INVALID;
    }
    while (error == UNWIND_OK && begin < code->count && code->records[begin].form == X86_FORM_PUSH_R64)
    {
        const X86Instruction* record = &code->records[begin];
        uint32_t save = next_saved;
        while (save < BUSTER_ARRAY_LENGTH(unwind_tier0_saved_registers) && unwind_tier0_saved_registers[save] != record->rm)
        {
            save += 1;
        }
        if (save == BUSTER_ARRAY_LENGTH(unwind_tier0_saved_registers) || record->mode != BUSTER_X86_REGISTER_DIRECT)
        {
            error = UNWIND_ERROR_UNSUPPORTED;
        }
        else
        {
            built.saved |= (uint8_t)(1u << save);
            saved_registers |= BUSTER_X86_RESOURCE_GPR(record->rm);
            next_saved = save + 1;
            pushes += 1;
            begin += 1;
            ends[built.event_count] = begin;
            built.event_count += 1;
        }
    }
    if (error == UNWIND_OK && begin < code->count && unwind_tier0_adjustment(&code->records[begin], true, &built.frame_size))
    {
        begin += 1;
        ends[built.event_count] = begin;
        built.event_count += 1;
    }
    const uint32_t suffix = pushes + (built.frame_size != 0 ? 1 : 0) + 1;
    uint32_t epilogue = 0;
    if (error == UNWIND_OK)
    {
        if (suffix > code->count - begin || !unwind_tier0_end(&code->records[code->count - 1], pushes == 0 && built.frame_size == 0))
        {
            error = UNWIND_ERROR_UNSUPPORTED;
        }
        else
        {
            epilogue = code->count - suffix;
        }
    }
    uint32_t restore = epilogue;
    if (error == UNWIND_OK && built.frame_size != 0)
    {
        uint32_t amount = 0;
        if (!unwind_tier0_adjustment(&code->records[restore], false, &amount) || amount != built.frame_size)
        {
            error = UNWIND_ERROR_UNSUPPORTED;
        }
        else
        {
            restore += 1;
            ends[built.event_count] = restore;
            built.event_count += 1;
        }
    }
    for (uint32_t r = BUSTER_ARRAY_LENGTH(unwind_tier0_saved_registers); error == UNWIND_OK && r > 0; r -= 1)
    {
        if ((built.saved & (1u << (r - 1))) != 0)
        {
            const X86Instruction* record = &code->records[restore];
            if (record->form != X86_FORM_POP_R64 || record->mode != BUSTER_X86_REGISTER_DIRECT || record->rm != unwind_tier0_saved_registers[r - 1])
            {
                error = UNWIND_ERROR_UNSUPPORTED;
            }
            else
            {
                restore += 1;
                ends[built.event_count] = restore;
                built.event_count += 1;
            }
        }
    }
    // Prefix and suffix are bounded by fourteen frame events and the final RET (or UD2). The body's offset checks share
    // its effects pass below, so there is no additional whole-function validation walk.
    for (uint32_t i = 0; error == UNWIND_OK && i < begin; i += 1)
    {
        error = unwind_tier0_offset(code, final_offsets, size, i) ? UNWIND_OK : UNWIND_ERROR_INVALID;
    }
    for (uint32_t i = epilogue; error == UNWIND_OK && i < code->count; i += 1)
    {
        error = unwind_tier0_offset(code, final_offsets, size, i) ? UNWIND_OK : UNWIND_ERROR_INVALID;
    }
    uint32_t branch = 0;
    for (uint32_t first = begin; error == UNWIND_OK && first < epilogue;)
    {
        const uint32_t count = epilogue - first < BUSTER_UNWIND_TIER0_BATCH ? epilogue - first : BUSTER_UNWIND_TIER0_BATCH;
        uint64_t reads[BUSTER_UNWIND_TIER0_BATCH];
        uint64_t writes[BUSTER_UNWIND_TIER0_BATCH];
        if (exact)
        {
            #pragma clang loop vectorize(disable) interleave(disable)
            for (uint32_t lane = 0; lane < count; lane += 1)
            {
                x86_instruction_effects_scalar(&code->records[first + lane], &reads[lane], &writes[lane]);
            }
        }
        else
        {
            x86_instructions_effects(code->records + first, count, reads, writes);
        }
        for (uint32_t lane = 0; error == UNWIND_OK && lane < count; lane += 1)
        {
            const uint32_t i = first + lane;
            const X86Instruction* record = &code->records[i];
            const X86Effects* effects = x86_effects(record->form);
            const uint32_t control = x86_effects_control(effects);
            // A call: Tier 0's marked direct one, or its indirect one through a register (r11).
            const bool call = record->form == (X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP) ||
                              (record->form == X86_FORM_CALL_RM64 && record->mode == BUSTER_X86_REGISTER_DIRECT && record->rm == X86_REGISTER_R11);
            const bool transfer = control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_BRANCH;
            const bool listed = branch < code->branch_count && code->branches[branch] == i;
            const uint64_t forbidden = (nonvolatile & ~saved_registers) | (call ? 0 : BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RSP));
            if (!unwind_tier0_offset(code, final_offsets, size, i))
            {
                error = UNWIND_ERROR_INVALID;
            }
            else if (effects->operation == X86_OPERATION_NONE || (writes[lane] & forbidden) != 0 ||
                     (!call && (effects->memory & (BUSTER_X86_MEMORY_EFFECT_STACK_READ | BUSTER_X86_MEMORY_EFFECT_STACK_WRITE)) != 0) ||
                     (control != BUSTER_X86_CONTROL_NONE && !transfer && !(call && control == BUSTER_X86_CONTROL_CALL)) ||
                     listed != transfer || !unwind_tier0_store(record, effects, built.frame_size, pushes))
            {
                error = UNWIND_ERROR_UNSUPPORTED;
            }
            else if (transfer)
            {
                const uint32_t target = code->targets[branch];
                if (!unwind_tier0_branch(record->form) || target < begin || target > epilogue)
                {
                    error = UNWIND_ERROR_UNSUPPORTED;
                }
                branch += 1;
            }
        }
        first += count;
    }
    if (error == UNWIND_OK && branch != code->branch_count)
    {
        error = UNWIND_ERROR_UNSUPPORTED;
    }
    for (uint32_t event = 0; error == UNWIND_OK && event < built.event_count; event += 1)
    {
        built.offsets[event] = final_offsets[ends[event]];
    }
    if (error == UNWIND_OK)
    {
        *output = built;
    }
    return error;
}

static UnwindError unwind_tier0_scalar(const Tier0Code* code, const uint32_t* final_offsets, uint32_t size, UnwindFunction* output);

static UnwindError unwind_tier0(const Tier0Code* code, const uint32_t* final_offsets, uint32_t size, UnwindFunction* output)
{
#if !BUSTER_HOST_VECTOR
    return unwind_tier0_scalar(code, final_offsets, size, output);
#else
    return unwind_tier0_build(code, final_offsets, size, output, false);
#endif
}

// Whether a record is a push or a pop (`form`) of `reg`.
static bool unwind_tier0_stack(const X86Instruction* record, uint32_t form, uint8_t reg)
{
    return record->form == form && record->mode == BUSTER_X86_REGISTER_DIRECT && record->rm == reg;
}

// Whether a realigned frame's records after its reservation are Tier 0's (tier0_emit_prologue's): rsp rounded down to
// 2^`alignment_log2` (and rsp, -alignment: imm8 to 128 bytes), and with `base` rsp copied into r15 after it.
static bool unwind_tier0_realignment(const X86Instruction* records, uint32_t alignment_log2, bool base)
{
    const X86Instruction* realign = &records[0];
    const uint32_t form = alignment_log2 <= 7 ? X86_FORM_AND_RM64_IMM8 : X86_FORM_AND_RM64_IMM32;
    const bool rounds = realign->form == form && realign->mode == BUSTER_X86_REGISTER_DIRECT && realign->rm == X86_REGISTER_RSP &&
                        (int64_t)realign->immediate == -(int64_t)(UINT64_C(1) << alignment_log2);
    const X86Instruction* copy = base ? &records[1] : 0;
    const bool copies = copy == 0 || (copy->form == X86_FORM_MOV_RM64_R64 && copy->reg == X86_REGISTER_RSP && copy->rm == X86_REGISTER_R15 &&
                                      copy->mode == BUSTER_X86_REGISTER_DIRECT);
    return rounds && copies;
}

// Whether a frame pointer's function's release, the record before its pops, is Tier 0's: rsp back from rbp, past the
// `pushes` after rbp's (a lea, or a mov without any).
static bool unwind_tier0_release(const X86Instruction* record, uint32_t pushes)
{
    const bool lea = record->form == X86_FORM_LEA_R64_M && record->reg == X86_REGISTER_RSP && record->rm == X86_REGISTER_RBP &&
                     record->mode == BUSTER_X86_MEMORY_BASE && record->displacement == -8 * (int32_t)pushes;
    const bool mov = record->form == X86_FORM_MOV_RM64_R64 && record->reg == X86_REGISTER_RBP && record->rm == X86_REGISTER_RSP &&
                     record->mode == BUSTER_X86_REGISTER_DIRECT && pushes == 0;
    return lea || mov;
}

// The descriptor of a function from the frame its producer published (Tier0Code.frame_saved, frame_size, frame_pointer,
// frame_alignment_log2 and frame_base, #395), with no pass over its body: tier0_emit_prologue's pushes (rbp's and its mov
// first with a frame pointer), reservation and realignment (a realigned frame's and of rsp, and its base's mov to r15)
// are the code's first records, and tier0_emit_epilogue's release, pops and ret its last, so that each transition is the
// end of a record known by its index, which this checks is the instruction it must be. The body keeps the frame, and
// moves rsp only with a frame pointer, whose CFA is rbp's: unwind_tier0 proves that for the functions it takes (the tests
// compare the two there), and this takes the producer's word for the others (a frame pointer, a computed goto's
// indirect jump). A realigned frame's and, like its reservation, is no transition: from rbp's mov on, the CFA is rbp's,
// whatever rsp does. Its work is constant per function, which is what makes the C driver's unwind tables cheap enough to
// have by default; scalar, since no column of many items is there to take in lanes. A frameless function may end in a
// ud2 rather than the ret (unwind_tier0_end): the terminal builtins' helper, a ud2 alone, whose FDE of no transitions
// lets an unwinder from its trap reach its caller. UNWIND_ERROR_INVALID for code or a frame its records can't be,
// UNWIND_ERROR_UNSUPPORTED for a prologue or epilogue that isn't the frame's and UNWIND_ERROR_LIMIT past the format's
// limits, each leaving the descriptor alone.
//
// A function with tail calls (Tier0Code.tail_records, #147's musttail) has, before each one's jump, tier0_emit_teardown's
// release and pops, the epilogue's, which are its transitions: their ends and the jump's go into `tails`, which has
// unwind_tier0_tail_room's room and which the descriptor then points at (UnwindFunction.tails), each record checked as
// the epilogue's are and the jump a direct jmp's or r11's. Without `tails`, such a function is UNWIND_ERROR_INVALID.
static UnwindError unwind_tier0_frame_tails(const Tier0Code* code, const uint32_t* final_offsets, uint32_t size, UnwindFunction* output, uint32_t* tails)
{
    UnwindError error = UNWIND_OK;
    const uint32_t saved = code != 0 ? code->frame_saved : 0;
    const uint32_t pushes = (uint32_t)__builtin_popcount(saved);
    const bool pointer = code != 0 && code->frame_pointer;
    const bool reserved = code != 0 && code->frame_size != 0 && !pointer;
    const uint32_t alignment_log2 = code != 0 ? code->frame_alignment_log2 : 0;
    const bool base = code != 0 && code->frame_base;
    // An open frame (Tier0Code.frame_open, Tier 1's) has no epilogue at the end: each of its returns is a tail's.
    const bool open = code != 0 && code->frame_open;
    // The prologue's records, and the epilogue's: its teardown, from its release (a frame pointer's lea or mov, which is
    // no transition), and its ret.
    const uint32_t realignment = (alignment_log2 != 0 ? 1 : 0) + (base ? 1 : 0);
    const uint32_t prologue = (pointer ? 2 + pushes + (code->frame_size != 0 ? 1 : 0) + realignment : pushes + (reserved ? 1 : 0));
    const uint32_t teardown = pointer ? 1 + pushes + 1 : pushes + (reserved ? 1 : 0);
    const uint32_t epilogue = open ? 0 : teardown + 1;
    // A realigned frame has a frame pointer, past 16 bytes' alignment and within an and's immediate, and its base is r15,
    // one of its pushes.
    const bool realigned = alignment_log2 == 0 ? !base : pointer && alignment_log2 > 4 && alignment_log2 <= 31 && (!base || (saved & (1u << BUSTER_TIER0_SAVED_R15)) != 0);
    if (code == 0 || final_offsets == 0 || output == 0 || code->error != TIER0_OK || code->records == 0 || code->count == 0 || saved >= 64 ||
        (pointer && (saved & 2) != 0) || !realigned || prologue + epilogue > code->count || size == 0 || final_offsets[0] != 0)
    {
        error = UNWIND_ERROR_INVALID;
    }
    else if (size > INT32_MAX || code->frame_size > INT32_MAX)
    {
        error = UNWIND_ERROR_LIMIT;
    }
    // A frameless function's rules are the CIE's throughout, open or not, and it has no transitions to remember.
    UnwindFunction built = {
        .size = size,
        .saved = (uint8_t)saved,
        .pointer = pointer ? 1 : 0,
        .open = open && teardown != 0 ? 1 : 0,
        .frame_size = reserved ? code->frame_size : 0,
    };
    // Each transition's record, in order; its end is the next record's offset (or the size, for none past the ret).
    uint32_t events[BUSTER_UNWIND_EVENTS];
    uint32_t count = 0;
    const X86Instruction* records = error == UNWIND_OK ? code->records : 0;
    if (records != 0 && pointer)
    {
        error = unwind_tier0_stack(&records[0], X86_FORM_PUSH_R64, X86_REGISTER_RBP) &&
                records[1].form == X86_FORM_MOV_RM64_R64 && records[1].reg == X86_REGISTER_RSP && records[1].rm == X86_REGISTER_RBP &&
                records[1].mode == BUSTER_X86_REGISTER_DIRECT ? UNWIND_OK : UNWIND_ERROR_UNSUPPORTED;
        events[count++] = 0;
        events[count++] = 1;
    }
    uint32_t at = pointer ? 2 : 0;
    for (uint32_t r = 0; records != 0 && error == UNWIND_OK && r < BUSTER_ARRAY_LENGTH(unwind_tier0_saved_registers); r += 1)
    {
        if ((saved & (1u << r)) != 0)
        {
            error = unwind_tier0_stack(&records[at], X86_FORM_PUSH_R64, unwind_tier0_saved_registers[r]) ? UNWIND_OK : UNWIND_ERROR_UNSUPPORTED;
            events[count++] = at;
            at += 1;
        }
    }
    uint32_t amount = 0;
    if (records != 0 && error == UNWIND_OK && code->frame_size != 0)
    {
        error = unwind_tier0_adjustment(&records[at], true, &amount) && amount == code->frame_size ? UNWIND_OK : UNWIND_ERROR_UNSUPPORTED;
        if (reserved)
        {
            events[count++] = at;
        }
        at += 1;
    }
    if (records != 0 && error == UNWIND_OK && alignment_log2 != 0)
    {
        error = unwind_tier0_realignment(&records[at], alignment_log2, base) ? UNWIND_OK : UNWIND_ERROR_UNSUPPORTED;
    }
    // The epilogue, from its release.
    at = code != 0 ? code->count - epilogue : 0;
    if (records != 0 && error == UNWIND_OK && !open && (pointer || reserved))
    {
        error = pointer ? (unwind_tier0_release(&records[at], pushes) ? UNWIND_OK : UNWIND_ERROR_UNSUPPORTED)
                        : (unwind_tier0_adjustment(&records[at], false, &amount) && amount == code->frame_size ? UNWIND_OK : UNWIND_ERROR_UNSUPPORTED);
        if (reserved)
        {
            events[count++] = at;
        }
        at += 1;
    }
    for (uint32_t r = BUSTER_ARRAY_LENGTH(unwind_tier0_saved_registers); records != 0 && error == UNWIND_OK && !open && r > 0; r -= 1)
    {
        if ((saved & (1u << (r - 1))) != 0)
        {
            error = unwind_tier0_stack(&records[at], X86_FORM_POP_R64, unwind_tier0_saved_registers[r - 1]) ? UNWIND_OK : UNWIND_ERROR_UNSUPPORTED;
            events[count++] = at;
            at += 1;
        }
    }
    if (records != 0 && error == UNWIND_OK && !open && pointer)
    {
        error = unwind_tier0_stack(&records[at], X86_FORM_POP_R64, X86_REGISTER_RBP) ? UNWIND_OK : UNWIND_ERROR_UNSUPPORTED;
        events[count++] = at;
        at += 1;
    }
    const bool frameless = saved == 0 && !pointer && code != 0 && code->frame_size == 0;
    error = records != 0 && error == UNWIND_OK && !open && !(at == code->count - 1 && unwind_tier0_end(&records[at], frameless)) ? UNWIND_ERROR_UNSUPPORTED : error;
    // An open frame's last record is no epilogue's ret: one that is would be its epilogue, read as the body's.
    error = records != 0 && error == UNWIND_OK && open && records[code->count - 1].form == X86_FORM_RET ? UNWIND_ERROR_UNSUPPORTED : error;
    // The transitions' ends, each after the one before and before the ret's end, as branch sizing's offsets have them.
    uint32_t previous = 0;
    for (uint32_t event = 0; error == UNWIND_OK && event < count; event += 1)
    {
        const uint32_t end = final_offsets[events[event] + 1];
        error = end > previous && end < size ? UNWIND_OK : UNWIND_ERROR_INVALID;
        built.offsets[event] = end;
        previous = end;
    }
    built.event_count = (uint8_t)count;
    // The tail calls' teardowns and jumps, in order between the prologue's records and the epilogue's, each after the one
    // before; their transitions' ends between the prologue's last and the epilogue's first (or the size).
    const uint32_t tail_count = code != 0 && error == UNWIND_OK ? code->tail_count : 0;
    error = tail_count != 0 && (tails == 0 || code->tail_records == 0) ? UNWIND_ERROR_INVALID : error;
    const uint32_t opening = pointer ? 2 + pushes : pushes + (reserved ? 1 : 0);
    previous = opening != 0 && error == UNWIND_OK ? built.offsets[opening - 1] : 0;
    const uint32_t bound = count > opening ? built.offsets[opening] : size;
    uint32_t next = prologue;
    uint32_t written = 0;
    for (uint32_t tail = 0; error == UNWIND_OK && tail < tail_count; tail += 1)
    {
        const uint32_t jump = code->tail_records[tail];
        error = jump >= next && jump - next >= teardown && jump < code->count - epilogue ? UNWIND_OK : UNWIND_ERROR_INVALID;
        uint32_t record = error == UNWIND_OK ? jump - teardown : 0;
        uint32_t ends[BUSTER_UNWIND_EVENTS];
        uint32_t ended = 0;
        if (error == UNWIND_OK && (pointer || reserved))
        {
            uint32_t amount = 0;
            error = pointer ? (unwind_tier0_release(&records[record], pushes) ? UNWIND_OK : UNWIND_ERROR_UNSUPPORTED)
                            : (unwind_tier0_adjustment(&records[record], false, &amount) && amount == code->frame_size ? UNWIND_OK : UNWIND_ERROR_UNSUPPORTED);
            if (reserved)
            {
                ends[ended++] = record;
            }
            record += 1;
        }
        for (uint32_t r = BUSTER_ARRAY_LENGTH(unwind_tier0_saved_registers); error == UNWIND_OK && r > 0; r -= 1)
        {
            if ((saved & (1u << (r - 1))) != 0)
            {
                error = unwind_tier0_stack(&records[record], X86_FORM_POP_R64, unwind_tier0_saved_registers[r - 1]) ? UNWIND_OK : UNWIND_ERROR_UNSUPPORTED;
                ends[ended++] = record;
                record += 1;
            }
        }
        if (error == UNWIND_OK && pointer)
        {
            error = unwind_tier0_stack(&records[record], X86_FORM_POP_R64, X86_REGISTER_RBP) ? UNWIND_OK : UNWIND_ERROR_UNSUPPORTED;
            ends[ended++] = record;
            record += 1;
        }
        // The jump: tier0_emit_tail's direct one, whose fixup is a call's, or through r11; or an early return's ret.
        const X86Instruction* jumping = error == UNWIND_OK ? &records[jump] : 0;
        error = jumping != 0 && (jumping->form == (X86_FORM_JMP_REL32 | BUSTER_X86_FORM_FIXUP) || jumping->form == X86_FORM_RET ||
                                 (jumping->form == X86_FORM_JMP_RM64 && jumping->mode == BUSTER_X86_REGISTER_DIRECT && jumping->rm == X86_REGISTER_R11)) ? UNWIND_OK :
                error == UNWIND_OK ? UNWIND_ERROR_UNSUPPORTED : error;
        ends[ended++] = jump;
        for (uint32_t event = 0; error == UNWIND_OK && event < ended; event += 1)
        {
            const uint32_t end = final_offsets[ends[event] + 1];
            error = end > previous && end < bound ? UNWIND_OK : UNWIND_ERROR_INVALID;
            tails[written] = end;
            written += 1;
            previous = end;
        }
        next = jump + 1;
    }
    built.tail_count = error == UNWIND_OK ? tail_count : 0;
    built.tails = error == UNWIND_OK && tail_count != 0 ? tails : 0;
    if (error == UNWIND_OK)
    {
        *output = built;
    }
    return error;
}

// The room unwind_tier0_frame_tails's `tails` needs for `code`: each tail call's transitions (the epilogue's) and its
// jump's end.
static uint64_t unwind_tier0_tail_room(const Tier0Code* code)
{
    const uint32_t pushes = (uint32_t)__builtin_popcount(code->frame_saved);
    const uint32_t per = (code->frame_pointer ? pushes + 1 : pushes + (code->frame_size != 0 ? 1 : 0)) + 1;
    return (uint64_t)code->tail_count * per;
}

// unwind_tier0_frame_tails of a function without tail calls, as every reader but the C driver's emission has them.
static UnwindError unwind_tier0_frame(const Tier0Code* code, const uint32_t* final_offsets, uint32_t size, UnwindFunction* output)
{
    return unwind_tier0_frame_tails(code, final_offsets, size, output, 0);
}

static UnwindError unwind_tier0_scalar(const Tier0Code* code, const uint32_t* final_offsets, uint32_t size, UnwindFunction* output)
{
    return unwind_tier0_build(code, final_offsets, size, output, true);
}
