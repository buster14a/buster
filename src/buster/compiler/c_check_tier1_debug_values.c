// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Scalar source assignments and decoded machine carriers (#1114). Included by c_check_tier1_debug.c after its frame
// proof. Source bindings are immutable typed assignment identities; record origins only limit source visibility.
// Checked definitions establish SSA values after their actual complete machine sequence. Copies and clobbers then
// follow actual registers and compiler frame rows, independently of allocation intervals and source line numbers.
//
// Each machine block owns only its proved incoming facts. Its first incoming edge allocates that sparse row once;
// subsequent predecessors intersect and compact it in place. One dense carrier scratch row is reused for transfer,
// never a local-by-instruction matrix. Carrier kills and selection have exact SIMD/scalar twins. Unsupported control
// loses machine facts; literal source assignments remain usable. All borrowed arrays stay immutable throughout.

typedef struct CheckTier1DebugValueHome CheckTier1DebugValueHome;
struct CheckTier1DebugValueHome
{
    Tier0Home home;
    uint8_t bytes;
    bool addressable;
};

typedef struct CheckTier1DebugValueTransfer CheckTier1DebugValueTransfer;
struct CheckTier1DebugValueTransfer
{
    uint32_t killed;
    uint32_t from;
    uint32_t to;
    uint32_t definition;
    uint32_t defined_at;
    int32_t memory_at;
    uint32_t memory_bytes;
    uint8_t bytes;
    uint8_t control;
    bool kill_memory;
    bool kill_addressable;
};

typedef struct CheckTier1DebugValueFact CheckTier1DebugValueFact;
struct CheckTier1DebugValueFact
{
    uint32_t place;
    uint32_t value;
};

typedef struct CheckTier1DebugValueBlock CheckTier1DebugValueBlock;
struct CheckTier1DebugValueBlock
{
    CheckTier1DebugValueFact* facts;
    uint32_t count;
    bool reached;
};

typedef struct CheckTier1DebugValueLive CheckTier1DebugValueLive;
struct CheckTier1DebugValueLive
{
    uint32_t* heads;
    uint32_t* next;
    uint32_t* previous;
    uint32_t* known;
    uint32_t* known_at;
    uint32_t* users;
    uint32_t* user_next;
    uint32_t* user_previous;
    uint32_t* user_value;
    uint32_t* changed_values;
    uint8_t* value_queued;
    uint32_t* changed_locals;
    uint8_t* local_queued;
    uint32_t known_count;
    uint32_t value_count;
    uint32_t local_count;
};

typedef struct CheckTier1DebugValues CheckTier1DebugValues;
struct CheckTier1DebugValues
{
    const Tier0Function* function;
    const IrFunction* ir;
    CheckTier1DebugValueTransfer* transfers;
    CheckTier1DebugValueHome* homes;
    uint32_t home_count;
    uint32_t* home_hash;
    uint32_t hash_count;
    uint32_t* first;
    CheckTier1DebugValueBlock* blocks;
    uint32_t block_count;
    uint32_t carrier_count;
    uint32_t value_count;
    uint8_t* widths;
    uint8_t* floating;
    uint8_t* needed;
    uint32_t* initial;
    uint32_t* completion;
    uint32_t* binding_first;
    uint32_t* binding_order;
    uint32_t* binding_end;
    uint32_t* locals;
    uint32_t local_count;
    uint32_t* local_index;
    uint32_t* events;
    uint32_t event_count;
    uint32_t* kill_first;
    uint32_t* kills;
    // Reused by the count and fill passes, which are sequential.
    uint32_t* state;
    uint32_t* winner;
    uint32_t* active;
    uint32_t* begins;
    uint32_t* selected;
    CheckTier1DebugValueLive live;
    bool machine;
};

static uint32_t check_tier1_debug_value_width(uint32_t type)
{
    return type == IR_I1 || type == IR_I8 ? 1 : type == IR_I16 ? 2 : type == IR_I32 || type == IR_F32 ? 4 :
           type == IR_I64 || type == IR_PTR || type == IR_F64 ? 8 : 0;
}

static uint32_t check_tier1_debug_value_home(const CheckTier1DebugValues* proof, int64_t displacement)
{
    uint32_t found = BUSTER_IR_NONE;
    if (proof->hash_count != 0 && displacement >= INT32_MIN && displacement <= INT32_MAX)
    {
        const uint32_t mask = proof->hash_count - 1;
        uint32_t at = ((uint32_t)displacement * UINT32_C(2654435761)) & mask;
        while (proof->home_hash[at] != BUSTER_IR_NONE && found == BUSTER_IR_NONE)
        {
            const uint32_t home = proof->home_hash[at];
            found = proof->homes[home].home.displacement == displacement ? home : BUSTER_IR_NONE;
            at = (at + 1) & mask;
        }
    }
    return found;
}

// Compiler rows are addressed by exact physical byte offsets. Different logical rows sharing those bytes are one
// carrier. Only their first eight bytes can hold these scalars. A row shared with an addressable object loses its
// cached value at an unknown pointer write or a call, since either may change the object's bytes through an alias.
static bool check_tier1_debug_value_homes(const Tier1EmitResult* emitted, Arena* scratch, CheckTier1DebugValues* proof)
{
    const Tier1Frame* frame = &emitted->frame;
    const uint32_t count = frame->slot_count;
    const bool framed = frame->count != 0 && frame->slot != NULL && frame->offset != NULL && check_tier1_debug_frame_base(emitted);
    bool valid = !framed || count <= INT32_MAX - 32u;
    const uint32_t rows = framed ? emitted->general ? tier1_emit_cfg_object_row(emitted, 0) : frame->count : 0;
    proof->home_count = framed ? count : 0;
    proof->homes = valid && framed ? ir_allocate(scratch, count, sizeof(CheckTier1DebugValueHome), &valid) : NULL;
    uint32_t hash_count = framed ? 1 : 0;
    while (valid && framed && hash_count <= count)
    {
        valid = hash_count <= UINT32_MAX / 2;
        hash_count = valid ? hash_count * 2 : hash_count;
    }
    proof->home_hash = valid && framed ? ir_allocate(scratch, hash_count, 4, &valid) : NULL;
    proof->hash_count = hash_count;
    if (valid && framed)
    {
        memset(proof->homes, 0, (uint64_t)count * sizeof(CheckTier1DebugValueHome));
        memset(proof->home_hash, 0xff, (uint64_t)hash_count * 4);
    }
    for (uint32_t row = 0; valid && row < frame->count && framed; row += 1)
    {
        const uint32_t physical = frame->slot[row];
        valid = physical < count;
        if (valid && row < rows)
        {
            const uint32_t bytes = emitted->general ? tier1_emit_cfg_row_bytes(emitted, row) : row < emitted->allocation.spill_count ?
                                   emitted->prepared.sizes[emitted->allocation.spill_values[row]] : 8;
            CheckTier1DebugValueHome* home = proof->homes + physical;
            const int64_t bias = frame->red_zone ? -(int64_t)frame->red_zone_bytes : frame->base_register == X86_REGISTER_RBP ?
                                 -(int64_t)frame->stack_adjust_bytes - 8 * (int64_t)__builtin_popcount(frame->saved_registers) : 0;
            valid = bytes != 0 && frame->offset[row] >= bias && (int64_t)frame->offset[row] + bytes <= bias + frame->payload_bytes &&
                    (home->bytes == 0 || home->home.displacement == frame->offset[row]);
            if (valid)
            {
                home->home = (Tier0Home){ .displacement = frame->offset[row], .reg = frame->base_register, .mode = BUSTER_X86_MEMORY_BASE };
                const uint32_t width = bytes < 8 ? bytes : 8;
                home->bytes = (uint8_t)(home->bytes > width ? home->bytes : width);
            }
        }
        else if (valid)
        {
            proof->homes[physical].addressable = true;
        }
    }
    for (uint32_t h = 0; valid && h < proof->home_count; h += 1)
    {
        const CheckTier1DebugValueHome home = proof->homes[h];
        if (home.bytes != 0)
        {
            const uint32_t mask = hash_count - 1;
            uint32_t at = ((uint32_t)home.home.displacement * UINT32_C(2654435761)) & mask;
            while (valid && proof->home_hash[at] != BUSTER_IR_NONE)
            {
                valid = proof->homes[proof->home_hash[at]].home.displacement != home.home.displacement;
                at = (at + 1) & mask;
            }
            if (valid)
            {
                proof->home_hash[at] = h;
            }
        }
    }
    proof->carrier_count = 32 + proof->home_count;
    return valid;
}

