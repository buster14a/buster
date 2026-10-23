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
        if (suffix > code->count - begin || code->records[code->count - 1].form != X86_FORM_RET)
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
    // Prefix and suffix are bounded by fourteen frame events and the final RET. The body's offset checks share its
    // effects pass below, so there is no additional whole-function validation walk.
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

static UnwindError unwind_tier0(const Tier0Code* code, const uint32_t* final_offsets, uint32_t size, UnwindFunction* output)
{
    return unwind_tier0_build(code, final_offsets, size, output, false);
}

static UnwindError unwind_tier0_scalar(const Tier0Code* code, const uint32_t* final_offsets, uint32_t size, UnwindFunction* output)
{
    return unwind_tier0_build(code, final_offsets, size, output, true);
}
