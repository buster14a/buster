// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's source origins and retained object locations (#1114). Include after emitted_code.c and tier1_emit.c.
// The IR sidecar names original typed entries and locals; the emitter names the IR node that wrote each record.
// Neither an instruction number nor an allocation interval is a source-variable identity. The post-scheduler's
// new-to-old permutation carries those record origins without adding a scheduling cut or changing code under -g.
//
// Retained objects use the actual frame's base and byte offsets. A shared physical slot is unavailable here until
// its source lifetime can be proved at final instruction boundaries. Prologue, entry copies, epilogues, returns and
// unknown origins are gaps. A body write to the frame base also makes these homes unavailable. Unsupported storage
// loses only its location; malformed metadata and capacity failures fail transactionally.
//
// Inputs remain immutable and disjoint from output/scratch throughout. Every column is bounded by its logical count;
// no padding is read. Source-run compaction has exact scalar/vector paths, with masked final batches. Retained work
// is proportional to records, locals, physical slots and the location ranges actually emitted, never a local-by-
// instruction matrix. Without -g this module is not called and its emitter metadata is not allocated.

typedef struct CheckTier1Debug CheckTier1Debug;
struct CheckTier1Debug
{
    uint32_t* source_records;
    uint32_t* source_origins;
    uint32_t source_count;
    EmittedLocalRange* local_ranges;
    uint32_t local_range_count;
};

typedef struct CheckTier1DebugSpan CheckTier1DebugSpan;
struct CheckTier1DebugSpan
{
    uint32_t begin;
    uint32_t end;
};

// The fixed prologue's actual instructions must establish the base that the frame layout describes. A probe's loop
// is deliberately not reconstructed here: an unsupported prologue retains source lines but publishes no frame homes.
// RSP's distance is known until realignment; R15 must then explicitly capture that aligned RSP for a dynamic frame.
static bool check_tier1_debug_frame_base(const Tier1EmitResult* emitted)
{
    const Tier1Frame* frame = &emitted->frame;
    const X86Instruction* const records = emitted->code.records;
    const uint32_t body = emitted->body_record;
    const uint32_t base = frame->base_register;
    const uint32_t expected_pushes = frame->saved_registers | (frame->frame_pointer ? 1u << X86_REGISTER_RBP : 0);
    const int64_t expected_stack = -(int64_t)frame->stack_adjust_bytes - 8 * (int64_t)__builtin_popcount(expected_pushes);
    int64_t stack = 0;
    uint32_t pushed = 0;
    bool pointer = false;
    bool aligned = false;
    bool captured = false;
    bool valid = body <= emitted->code.count && base < 16;
    for (uint32_t r = 0; valid && r < body; r += 1)
    {
        const X86Instruction record = records[r];
        const bool plain = record.mode == BUSTER_X86_REGISTER_DIRECT && record.index == 0 && record.vvvv == 0 && record.decorations == 0 && record.displacement == 0;
        uint64_t read = 0;
        uint64_t written = 0;
        x86_instruction_effects_scalar(&record, &read, &written);
        const bool push = plain && record.form == X86_FORM_PUSH_R64 && record.rm < 16 && record.immediate == 0;
        const bool move = plain && record.immediate == 0 && (record.form == X86_FORM_MOV_R64_RM64 || record.form == X86_FORM_MOV_RM64_R64);
        const uint32_t from = record.form == X86_FORM_MOV_R64_RM64 ? record.rm : record.reg;
        const uint32_t to = record.form == X86_FORM_MOV_R64_RM64 ? record.reg : record.rm;
        const bool subtract = plain && record.rm == X86_REGISTER_RSP && (record.form == X86_FORM_SUB_RM64_IMM8 || record.form == X86_FORM_SUB_RM64_IMM32);
        const bool align = plain && record.rm == X86_REGISTER_RSP && (record.form == X86_FORM_AND_RM64_IMM8 || record.form == X86_FORM_AND_RM64_IMM32);
        if (push)
        {
            valid = !aligned && record.rm != X86_REGISTER_RSP && (pushed & (1u << record.rm)) == 0;
            pushed |= 1u << record.rm;
            stack -= 8;
        }
        else if (move && from == X86_REGISTER_RSP && to == X86_REGISTER_RBP)
        {
            valid = frame->frame_pointer && !pointer && !aligned && stack == -8;
            pointer = valid;
        }
        else if (subtract)
        {
            const int64_t amount = (int64_t)record.immediate;
            valid = !aligned && amount > 0 && amount <= INT32_MAX;
            stack -= valid ? amount : 0;
        }
        else if (align)
        {
            valid = frame->realigned && pointer && !aligned && stack == expected_stack && (int64_t)record.immediate == -(int64_t)frame->alignment;
            aligned = valid;
        }
        else if (move && from == X86_REGISTER_RSP && to == BUSTER_TIER1_FRAME_REALIGN_BASE)
        {
            valid = base == BUSTER_TIER1_FRAME_REALIGN_BASE && aligned && !captured;
            captured = valid;
        }
        else
        {
            const uint64_t bases = BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RSP) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RBP) | BUSTER_X86_RESOURCE_GPR(base);
            valid = (written & bases) == 0;
        }
    }
    valid = valid && pushed == expected_pushes && stack == expected_stack && pointer == frame->frame_pointer && aligned == frame->realigned;
    valid = valid && (base == X86_REGISTER_RSP || (base == X86_REGISTER_RBP && pointer && !aligned) ||
                      (base == BUSTER_TIER1_FRAME_REALIGN_BASE && pointer && aligned && captured));
    for (uint32_t r = body; valid && r < emitted->code.count; r += 1)
    {
        const uint32_t kind = emitted->origins[r] >> BUSTER_TIER1_ORIGIN_SHIFT;
        const X86Instruction record = records[r];
        const uint32_t control = x86_effects_control(x86_effects(record.form));
        uint64_t read = 0;
        uint64_t written = 0;
        x86_instruction_effects_scalar(&record, &read, &written);
        const bool frame_record = kind == TIER1_ORIGIN_FRAME || control == BUSTER_X86_CONTROL_RETURN;
        // A call's architectural push is reflected in the callee's frame; the caller's resumed RSP is unchanged.
        const bool call_stack = base == X86_REGISTER_RSP && control == BUSTER_X86_CONTROL_CALL;
        valid = frame_record || call_stack || (written & BUSTER_X86_RESOURCE_GPR(base)) == 0;
    }
    return valid;
}