static CheckTier1DebugValueTransfer check_tier1_debug_value_transfer(const CheckTier1DebugValues* proof, const Tier1EmitResult* emitted, uint32_t old)
{
    const X86Instruction record = emitted->code.records[old];
    const CheckTier1DebugTransfer registers = check_tier1_debug_transfer(&record);
    CheckTier1DebugValueTransfer result = {
        .killed = registers.killed,
        .from = registers.bytes != 0 ? registers.from : BUSTER_IR_NONE,
        .to = registers.bytes != 0 ? registers.to : BUSTER_IR_NONE,
        .definition = BUSTER_IR_NONE,
        .defined_at = BUSTER_IR_NONE,
        .bytes = registers.bytes,
        .control = registers.control,
    };
    const uint32_t origin = emitted->origins[old] >> BUSTER_TIER1_ORIGIN_SHIFT;
    const bool body = old >= emitted->body_record && origin != TIER1_ORIGIN_FRAME && registers.control != BUSTER_X86_CONTROL_RETURN;
    uint64_t read = 0;
    uint64_t written = 0;
    x86_instruction_effects_scalar(&record, &read, &written);
    const bool store = (written & BUSTER_X86_RESOURCE_MEMORY) != 0;
    const bool based = record.mode == BUSTER_X86_MEMORY_BASE && record.rm == emitted->frame.base_register;
    if (proof->home_count != 0)
    {
        result.kill_memory = !body || ((written & BUSTER_X86_RESOURCE_GPR(emitted->frame.base_register)) != 0 && registers.control != BUSTER_X86_CONTROL_CALL);
        result.kill_addressable = (store && !based) || registers.control == BUSTER_X86_CONTROL_CALL;
        if (store && based)
        {
            const X86FormDescription* description = x86_form_description(record.form & (BUSTER_X86_FORM_FIXUP - 1));
            result.memory_at = record.displacement;
            result.memory_bytes = description->operand_bytes;
            result.kill_memory |= description->operand_bytes == 0;
        }
        else if (store && (record.mode & BUSTER_X86_MODE_BASE) != 0 &&
                 (record.rm == emitted->frame.base_register || record.rm == X86_REGISTER_RSP || record.rm == X86_REGISTER_RBP || record.rm == BUSTER_TIER1_FRAME_REALIGN_BASE))
        {
            result.kill_memory = true;
        }
    }
    const bool plain = body && based && record.reg < 16 && record.index == 0 && record.vvvv == 0 && record.decorations == 0 && record.immediate == 0;
    if (plain)
    {
        uint32_t bytes = 0;
        uint32_t reg = record.reg;
        bool load = true;
        switch (record.form)
        {
            case X86_FORM_MOV_R64_RM64: bytes = 8; break;
            case X86_FORM_MOV_RM64_R64: bytes = 8; load = false; break;
            case X86_FORM_MOV_R32_RM32: bytes = 4; break;
            case X86_FORM_MOV_RM32_R32: bytes = 4; load = false; break;
            case X86_FORM_MOV_RM16_R16: bytes = 2; load = false; break;
            case X86_FORM_MOV_RM8_R8: bytes = 1; load = false; break;
            case X86_FORM_MOVZX_R32_RM8:
            case X86_FORM_MOVZX_R64_RM8:
            case X86_FORM_MOVSX_R32_RM8:
            case X86_FORM_MOVSX_R64_RM8: bytes = 1; break;
            case X86_FORM_MOVZX_R32_RM16:
            case X86_FORM_MOVZX_R64_RM16:
            case X86_FORM_MOVSX_R32_RM16:
            case X86_FORM_MOVSX_R64_RM16: bytes = 2; break;
            case X86_FORM_MOVSXD_R64_RM32: bytes = 4; break;
            default: break;
        }
        for (uint32_t m = 0; bytes == 0 && m < BUSTER_ARRAY_LENGTH(tier1_cert_emitted_vector_moves); m += 1)
        {
            const Tier1CertEmittedVectorMove move = tier1_cert_emitted_vector_moves[m];
            if (move.form == record.form && move.memory)
            {
                bytes = move.width;
                reg += 16;
                load = move.load;
            }
        }
        const uint32_t home = bytes != 0 ? check_tier1_debug_value_home(proof, record.displacement) : BUSTER_IR_NONE;
        if (home != BUSTER_IR_NONE)
        {
            result.from = load ? 32 + home : reg;
            result.to = load ? reg : 32 + home;
            result.bytes = (uint8_t)(bytes > 8 ? 8 : bytes);
        }
    }
    return result;
}

static void check_tier1_debug_value_step(const CheckTier1DebugValues* proof, CheckTier1DebugValueTransfer transfer, bool vector, uint32_t* state)
{
    const uint32_t copied = transfer.from < proof->carrier_count ? state[transfer.from] : BUSTER_IR_NONE;
    if (BUSTER_VECTOR_PATH(vector))
    {
        for (uint32_t at = 0; at < 32; at += 16)
        {
            const __m512i before = _mm512_loadu_si512(state + at);
            const __m512i after = _mm512_mask_mov_epi32(before, (__mmask16)(transfer.killed >> at), _mm512_set1_epi32(-1));
            _mm512_storeu_si512(state + at, after);
        }
    }
    else
    {
        uint32_t killed = transfer.killed;
        while (killed != 0)
        {
            state[(uint32_t)__builtin_ctz(killed)] = BUSTER_IR_NONE;
            killed &= killed - 1;
        }
    }
    if (transfer.kill_memory)
    {
        memset(state + 32, 0xff, (uint64_t)proof->home_count * 4);
    }
    else
    {
        for (uint32_t h = 0; transfer.kill_addressable && h < proof->home_count; h += 1)
        {
            state[32 + h] = proof->homes[h].addressable ? BUSTER_IR_NONE : state[32 + h];
        }
        for (int64_t at = (int64_t)transfer.memory_at - 7; transfer.memory_bytes != 0 && at < (int64_t)transfer.memory_at + transfer.memory_bytes; at += 1)
        {
            const uint32_t home = check_tier1_debug_value_home(proof, at);
            if (home != BUSTER_IR_NONE && at + proof->homes[home].bytes > transfer.memory_at)
            {
                state[32 + home] = BUSTER_IR_NONE;
            }
        }
    }
    const bool class = transfer.to >= 32 || (copied < proof->value_count && proof->floating[copied] == (transfer.to >= 16));
    const bool fits = transfer.to < 32 || (transfer.to < proof->carrier_count && copied < proof->value_count && proof->widths[copied] <= proof->homes[transfer.to - 32].bytes);
    if (copied < proof->value_count && transfer.to < proof->carrier_count && proof->widths[copied] <= transfer.bytes && class && fits)
    {
        state[transfer.to] = copied;
    }
    if (transfer.definition < proof->value_count && transfer.defined_at < 32 && proof->needed[transfer.definition] != 0)
    {
        state[transfer.defined_at] = transfer.definition;
    }
}

static void check_tier1_debug_value_restore(const CheckTier1DebugValues* proof, const CheckTier1DebugValueBlock* block, uint32_t* state)
{
    memset(state, 0xff, (uint64_t)proof->carrier_count * 4);
    for (uint32_t f = 0; f < block->count; f += 1)
    {
        state[block->facts[f].place] = block->facts[f].value;
    }
}

static bool check_tier1_debug_value_meet(const CheckTier1DebugValues* proof, const uint32_t* state, bool vector, Arena* scratch, CheckTier1DebugValueBlock* block, bool* changed)
{
    bool valid = true;
    *changed = !block->reached;
    if (!block->reached)
    {
        uint32_t count = 0;
        for (uint32_t c = 0; c < proof->carrier_count; c += 1)
        {
            count += state[c] != BUSTER_IR_NONE;
        }
        block->facts = count != 0 ? ir_allocate(scratch, count, sizeof(CheckTier1DebugValueFact), &valid) : NULL;
        if (valid)
        {
            block->count = count;
            block->reached = true;
            uint32_t at = 0;
            for (uint32_t c = 0; c < proof->carrier_count; c += 1)
            {
                if (state[c] != BUSTER_IR_NONE)
                {
                    block->facts[at++] = (CheckTier1DebugValueFact){ .place = c, .value = state[c] };
                }
            }
        }
    }
    else
    {
        // Facts are sparse and sorted by carrier; a meet only removes entries from this first-incoming row.
        uint32_t kept = 0;
        for (uint32_t at = 0; at < block->count;)
        {
            const uint32_t live = block->count - at < 8 ? block->count - at : 8;
            uint32_t equal = 0;
            if (BUSTER_VECTOR_PATH(vector))
            {
                const __mmask16 mask = (__mmask16)((1u << (2 * live)) - 1);
                const __m512i facts = _mm512_maskz_loadu_epi32(mask, block->facts + at);
                const __m512i places = _mm512_permutexvar_epi32(_mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14, 0, 0, 0, 0, 0, 0, 0, 0), facts);
                const __m512i values = _mm512_permutexvar_epi32(_mm512_setr_epi32(1, 3, 5, 7, 9, 11, 13, 15, 0, 0, 0, 0, 0, 0, 0, 0), facts);
                const __mmask16 lanes = (__mmask16)((1u << live) - 1);
                const __m512i incoming = _mm512_mask_i32gather_epi32(_mm512_set1_epi32(-1), lanes, places, state, 4);
                equal = _mm512_cmpeq_epi32_mask(values, incoming) & lanes;
            }
            else
            {
                for (uint32_t k = 0; k < live; k += 1)
                {
                    const CheckTier1DebugValueFact fact = block->facts[at + k];
                    equal |= (uint32_t)(state[fact.place] == fact.value) << k;
                }
            }
            for (uint32_t k = 0; k < live; k += 1)
            {
                if (((equal >> k) & 1) != 0)
                {
                    block->facts[kept++] = block->facts[at + k];
                }
            }
            at += live;
        }
        *changed = kept != block->count;
        block->count = kept;
    }
    return valid;
}