// Compact changes in the final records' source origins. The first record is a boundary even if its origin is unknown;
// unknown runs stay explicit so later lexical/location consumers cannot bridge them by borrowing a nearby token.
static uint32_t check_tier1_debug_sources(const uint32_t* entries, uint32_t first, uint32_t end, bool vector, uint32_t* records, uint32_t* origins)
{
    uint32_t count = 0;
    if (BUSTER_VECTOR_PATH(vector))
    {
        const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        const __m512i previous_lanes = _mm512_setr_epi32(0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
        uint32_t previous = BUSTER_EMISSION_NO_ORIGIN;
        for (uint32_t at = first; at < end;)
        {
            const uint32_t live = end - at < 16 ? end - at : 16;
            const __mmask16 mask = (__mmask16)((1u << live) - 1);
            const __m512i values = _mm512_maskz_loadu_epi32(mask, entries + at);
            const __m512i before = _mm512_mask_mov_epi32(_mm512_permutexvar_epi32(previous_lanes, values), 1, _mm512_set1_epi32((int32_t)previous));
            const __mmask16 changed = (_mm512_cmpneq_epi32_mask(values, before) | (at == first ? 1 : 0)) & mask;
            const __m512i positions = _mm512_add_epi32(lanes, _mm512_set1_epi32((int32_t)at));
            _mm512_mask_compressstoreu_epi32(records + count, changed, positions);
            _mm512_mask_compressstoreu_epi32(origins + count, changed, values);
            count += (uint32_t)__builtin_popcount((uint32_t)changed);
            previous = entries[at + live - 1];
            at += live;
        }
    }
    else
    {
        for (uint32_t at = first; at < end; at += 1)
        {
            if (at == first || entries[at] != entries[at - 1])
            {
                records[count] = at;
                origins[count] = entries[at];
                count += 1;
            }
        }
    }
    records[count] = end;
    return count;
}

// A retained original local's whole object, proved to have its own physical slot. Slot flags for ABI bookkeeping do
// not describe source objects. The original local's size must fit the surviving slot, including scalar homes.
static bool check_tier1_debug_home(const Tier0Function* function, const IrFunction* ir, const Tier1EmitResult* emitted, const uint32_t* uses, uint32_t slot, Tier0Home* home)
{
    const uint32_t local = ir->debug->locals[slot];
    const IrSlot object = ir->slots[slot];
    const Tier1Frame* frame = &emitted->frame;
    const uint32_t row = tier1_emit_cfg_object_row(emitted, slot);
    const uint32_t bytes = function->local_sizes != NULL && function->local_sizes[local] != 0 ? function->local_sizes[local] : tier0_class_bytes(function->local_classes[local]);
    const uint32_t pushes = (uint32_t)__builtin_popcount(frame->saved_registers);
    const int64_t bias = frame->red_zone ? -(int64_t)frame->red_zone_bytes : frame->base_register == X86_REGISTER_RBP ? -(int64_t)frame->stack_adjust_bytes - 8 * (int64_t)pushes : 0;
    int32_t displacement = 0;
    const bool special = (object.flags & (BUSTER_IR_SLOT_REGISTER_SAVE_AREA | BUSTER_IR_SLOT_OVERFLOW_AREA)) != 0;
    bool valid = !special && bytes != 0 && bytes <= object.size && object.size <= INT32_MAX && row < frame->count;
    valid = valid && frame->slot[row] < frame->slot_count && uses[frame->slot[row]] == 1;
    valid = valid && tier1_emit_cfg_object(ir, emitted, slot, &displacement);
    valid = valid && displacement >= bias && (int64_t)displacement + (int64_t)object.size <= bias + frame->payload_bytes;
    *home = (Tier0Home){
        .displacement = displacement,
        .reg = (uint8_t)frame->base_register,
        .mode = valid ? BUSTER_X86_MEMORY_BASE : 0,
    };
    return valid;
}

static bool check_tier1_debug(const Tier0Function* function, const IrFunction* ir, const Tier1EmitResult* emitted, const uint32_t* record_order, uint32_t epilogue,
                             bool vector, Arena* output, Arena* scratch, CheckTier1Debug* result)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const IrDebugInfo* debug = ir->debug;
    const uint32_t count = emitted->code.count;
    const uint32_t body = emitted->body_record;
    const uint32_t nodes = ir->instruction_count;
    const uint32_t slots = ir->slot_count;
    const uint32_t locals = function->local_count;
    const Tier1Frame* frame = &emitted->frame;
    bool valid = debug != NULL && debug->instruction_count == nodes && debug->slot_count == slots && debug->entry_count == function->count && debug->local_count == locals &&
                 (nodes == 0 || debug->origins != NULL) && (slots == 0 || debug->locals != NULL) && (count == 0 || emitted->origins != NULL) &&
                 body <= epilogue && epilogue <= count && (record_order == NULL || record_order[count] == count) &&
                 (!emitted->general || emitted->cfg_input.block_count >= ir->block_count);
    CheckTier1Debug candidate = { 0 };
    uint32_t* entries = valid ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    uint8_t* safe = valid ? ir_allocate(scratch, (uint64_t)count + 1, 1, &valid) : NULL;
    uint8_t* seen = valid && record_order != NULL ? ir_allocate(scratch, (uint64_t)count + 1, 1, &valid) : NULL;
    CheckTier1DebugSpan* spans = valid ? ir_allocate(scratch, (uint64_t)count + 1, sizeof(CheckTier1DebugSpan), &valid) : NULL;
    if (valid && seen != NULL)
    {
        memset(seen, 0, (uint64_t)count + 1);
    }
    const uint32_t position_base = valid && emitted->general ? 2 * (emitted->cfg_input.block_count - ir->block_count) : 0;
    for (uint32_t r = 0; valid && r < count; r += 1)
    {
        const uint32_t old = record_order != NULL ? record_order[r] : r;
        valid = old < count && (seen == NULL || seen[old] == 0);
        if (valid)
        {
            if (seen != NULL)
            {
                seen[old] = 1;
            }
            const uint32_t origin = emitted->origins[old];
            const uint32_t kind = origin >> BUSTER_TIER1_ORIGIN_SHIFT;
            const uint32_t payload = origin & BUSTER_TIER1_ORIGIN_PAYLOAD;
            const bool own = kind == TIER1_ORIGIN_NODE || kind == TIER1_ORIGIN_OTHER;
            const bool move = kind == TIER1_ORIGIN_MOVE && payload >= position_base;
            const uint32_t node = own ? payload : move ? (payload - position_base) / 2 : BUSTER_IR_NONE;
            const uint32_t entry = node < nodes ? debug->origins[node] : BUSTER_EMISSION_NO_ORIGIN;
            const uint32_t control = x86_effects_control(x86_effects(emitted->code.records[old].form));
            valid = kind <= TIER1_ORIGIN_OTHER && (!own || node < nodes) && (entry == BUSTER_EMISSION_NO_ORIGIN || entry < function->count);
            entries[r] = entry;
            safe[r] = entry != BUSTER_EMISSION_NO_ORIGIN && old >= body && r >= body && r < epilogue && kind != TIER1_ORIGIN_FRAME && kind != TIER1_ORIGIN_ENTRY && control != BUSTER_X86_CONTROL_RETURN;
        }
    }
    uint32_t span_count = 0;
    for (uint32_t first = body; valid && first < epilogue;)
    {
        uint32_t end = first + 1;
        if (safe[first] != 0)
        {
            while (end < epilogue && safe[end] != 0)
            {
                end += 1;
            }
            spans[span_count++] = (CheckTier1DebugSpan){ .begin = first, .end = end };
        }
        first = end;
    }
    const bool framed = valid && emitted->general && slots != 0 && frame->count != 0 && frame->slot != NULL && frame->offset != NULL && check_tier1_debug_frame_base(emitted);
    uint32_t* uses = valid && framed ? ir_allocate(scratch, (uint64_t)frame->slot_count + 1, 4, &valid) : NULL;
    uint32_t* local_slots = valid ? ir_allocate(scratch, (uint64_t)locals + 1, 4, &valid) : NULL;
    Tier0Home* homes = valid ? ir_allocate(scratch, (uint64_t)locals + 1, sizeof(Tier0Home), &valid) : NULL;
    if (valid)
    {
        memset(local_slots, 0xff, ((uint64_t)locals + 1) * 4);
        if (framed)
        {
            memset(uses, 0, ((uint64_t)frame->slot_count + 1) * 4);
        }
    }
    for (uint32_t row = 0; valid && framed && row < frame->count; row += 1)
    {
        const uint32_t physical = frame->slot[row];
        valid = physical < frame->slot_count;
        if (valid)
        {
            uses[physical] += 1;
        }
    }
    for (uint32_t slot = 0; valid && slot < slots; slot += 1)
    {
        const uint32_t local = debug->locals[slot];
        valid = local == BUSTER_IR_NONE || local < locals;
        if (valid && local != BUSTER_IR_NONE)
        {
            // A split or a duplicate cannot become a whole source object by picking one of its surviving pieces.
            local_slots[local] = local_slots[local] == BUSTER_IR_NONE ? slot : UINT32_MAX - 1;
        }
    }
    uint32_t home_count = 0;
    for (uint32_t local = 0; valid && local < locals; local += 1)
    {
        const uint32_t slot = local_slots[local];
        const bool found = framed && slot < slots && check_tier1_debug_home(function, ir, emitted, uses, slot, homes + local);
        local_slots[local] = found ? slot : BUSTER_IR_NONE;
        home_count += found;
    }
    const uint64_t ranges = (uint64_t)home_count * span_count;
    valid = valid && ranges <= UINT32_MAX;
    if (valid)
    {
        candidate.source_records = ir_allocate(output, (uint64_t)count + 1, 4, &valid);
        candidate.source_origins = ir_allocate(output, (uint64_t)count + 1, 4, &valid);
        candidate.local_ranges = ranges != 0 ? ir_allocate(output, ranges, sizeof(EmittedLocalRange), &valid) : NULL;
    }
    if (valid)
    {
        candidate.source_count = check_tier1_debug_sources(entries, body, epilogue, vector, candidate.source_records, candidate.source_origins);
        for (uint32_t local = 0; local < locals; local += 1)
        {
            for (uint32_t span = 0; local_slots[local] != BUSTER_IR_NONE && span < span_count; span += 1)
            {
                candidate.local_ranges[candidate.local_range_count++] = (EmittedLocalRange){
                    .home = homes[local],
                    .local = local,
                    .begin = spans[span].begin,
                    .end = spans[span].end,
                };
            }
        }
        *result = candidate;
    }
    else
    {
        arena_pop_to(output, output_mark);
        *result = (CheckTier1Debug){ 0 };
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}