static bool check_tier1_debug_values(const Tier0Function* function, const IrFunction* ir, const Tier1EmitResult* emitted,
                                    const uint32_t* record_order, bool vector, Arena* scratch, CheckTier1DebugValues* result)
{
    const IrDebugInfo* debug = ir->debug;
    const uint32_t nodes = ir->instruction_count;
    const uint32_t parameters = function->parameter_count;
    const uint32_t locals = function->local_count;
    const uint32_t count = emitted->code.count;
    const uint32_t bindings = debug->binding_count;
    CheckTier1DebugValues proof = { .function = function, .ir = ir, .value_count = nodes + parameters };
    bool valid = parameters <= UINT32_MAX - nodes && debug->binding_flags != NULL && (bindings == 0 || debug->bindings != NULL) &&
                 bindings <= (uint64_t)function->count + parameters && debug->first_block_end <= function->count &&
                 (count == 0 || (emitted->origins != NULL && emitted->code.records != NULL)) &&
                 (emitted->code.branch_count == 0 || (emitted->code.branches != NULL && emitted->code.targets != NULL));
    proof.widths = valid ? ir_allocate(scratch, (uint64_t)proof.value_count + 1, 1, &valid) : NULL;
    proof.floating = valid ? ir_allocate(scratch, (uint64_t)proof.value_count + 1, 1, &valid) : NULL;
    proof.needed = valid ? ir_allocate(scratch, (uint64_t)proof.value_count + 1, 1, &valid) : NULL;
    proof.initial = valid ? ir_allocate(scratch, (uint64_t)parameters + 1, 4, &valid) : NULL;
    proof.completion = valid ? ir_allocate(scratch, (uint64_t)nodes + 1, 4, &valid) : NULL;
    proof.binding_first = valid ? ir_allocate(scratch, (uint64_t)locals + 1, 4, &valid) : NULL;
    proof.binding_order = valid ? ir_allocate(scratch, (uint64_t)bindings + 1, 4, &valid) : NULL;
    proof.binding_end = valid ? ir_allocate(scratch, (uint64_t)bindings + 1, 4, &valid) : NULL;
    proof.locals = valid ? ir_allocate(scratch, (uint64_t)locals + 1, 4, &valid) : NULL;
    proof.local_index = valid ? ir_allocate(scratch, (uint64_t)locals + 1, 4, &valid) : NULL;
    proof.events = valid ? ir_allocate(scratch, (uint64_t)bindings + 1, 4, &valid) : NULL;
    uint32_t* next = valid ? ir_allocate(scratch, (uint64_t)locals + 1, 4, &valid) : NULL;
    uint32_t seed[32];
    memset(seed, 0xff, sizeof(seed));
    if (valid)
    {
        memset(proof.needed, 0, (uint64_t)proof.value_count + 1);
        memset(proof.initial, 0xff, ((uint64_t)parameters + 1) * 4);
        memset(proof.completion, 0xff, ((uint64_t)nodes + 1) * 4);
        memset(proof.binding_first, 0, ((uint64_t)locals + 1) * 4);
        memset(proof.local_index, 0xff, ((uint64_t)locals + 1) * 4);
        for (uint32_t v = 0; v < proof.value_count; v += 1)
        {
            const uint32_t type = v < nodes ? ir->type[v] : ir_typed_types[function->local_classes[v - nodes]];
            proof.widths[v] = (uint8_t)check_tier1_debug_value_width(type);
            proof.floating[v] = type == IR_F32 || type == IR_F64;
        }
    }
    for (uint32_t b = 0; valid && b < bindings; b += 1)
    {
        const IrDebugBinding binding = debug->bindings[b];
        valid = binding.local < locals && binding.kind >= IR_DEBUG_BIND_VALUE && binding.kind <= IR_DEBUG_BIND_UNAVAILABLE &&
                (binding.entry == BUSTER_IR_NONE ? binding.local < parameters : binding.entry < function->count) &&
                (binding.value == BUSTER_IR_NONE || binding.value < nodes) && (binding.anchor == BUSTER_IR_NONE || binding.anchor < nodes);
        if (valid)
        {
            proof.binding_first[binding.local + 1] += 1;
            if ((debug->binding_flags[binding.local] & BUSTER_IR_DEBUG_BIND_ENTRY) != 0)
            {
                valid = binding.bytes != 0 && binding.bytes <= 8 && binding.bytes == tier0_class_bytes(function->local_classes[binding.local]) &&
                        binding.type == ir_typed_types[function->local_classes[binding.local]] &&
                        (binding.entry == BUSTER_IR_NONE || binding.entry < debug->first_block_end) &&
                        binding.bytes == check_tier1_debug_value_width(binding.type) &&
                        (binding.kind != IR_DEBUG_BIND_VALUE || (binding.value < nodes && ir->type[binding.value] == binding.type));
                if (valid && binding.kind == IR_DEBUG_BIND_VALUE)
                {
                    proof.needed[binding.value] = 1;
                }
                if (valid && binding.entry != BUSTER_IR_NONE)
                {
                    valid = proof.event_count == 0 || debug->bindings[proof.events[proof.event_count - 1]].entry < binding.entry;
                    proof.events[proof.event_count++] = b;
                }
            }
        }
    }
    for (uint32_t local = 0; valid && local < locals; local += 1)
    {
        valid = (debug->binding_flags[local] & ~BUSTER_IR_DEBUG_BIND_ENTRY) == 0;
        proof.binding_first[local + 1] += proof.binding_first[local];
        next[local] = proof.binding_first[local];
        if ((debug->binding_flags[local] & BUSTER_IR_DEBUG_BIND_ENTRY) != 0 && proof.binding_first[local] != proof.binding_first[local + 1])
        {
            proof.local_index[local] = proof.local_count;
            proof.locals[proof.local_count++] = local;
        }
    }
    for (uint32_t b = 0; valid && b < bindings; b += 1)
    {
        proof.binding_order[next[debug->bindings[b].local]++] = b;
    }
    for (uint32_t local = 0; valid && local < locals; local += 1)
    {
        const uint32_t first = proof.binding_first[local];
        const uint32_t end = proof.binding_first[local + 1];
        for (uint32_t at = first + (first < end); valid && at < end; at += 1)
        {
            const uint32_t before = debug->bindings[proof.binding_order[at - 1]].entry;
            const uint32_t after = debug->bindings[proof.binding_order[at]].entry;
            valid = after != BUSTER_IR_NONE && (before == BUSTER_IR_NONE || before < after);
        }
        if (valid && local < parameters && first < end && (debug->binding_flags[local] & BUSTER_IR_DEBUG_BIND_ENTRY) != 0)
        {
            const IrDebugBinding initial = debug->bindings[proof.binding_order[first]];
            valid = initial.entry == BUSTER_IR_NONE;
            proof.initial[local] = initial.kind == IR_DEBUG_BIND_VALUE ? initial.value : BUSTER_IR_NONE;
            const AbiLocation location = function->parameters[local];
            const uint32_t reg = location.registers[0];
            const bool floating = tier0_class_floating(function->local_classes[local]);
            const bool carrier = floating ? (reg & 0xf0) == BUSTER_ABI_REGISTER_VECTOR : reg < 16;
            const uint32_t place = floating ? 16 + (reg & 15) : reg;
            const bool supported = valid && function->interrupt == 0 && !function->returns_twice && location.kind == BUSTER_ABI_LOCATION_REGISTERS &&
                                   location.registers[1] == BUSTER_ABI_REGISTER_NONE && carrier && place < 32 &&
                                   (location.flags & ~(BUSTER_ABI_FLAG_SIGN | BUSTER_ABI_FLAG_ZERO)) == 0 && ((BUSTER_TIER0_ARGUMENT_REGISTERS >> place) & 1) != 0;
            if (supported)
            {
                const uint32_t value = initial.kind == IR_DEBUG_BIND_VALUE ? initial.value : nodes + local;
                valid = seed[place] == BUSTER_IR_NONE && proof.widths[value] == initial.bytes && proof.floating[value] == floating;
                if (valid)
                {
                    proof.initial[local] = value;
                    proof.needed[value] = 1;
                    seed[place] = value;
                    if (initial.anchor < nodes)
                    {
                        proof.completion[initial.anchor] = 0;
                    }
                }
            }
        }
    }
    valid = valid && check_tier1_debug_value_homes(emitted, scratch, &proof);
    valid = valid && bindings < UINT32_MAX - proof.carrier_count;
    proof.transfers = valid ? ir_allocate(scratch, (uint64_t)count + 1, sizeof(CheckTier1DebugValueTransfer), &valid) : NULL;
    uint32_t* places = valid ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    uint32_t* definitions = valid ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    uint32_t* targets = valid ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    uint32_t* block_of = valid ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    uint8_t* cuts = valid ? ir_allocate(scratch, (uint64_t)count + 1, 1, &valid) : NULL;
    if (valid)
    {
        memset(places, 0xff, ((uint64_t)count + 1) * 4);
        memset(definitions, 0xff, ((uint64_t)count + 1) * 4);
        memset(targets, 0xff, ((uint64_t)count + 1) * 4);
        memset(cuts, 0, (uint64_t)count + 1);
        cuts[0] = 1;
        cuts[count] = 1;
    }
    for (uint32_t r = 0; valid && r < count; r += 1)
    {
        const uint32_t old = record_order != NULL ? record_order[r] : r;
        valid = old < count && places[old] == BUSTER_IR_NONE;
        if (valid)
        {
            places[old] = r;
            proof.transfers[r] = check_tier1_debug_value_transfer(&proof, emitted, old);
        }
    }
    // The final order may interleave independent records. A checked result opens only after every dependency record
    // completed, with their own order preserved and the last concrete write to its carrier still inside its kernel.
    // An unrelated overwrite between the last result write and the sequence's end must not be undone by our seed.
    for (uint32_t node = 0; valid && emitted->debug_definitions != NULL && node < nodes; node += 1)
    {
        const Tier1DebugDefinition definition = emitted->debug_definitions[node];
        if (definition.first != BUSTER_IR_NONE || definition.end != BUSTER_IR_NONE)
        {
            valid = definition.first < definition.end && definition.end <= count && definition.reg < 16 && definition.class <= BUSTER_TIER1_ALLOCATE_VECTOR;
            bool ordered = valid && proof.widths[node] != 0 && definition.bytes >= proof.widths[node] &&
                           proof.floating[node] == (definition.class == BUSTER_TIER1_ALLOCATE_VECTOR);
            uint32_t previous = BUSTER_IR_NONE;
            for (uint32_t r = definition.first; ordered && r < definition.end; r += 1)
            {
                ordered = previous == BUSTER_IR_NONE || previous < places[r];
                previous = places[r];
            }
            if (ordered)
            {
                definitions[previous] = definitions[previous] == BUSTER_IR_NONE ? node : UINT32_MAX - 1;
            }
        }
    }
    uint32_t writers[32];
    memset(writers, 0xff, sizeof(writers));
    for (uint32_t r = 0; valid && r < count; r += 1)
    {
        uint32_t killed = proof.transfers[r].killed;
        while (killed != 0)
        {
            writers[(uint32_t)__builtin_ctz(killed)] = r;
            killed &= killed - 1;
        }
        const uint32_t node = definitions[r];
        if (node < nodes)
        {
            const Tier1DebugDefinition definition = emitted->debug_definitions[node];
            const uint32_t carrier = definition.reg + (definition.class == BUSTER_TIER1_ALLOCATE_VECTOR ? 16 : 0);
            const uint32_t written = writers[carrier];
            const uint32_t old = written < count ? record_order != NULL ? record_order[written] : written : BUSTER_IR_NONE;
            if (old >= definition.first && old < definition.end)
            {
                proof.transfers[r].definition = node;
                proof.transfers[r].defined_at = carrier;
                proof.completion[node] = r + 1;
            }
        }
    }
    for (uint32_t local = 0; valid && local < locals; local += 1)
    {
        uint32_t limit = count;
        for (uint32_t at = proof.binding_first[local + 1]; at > proof.binding_first[local];)
        {
            const uint32_t b = proof.binding_order[--at];
            const IrDebugBinding binding = debug->bindings[b];
            proof.binding_end[b] = limit;
            // An absent/early next anchor cannot justify resurrecting the old assignment after reordering. Dropping
            // that older range is conservative; line origins alone never establish that an old value is still current.
            const uint32_t completed = binding.anchor < nodes ? proof.completion[binding.anchor] : BUSTER_IR_NONE;
            limit = completed == BUSTER_IR_NONE ? 0 : completed < limit ? completed : limit;
        }
    }
    proof.machine = valid && count != 0;
    for (uint32_t k = 0; valid && k < emitted->code.branch_count; k += 1)
    {
        const uint32_t branch = emitted->code.branches[k];
        const uint32_t target = emitted->code.targets[k];
        valid = branch < count && target <= count;
        valid = valid && targets[branch] == BUSTER_IR_NONE && (record_order == NULL || record_order[branch] == branch);
        if (valid)
        {
            targets[branch] = target;
            cuts[branch + 1] = 1;
            cuts[target] = 1;
        }
    }
    for (uint32_t r = 0; valid && r < count; r += 1)
    {
        const uint32_t control = proof.transfers[r].control;
        const bool branch = control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_BRANCH;
        valid = targets[r] == BUSTER_IR_NONE || branch;
        proof.machine &= control != BUSTER_X86_CONTROL_UNKNOWN && control != BUSTER_X86_CONTROL_TRAP && (!branch || targets[r] != BUSTER_IR_NONE);
        if (branch || control == BUSTER_X86_CONTROL_RETURN)
        {
            cuts[r + 1] = 1;
        }
    }
    for (uint32_t r = 0; valid && r < count; r += 1)
    {
        proof.block_count += cuts[r] != 0;
        block_of[r] = proof.block_count - 1;
    }
    proof.first = valid ? ir_allocate(scratch, (uint64_t)proof.block_count + 1, 4, &valid) : NULL;
    proof.blocks = valid ? ir_allocate(scratch, (uint64_t)proof.block_count + 1, sizeof(CheckTier1DebugValueBlock), &valid) : NULL;
    proof.state = valid ? ir_allocate(scratch, proof.carrier_count, 4, &valid) : NULL;
    proof.winner = valid ? ir_allocate(scratch, (uint64_t)proof.value_count + 1, 4, &valid) : NULL;
    proof.active = valid ? ir_allocate(scratch, (uint64_t)proof.local_count + 1, 4, &valid) : NULL;
    proof.begins = valid ? ir_allocate(scratch, (uint64_t)proof.local_count + 1, 4, &valid) : NULL;
    proof.selected = valid ? ir_allocate(scratch, (uint64_t)proof.local_count + 1, 4, &valid) : NULL;
    proof.kill_first = valid ? ir_allocate(scratch, (uint64_t)count + 2, 4, &valid) : NULL;
    proof.kills = valid ? ir_allocate(scratch, (uint64_t)bindings + 1, 4, &valid) : NULL;
    proof.live.heads = valid ? ir_allocate(scratch, (uint64_t)proof.value_count + 1, 4, &valid) : NULL;
    proof.live.next = valid ? ir_allocate(scratch, proof.carrier_count, 4, &valid) : NULL;
    proof.live.previous = valid ? ir_allocate(scratch, proof.carrier_count, 4, &valid) : NULL;
    proof.live.known = valid ? ir_allocate(scratch, proof.carrier_count, 4, &valid) : NULL;
    proof.live.known_at = valid ? ir_allocate(scratch, proof.carrier_count, 4, &valid) : NULL;
    proof.live.users = valid ? ir_allocate(scratch, (uint64_t)proof.value_count + 1, 4, &valid) : NULL;
    proof.live.user_next = valid ? ir_allocate(scratch, (uint64_t)proof.local_count + 1, 4, &valid) : NULL;
    proof.live.user_previous = valid ? ir_allocate(scratch, (uint64_t)proof.local_count + 1, 4, &valid) : NULL;
    proof.live.user_value = valid ? ir_allocate(scratch, (uint64_t)proof.local_count + 1, 4, &valid) : NULL;
    proof.live.changed_values = valid ? ir_allocate(scratch, (uint64_t)proof.value_count + 1, 4, &valid) : NULL;
    proof.live.value_queued = valid ? ir_allocate(scratch, (uint64_t)proof.value_count + 1, 1, &valid) : NULL;
    proof.live.changed_locals = valid ? ir_allocate(scratch, (uint64_t)proof.local_count + 1, 4, &valid) : NULL;
    proof.live.local_queued = valid ? ir_allocate(scratch, (uint64_t)proof.local_count + 1, 1, &valid) : NULL;
    uint32_t* queue = valid && proof.machine ? ir_allocate(scratch, proof.block_count, 4, &valid) : NULL;
    uint8_t* queued = valid && proof.machine ? ir_allocate(scratch, proof.block_count, 1, &valid) : NULL;
    if (valid)
    {
        memset(proof.kill_first, 0, ((uint64_t)count + 2) * 4);
        for (uint32_t b = 0; b < bindings; b += 1)
        {
            if (proof.local_index[debug->bindings[b].local] != BUSTER_IR_NONE && proof.binding_end[b] < count)
            {
                proof.kill_first[proof.binding_end[b] + 1] += 1;
            }
        }
        for (uint32_t r = 0; r <= count; r += 1)
        {
            proof.kill_first[r + 1] += proof.kill_first[r];
            places[r] = proof.kill_first[r];
        }
        for (uint32_t b = 0; b < bindings; b += 1)
        {
            if (proof.local_index[debug->bindings[b].local] != BUSTER_IR_NONE && proof.binding_end[b] < count)
            {
                proof.kills[places[proof.binding_end[b]]++] = proof.local_index[debug->bindings[b].local];
            }
        }
        memset(proof.blocks, 0, ((uint64_t)proof.block_count + 1) * sizeof(CheckTier1DebugValueBlock));
        for (uint32_t r = 0; r < count; r += 1)
        {
            if (cuts[r] != 0)
            {
                proof.first[block_of[r]] = r;
            }
        }
        proof.first[proof.block_count] = count;
        block_of[count] = proof.block_count;
    }
    if (valid && proof.machine)
    {
        memset(proof.state, 0xff, (uint64_t)proof.carrier_count * 4);
        memcpy(proof.state, seed, sizeof(seed));
        bool changed = false;
        valid = check_tier1_debug_value_meet(&proof, proof.state, vector, scratch, proof.blocks, &changed);
        memset(queued, 0, proof.block_count);
        queued[0] = 1;
        queue[0] = 0;
        uint32_t pending = 1;
        uint32_t head = 0;
        uint32_t tail = proof.block_count == 1 ? 0 : 1;
        while (valid && pending != 0)
        {
            const uint32_t block = queue[head];
            head = head + 1 == proof.block_count ? 0 : head + 1;
            pending -= 1;
            queued[block] = 0;
            check_tier1_debug_value_restore(&proof, proof.blocks + block, proof.state);
            for (uint32_t r = proof.first[block]; r < proof.first[block + 1]; r += 1)
            {
                check_tier1_debug_value_step(&proof, proof.transfers[r], vector, proof.state);
            }
            const uint32_t last = proof.first[block + 1] - 1;
            const uint32_t control = proof.transfers[last].control;
            const bool fallthrough = control != BUSTER_X86_CONTROL_JUMP && control != BUSTER_X86_CONTROL_RETURN;
            const uint32_t successors[2] = { targets[last], fallthrough ? proof.first[block + 1] : BUSTER_IR_NONE };
            for (uint32_t edge = 0; valid && edge < 2; edge += 1)
            {
                if (successors[edge] < count)
                {
                    const uint32_t destination = block_of[successors[edge]];
                    valid = check_tier1_debug_value_meet(&proof, proof.state, vector, scratch, proof.blocks + destination, &changed);
                    if (valid && changed && queued[destination] == 0)
                    {
                        queue[tail] = destination;
                        tail = tail + 1 == proof.block_count ? 0 : tail + 1;
                        pending += 1;
                        queued[destination] = 1;
                    }
                }
            }
        }
    }
    *result = valid ? proof : (CheckTier1DebugValues){ 0 };
    return valid;
}

static uint32_t check_tier1_debug_value_binding(const CheckTier1DebugValues* proof, uint32_t local, uint32_t entry, bool source)
{
    const IrDebugInfo* debug = proof->ir->debug;
    const uint32_t begin = proof->binding_first[local];
    const uint32_t end = proof->binding_first[local + 1];
    uint32_t first = begin;
    uint32_t last = end;
    while (first < last)
    {
        const uint32_t middle = first + (last - first) / 2;
        const IrDebugBinding binding = debug->bindings[proof->binding_order[middle]];
        const bool before = binding.entry == BUSTER_IR_NONE || (source && binding.entry < entry);
        first = before ? middle + 1 : first;
        last = before ? last : middle;
    }
    const bool assigning = source && first < end && debug->bindings[proof->binding_order[first]].entry == entry;
    const uint32_t binding = first != begin && !assigning ? proof->binding_order[first - 1] : BUSTER_IR_NONE;
    return binding;
}

static uint32_t check_tier1_debug_value_priority(uint32_t carrier)
{
    return carrier < 16 && ((BUSTER_CHECK_TIER1_DEBUG_PRESERVED >> carrier) & 1) != 0 ? 0 : carrier >= 32 ? 1 : 2;
}

static void check_tier1_debug_value_range(const CheckTier1DebugValues* proof, uint32_t local, uint32_t selected, uint32_t begin, uint32_t end, EmittedLocalRange* output)
{
    if (output != NULL)
    {
        if (selected < proof->carrier_count)
        {
            const Tier0Home home = selected < 32 ? (Tier0Home){ .reg = (uint8_t)(selected & 15), .is_register = true } : proof->homes[selected - 32].home;
            *output = (EmittedLocalRange){ .home = home, .local = local, .begin = begin, .end = end };
        }
        else
        {
            const IrDebugBinding binding = proof->ir->debug->bindings[selected - proof->carrier_count];
            *output = (EmittedLocalRange){ .value = binding.literal, .local = local, .begin = begin, .end = end, .value_bytes = binding.bytes };
        }
    }
}

static void check_tier1_debug_value_changed(CheckTier1DebugValueLive* live, uint32_t value)
{
    if (value != BUSTER_IR_NONE && live->value_queued[value] == 0)
    {
        live->value_queued[value] = 1;
        live->changed_values[live->value_count++] = value;
    }
}

static void check_tier1_debug_local_changed(CheckTier1DebugValueLive* live, uint32_t local)
{
    if (local != BUSTER_IR_NONE && live->local_queued[local] == 0)
    {
        live->local_queued[local] = 1;
        live->changed_locals[live->local_count++] = local;
    }
}

// Intrusive reverse lists make changes local: a concrete register/row write touches its old and new SSA identities,
// and each identity names only its actual remaining carriers and the source variables currently bound to it.
static void check_tier1_debug_value_change(const CheckTier1DebugValues* proof, CheckTier1DebugValueLive* live, uint32_t place, uint32_t value)
{
    const uint32_t before = proof->state[place];
    if (before != value)
    {
        if (before != BUSTER_IR_NONE)
        {
            const uint32_t previous = live->previous[place];
            const uint32_t next = live->next[place];
            if (previous != BUSTER_IR_NONE)
            {
                live->next[previous] = next;
            }
            else
            {
                live->heads[before] = next;
            }
            if (next != BUSTER_IR_NONE)
            {
                live->previous[next] = previous;
            }
            if (value == BUSTER_IR_NONE)
            {
                const uint32_t index = live->known_at[place];
                const uint32_t last = live->known[--live->known_count];
                live->known[index] = last;
                live->known_at[last] = index;
                live->known_at[place] = BUSTER_IR_NONE;
            }
        }
        if (value != BUSTER_IR_NONE)
        {
            const uint32_t head = live->heads[value];
            live->previous[place] = BUSTER_IR_NONE;
            live->next[place] = head;
            if (head != BUSTER_IR_NONE)
            {
                live->previous[head] = place;
            }
            live->heads[value] = place;
            if (before == BUSTER_IR_NONE)
            {
                live->known_at[place] = live->known_count;
                live->known[live->known_count++] = place;
            }
        }
        proof->state[place] = value;
        check_tier1_debug_value_changed(live, before);
        check_tier1_debug_value_changed(live, value);
    }
}

static void check_tier1_debug_value_user(CheckTier1DebugValueLive* live, uint32_t local, uint32_t value)
{
    const uint32_t before = live->user_value[local];
    if (before != value)
    {
        if (before != BUSTER_IR_NONE)
        {
            const uint32_t previous = live->user_previous[local];
            const uint32_t next = live->user_next[local];
            if (previous != BUSTER_IR_NONE)
            {
                live->user_next[previous] = next;
            }
            else
            {
                live->users[before] = next;
            }
            if (next != BUSTER_IR_NONE)
            {
                live->user_previous[next] = previous;
            }
        }
        if (value != BUSTER_IR_NONE)
        {
            const uint32_t head = live->users[value];
            live->user_previous[local] = BUSTER_IR_NONE;
            live->user_next[local] = head;
            if (head != BUSTER_IR_NONE)
            {
                live->user_previous[head] = local;
            }
            live->users[value] = local;
        }
        live->user_value[local] = value;
    }
}

// The publication twin of value_step updates reverse lists only for the places actually written. Its input transfer
// is the same decoded recipe used by the CFG proof. A memory-wide kill visits current facts, not every frame slot.
static void check_tier1_debug_value_follow(const CheckTier1DebugValues* proof, CheckTier1DebugValueLive* live, CheckTier1DebugValueTransfer transfer)
{
    const uint32_t copied = transfer.from < proof->carrier_count ? proof->state[transfer.from] : BUSTER_IR_NONE;
    uint32_t killed = transfer.killed;
    while (killed != 0)
    {
        check_tier1_debug_value_change(proof, live, (uint32_t)__builtin_ctz(killed), BUSTER_IR_NONE);
        killed &= killed - 1;
    }
    for (uint32_t k = 0; (transfer.kill_memory || transfer.kill_addressable) && k < live->known_count;)
    {
        const uint32_t place = live->known[k];
        if (place >= 32 && (transfer.kill_memory || proof->homes[place - 32].addressable))
        {
            check_tier1_debug_value_change(proof, live, place, BUSTER_IR_NONE);
        }
        else
        {
            k += 1;
        }
    }
    for (int64_t at = (int64_t)transfer.memory_at - 7; !transfer.kill_memory && transfer.memory_bytes != 0 && at < (int64_t)transfer.memory_at + transfer.memory_bytes; at += 1)
    {
        const uint32_t home = check_tier1_debug_value_home(proof, at);
        if (home != BUSTER_IR_NONE && at + proof->homes[home].bytes > transfer.memory_at)
        {
            check_tier1_debug_value_change(proof, live, 32 + home, BUSTER_IR_NONE);
        }
    }
    const bool class = transfer.to >= 32 || (copied < proof->value_count && proof->floating[copied] == (transfer.to >= 16));
    const bool fits = transfer.to < 32 || (transfer.to < proof->carrier_count && copied < proof->value_count && proof->widths[copied] <= proof->homes[transfer.to - 32].bytes);
    if (copied < proof->value_count && transfer.to < proof->carrier_count && proof->widths[copied] <= transfer.bytes && class && fits)
    {
        check_tier1_debug_value_change(proof, live, transfer.to, copied);
    }
    if (transfer.definition < proof->value_count && transfer.defined_at < 32 && proof->needed[transfer.definition] != 0)
    {
        check_tier1_debug_value_change(proof, live, transfer.defined_at, transfer.definition);
    }
}

static uint32_t check_tier1_debug_value_event_cursor(const CheckTier1DebugValues* proof, uint32_t entry, bool source)
{
    uint32_t first = 0;
    uint32_t end = source ? proof->event_count : 0;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        const bool before = proof->ir->debug->bindings[proof->events[middle]].entry < entry;
        first = before ? middle + 1 : first;
        end = before ? end : middle;
    }
    return first;
}

static void check_tier1_debug_value_winners(const CheckTier1DebugValues* proof, CheckTier1DebugValueLive* live, bool call)
{
    for (uint32_t k = 0; k < live->value_count; k += 1)
    {
        const uint32_t value = live->changed_values[k];
        uint32_t chosen = BUSTER_IR_NONE;
        for (uint32_t carrier = live->heads[value]; carrier != BUSTER_IR_NONE; carrier = live->next[carrier])
        {
            const bool suspended = !call || (carrier < 16 && ((BUSTER_CHECK_TIER1_DEBUG_PRESERVED >> carrier) & 1) != 0) ||
                                   (carrier >= 32 && !proof->homes[carrier - 32].addressable);
            const uint32_t priority = check_tier1_debug_value_priority(carrier);
            const uint32_t current = chosen != BUSTER_IR_NONE ? check_tier1_debug_value_priority(chosen) : UINT32_MAX;
            if (suspended && (priority < current || (priority == current && carrier < chosen)))
            {
                chosen = carrier;
            }
        }
        if (chosen != proof->winner[value])
        {
            proof->winner[value] = chosen;
            for (uint32_t local = live->users[value]; local != BUSTER_IR_NONE; local = live->user_next[local])
            {
                check_tier1_debug_local_changed(live, local);
            }
        }
        live->value_queued[value] = 0;
    }
    live->value_count = 0;
}

// The machine row supplies only current carriers. The source row chooses the latest completed assignment whose
// origin is visible here; its next RHS completion additionally kills the old binding even if scheduling moves an
// older source origin after that completion. Assignment instructions themselves and unknown local scopes are gaps.
// An initial parameter is already a source value at entry, including before the first source statement.
static uint64_t check_tier1_debug_value_ranges(const CheckTier1DebugValues* proof, const uint32_t* entries, const uint8_t* safe,
                                             uint32_t body, uint32_t epilogue, bool vector, EmittedLocalRange* output)
{
    const IrDebugInfo* debug = proof->ir->debug;
    const uint32_t locals = proof->local_count;
    const uint32_t carriers = proof->carrier_count;
    const uint32_t values = proof->value_count;
    CheckTier1DebugValueLive live = proof->live;
    live.known_count = 0;
    live.value_count = 0;
    live.local_count = 0;
    uint64_t ranges = 0;
    memset(proof->state, 0xff, (uint64_t)carriers * 4);
    memset(proof->winner, 0xff, ((uint64_t)values + 1) * 4);
    memset(proof->active, 0xff, ((uint64_t)locals + 1) * 4);
    memset(proof->selected, 0xff, ((uint64_t)locals + 1) * 4);
    memset(live.heads, 0xff, ((uint64_t)values + 1) * 4);
    memset(live.users, 0xff, ((uint64_t)values + 1) * 4);
    memset(live.known_at, 0xff, (uint64_t)carriers * 4);
    memset(live.user_value, 0xff, ((uint64_t)locals + 1) * 4);
    memset(live.value_queued, 0, (uint64_t)values + 1);
    memset(live.local_queued, 0, (uint64_t)locals + 1);
    for (uint32_t k = 0; k < locals; k += 1)
    {
        check_tier1_debug_local_changed(&live, k);
    }
    uint32_t cursor = 0;
    uint32_t assigning = BUSTER_IR_NONE;
    uint32_t previous_entry = BUSTER_IR_NONE;
    bool previous_source = false;
    for (uint32_t block = 0; block < proof->block_count; block += 1)
    {
        while (live.known_count != 0)
        {
            check_tier1_debug_value_change(proof, &live, live.known[live.known_count - 1], BUSTER_IR_NONE);
        }
        const CheckTier1DebugValueBlock incoming = proof->blocks[block];
        const bool reached = proof->machine && incoming.reached;
        for (uint32_t f = 0; reached && f < incoming.count; f += 1)
        {
            check_tier1_debug_value_change(proof, &live, incoming.facts[f].place, incoming.facts[f].value);
        }
        for (uint32_t r = proof->first[block]; r < proof->first[block + 1]; r += 1)
        {
            const bool source = r >= body && r < epilogue && safe[r] != 0;
            const uint32_t entry = entries[r];
            if (r == 0 || entry != previous_entry || source != previous_source)
            {
                const uint32_t next = check_tier1_debug_value_event_cursor(proof, entry, source);
                const uint32_t first = cursor < next ? cursor : next;
                const uint32_t end = cursor < next ? next : cursor;
                for (uint32_t at = first; at < end; at += 1)
                {
                    check_tier1_debug_local_changed(&live, proof->local_index[debug->bindings[proof->events[at]].local]);
                }
                const uint32_t own = source && next < proof->event_count && debug->bindings[proof->events[next]].entry == entry ?
                                     proof->local_index[debug->bindings[proof->events[next]].local] : BUSTER_IR_NONE;
                check_tier1_debug_local_changed(&live, assigning);
                check_tier1_debug_local_changed(&live, own);
                assigning = own;
                cursor = next;
                previous_entry = entry;
                previous_source = source;
            }
            for (uint32_t at = proof->kill_first[r]; at < proof->kill_first[r + 1]; at += 1)
            {
                check_tier1_debug_local_changed(&live, proof->kills[at]);
            }
            const bool call = proof->transfers[r].control == BUSTER_X86_CONTROL_CALL;
            if (call)
            {
                uint32_t callers = 0;
                if (BUSTER_VECTOR_PATH(vector))
                {
                    callers = _mm512_cmpneq_epi32_mask(_mm512_loadu_si512(proof->state), _mm512_set1_epi32(-1));
                    callers |= (uint32_t)_mm512_cmpneq_epi32_mask(_mm512_loadu_si512(proof->state + 16), _mm512_set1_epi32(-1)) << 16;
                    callers &= BUSTER_TIER0_GENERAL_CALLER_SAVED | BUSTER_TIER0_XMM_ALL;
                }
                else
                {
                    for (uint32_t reg = 0; reg < 32; reg += 1)
                    {
                        callers |= (uint32_t)(proof->state[reg] != BUSTER_IR_NONE) << reg;
                    }
                    callers &= BUSTER_TIER0_GENERAL_CALLER_SAVED | BUSTER_TIER0_XMM_ALL;
                }
                while (callers != 0)
                {
                    check_tier1_debug_value_changed(&live, proof->state[(uint32_t)__builtin_ctz(callers)]);
                    callers &= callers - 1;
                }
                for (uint32_t k = 0; k < live.known_count; k += 1)
                {
                    const uint32_t place = live.known[k];
                    if (place >= 32 && proof->homes[place - 32].addressable)
                    {
                        check_tier1_debug_value_changed(&live, proof->state[place]);
                    }
                }
            }
            check_tier1_debug_value_winners(proof, &live, call);
            for (uint32_t at = 0; at < live.local_count; at += 1)
            {
                const uint32_t k = live.changed_locals[at];
                const uint32_t local = proof->locals[k];
                uint32_t selected = source || local < proof->function->parameter_count ? check_tier1_debug_value_binding(proof, local, entry, source) : BUSTER_IR_NONE;
                selected = selected < debug->binding_count && r < proof->binding_end[selected] ? selected : BUSTER_IR_NONE;
                uint32_t value = BUSTER_IR_NONE;
                uint32_t chosen = BUSTER_IR_NONE;
                if (selected != BUSTER_IR_NONE)
                {
                    const IrDebugBinding binding = debug->bindings[selected];
                    const bool initial = local < proof->function->parameter_count && binding.entry == BUSTER_IR_NONE;
                    value = initial ? proof->initial[local] : binding.kind == IR_DEBUG_BIND_VALUE ? binding.value : BUSTER_IR_NONE;
                    if (value < values)
                    {
                        chosen = proof->winner[value];
                    }
                    else if (!initial && binding.kind == IR_DEBUG_BIND_CONSTANT && source)
                    {
                        chosen = carriers + selected;
                    }
                }
                check_tier1_debug_value_user(&live, k, value);
                proof->selected[k] = selected;
                if (chosen != proof->active[k])
                {
                    if (proof->active[k] != BUSTER_IR_NONE)
                    {
                        check_tier1_debug_value_range(proof, local, proof->active[k], proof->begins[k], r, output != NULL ? output + ranges : NULL);
                        ranges += 1;
                    }
                    proof->active[k] = chosen;
                    proof->begins[k] = r;
                }
                live.local_queued[k] = 0;
            }
            live.local_count = 0;
            if (reached)
            {
                check_tier1_debug_value_follow(proof, &live, proof->transfers[r]);
            }
        }
    }
    for (uint32_t k = 0; k < locals; k += 1)
    {
        if (proof->active[k] != BUSTER_IR_NONE)
        {
            check_tier1_debug_value_range(proof, proof->locals[k], proof->active[k], proof->begins[k], proof->first[proof->block_count], output != NULL ? output + ranges : NULL);
            ranges += 1;
        }
    }
    return ranges;
}

