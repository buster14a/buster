// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's source origins and proven local locations (#1114). Include after emitted_code.c and tier1_emit.c.
// The IR sidecar names original typed entries and locals; the emitter names the IR node that wrote each record.
// Neither an instruction number nor an allocation interval is a source-variable identity. The post-scheduler's
// new-to-old permutation carries those record origins without adding a scheduling cut or changing code under -g.
//
// Retained objects use the actual frame's base and byte offsets. A shared physical slot additionally needs a proved
// source lifetime and concrete initialization/overwrite boundaries. Prologue, entry copies, epilogues, returns and
// unknown origins are gaps. A body write to the frame base also makes these homes unavailable. Unsupported storage
// loses only its location; malformed metadata and capacity failures fail transactionally.
//
// Immutable incoming scalars use a separate decoded machine CFG proof: ABI carriers, exact register copies and
// intersections at all predecessors, including backedges. Only proven preserved carriers survive a call's PC;
// spills, arbitrary computation and indirect control do not invent facts. These parameter ranges also cover entry
// and prologue PCs where a carrier is proved, regardless of source origins. Immutable literal locals need an exact
// original typed initialization and later known source origins; they never extend through a synthetic source gap.
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

#define BUSTER_CHECK_TIER1_DEBUG_CARRIERS 32
#define BUSTER_CHECK_TIER1_DEBUG_UNKNOWN UINT8_MAX
#define BUSTER_CHECK_TIER1_DEBUG_PRESERVED \
    (1u << X86_REGISTER_RBX | 1u << X86_REGISTER_RBP | 1u << X86_REGISTER_R12 | 1u << X86_REGISTER_R13 | 1u << X86_REGISTER_R14 | 1u << X86_REGISTER_R15)

typedef struct CheckTier1DebugTransfer CheckTier1DebugTransfer;
struct CheckTier1DebugTransfer
{
    uint32_t killed;
    uint8_t from;
    uint8_t to;
    uint8_t bytes;
    uint8_t control;
};

typedef struct CheckTier1DebugParameters CheckTier1DebugParameters;
struct CheckTier1DebugParameters
{
    IrDebugScalar values[BUSTER_CHECK_TIER1_DEBUG_CARRIERS];
    uint32_t count;
    CheckTier1DebugTransfer* transfers;
    uint32_t* first;
    uint8_t* states;
    uint8_t* reached;
    uint32_t block_count;
};

typedef struct CheckTier1DebugConstants CheckTier1DebugConstants;
struct CheckTier1DebugConstants
{
    const IrDebugScalar* values;
    uint32_t* order;
    uint32_t* first;
    uint32_t count;
};

// Decode exact low-byte copies, not arithmetic or reconstructed SSA values. Each carrier names one admitted
// immutable parameter; ordinary writes kill the whole carrier. Keeping fewer bytes than that parameter is a kill.
// GPR and XMM facts never cross classes: the DWARF home describes its source type's register class.
static CheckTier1DebugTransfer check_tier1_debug_transfer(const X86Instruction* record)
{
    uint64_t read = 0;
    uint64_t written = 0;
    x86_instruction_effects_scalar(record, &read, &written);
    CheckTier1DebugTransfer result = {
        .killed = (uint32_t)written,
        .control = (uint8_t)x86_effects_control(x86_effects(record->form)),
    };
    if (result.control == BUSTER_X86_CONTROL_CALL)
    {
        result.killed |= BUSTER_TIER0_GENERAL_CALLER_SAVED | BUSTER_TIER0_XMM_ALL;
    }
    const bool plain = record->mode == BUSTER_X86_REGISTER_DIRECT && record->reg < 16 && record->rm < 16 &&
                       record->index == 0 && record->vvvv == 0 && record->decorations == 0 && record->displacement == 0 && record->immediate == 0;
    if (plain)
    {
        bool load = true;
        uint32_t base = 0;
        switch (record->form)
        {
            case X86_FORM_MOV_R64_RM64: result.bytes = 8; break;
            case X86_FORM_MOV_RM64_R64: result.bytes = 8; load = false; break;
            case X86_FORM_MOV_R32_RM32: result.bytes = 4; break;
            case X86_FORM_MOV_RM32_R32: result.bytes = 4; load = false; break;
            case X86_FORM_MOVZX_R32_RM8:
            case X86_FORM_MOVZX_R64_RM8:
            case X86_FORM_MOVSX_R32_RM8:
            case X86_FORM_MOVSX_R64_RM8: result.bytes = 1; break;
            case X86_FORM_MOVZX_R32_RM16:
            case X86_FORM_MOVZX_R64_RM16:
            case X86_FORM_MOVSX_R32_RM16:
            case X86_FORM_MOVSX_R64_RM16: result.bytes = 2; break;
            case X86_FORM_MOVSXD_R64_RM32: result.bytes = 4; break;
            case X86_FORM_MOVSS_X_XM32: result.bytes = 4; base = 16; break;
            case X86_FORM_MOVSS_XM32_X: result.bytes = 4; base = 16; load = false; break;
            case X86_FORM_MOVSD_X_XM64: result.bytes = 8; base = 16; break;
            case X86_FORM_MOVSD_XM64_X: result.bytes = 8; base = 16; load = false; break;
            case X86_FORM_MOVAPS_X_XM:
            case X86_FORM_MOVUPS_X_XM:
            case X86_FORM_VMOVDQA_X_XM128:
            case X86_FORM_VMOVDQU_X_XM128:
            case X86_FORM_VMOVDQA64_X_XM128:
            case X86_FORM_VMOVDQU32_X_XM128:
            case X86_FORM_VMOVQ_X_XM64:
            case X86_FORM_VMOVQ_X_XM64_EVEX: result.bytes = 8; base = 16; break;
            case X86_FORM_MOVAPS_XM_X:
            case X86_FORM_MOVUPS_XM_X:
            case X86_FORM_VMOVDQU_XM128_X:
            case X86_FORM_VMOVDQU32_XM128_X: result.bytes = 8; base = 16; load = false; break;
            default: break;
        }
        result.from = (uint8_t)(base + (load ? record->rm : record->reg));
        result.to = (uint8_t)(base + (load ? record->reg : record->rm));
    }
    return result;
}

// The dependency between machine instructions remains ordered. The bounded carrier set is the data-parallel part:
// every kill and every CFG meet operates on all 32 bytes together, with an exact scalar twin and no padded read.
static void check_tier1_debug_step(const CheckTier1DebugParameters* parameters, CheckTier1DebugTransfer transfer, bool vector, uint8_t* state)
{
    const uint32_t value = transfer.bytes != 0 ? state[transfer.from] : BUSTER_CHECK_TIER1_DEBUG_UNKNOWN;
    if (BUSTER_VECTOR_PATH(vector))
    {
        const __m512i before = _mm512_maskz_loadu_epi8(UINT32_MAX, state);
        const __m512i after = _mm512_mask_mov_epi8(before, transfer.killed, _mm512_set1_epi8(-1));
        _mm512_mask_storeu_epi8(state, UINT32_MAX, after);
    }
    else
    {
        uint32_t killed = transfer.killed;
        while (killed != 0)
        {
            const uint32_t reg = (uint32_t)__builtin_ctz(killed);
            state[reg] = BUSTER_CHECK_TIER1_DEBUG_UNKNOWN;
            killed &= killed - 1;
        }
    }
    if (value < parameters->count && parameters->values[value].bytes <= transfer.bytes)
    {
        state[transfer.to] = (uint8_t)value;
    }
}

static bool check_tier1_debug_meet(const uint8_t* incoming, bool vector, uint8_t* state)
{
    bool changed = false;
    if (BUSTER_VECTOR_PATH(vector))
    {
        const __m512i before = _mm512_maskz_loadu_epi8(UINT32_MAX, state);
        const __m512i other = _mm512_maskz_loadu_epi8(UINT32_MAX, incoming);
        const __mmask64 different = _mm512_cmpneq_epi8_mask(before, other) & UINT32_MAX;
        const __m512i after = _mm512_mask_mov_epi8(before, different, _mm512_set1_epi8(-1));
        changed = (_mm512_cmpneq_epi8_mask(before, after) & UINT32_MAX) != 0;
        _mm512_mask_storeu_epi8(state, UINT32_MAX, after);
    }
    else
    {
        for (uint32_t reg = 0; reg < BUSTER_CHECK_TIER1_DEBUG_CARRIERS; reg += 1)
        {
            const bool lost = state[reg] != BUSTER_CHECK_TIER1_DEBUG_UNKNOWN && state[reg] != incoming[reg];
            changed |= lost;
            state[reg] = lost ? BUSTER_CHECK_TIER1_DEBUG_UNKNOWN : state[reg];
        }
    }
    return changed;
}

// Build the concrete machine CFG, then intersect register provenance at every reachable predecessor. Unvisited
// blocks are not unknown facts: their first incoming edge initializes them; subsequent edges can only remove facts.
// Backedges therefore reach a fixed point before any location is published. No allocation interval participates.
// Indirect/external jumps and unmodelled control omit parameter locations, retaining the other debug information.
static bool check_tier1_debug_parameters(const Tier0Function* function, const IrDebugInfo* debug, const Tier1EmitResult* emitted,
                                        const uint32_t* record_order, bool vector, Arena* scratch, CheckTier1DebugParameters* result)
{
    const uint32_t count = emitted->code.count;
    const uint32_t branch_count = emitted->code.branch_count;
    CheckTier1DebugParameters candidate = { 0 };
    uint8_t seed[BUSTER_CHECK_TIER1_DEBUG_CARRIERS];
    memset(seed, BUSTER_CHECK_TIER1_DEBUG_UNKNOWN, sizeof(seed));
    bool valid = true;
    for (uint32_t k = 0; valid && k < debug->scalar_count; k += 1)
    {
        const IrDebugScalar value = debug->scalars[k];
        if (value.kind == IR_DEBUG_PARAMETER && function->interrupt == 0 && !function->returns_twice)
        {
            const AbiLocation location = function->parameters[value.local];
            const uint32_t reg = location.registers[0];
            const bool floating = tier0_class_floating(function->local_classes[value.local]);
            const bool carrier = floating ? (reg & 0xf0) == BUSTER_ABI_REGISTER_VECTOR : reg < 16;
            const uint32_t place = floating ? 16 + (reg & 15) : reg;
            const bool supported = location.kind == BUSTER_ABI_LOCATION_REGISTERS && location.registers[1] == BUSTER_ABI_REGISTER_NONE && carrier &&
                                   (location.flags & ~(BUSTER_ABI_FLAG_SIGN | BUSTER_ABI_FLAG_ZERO)) == 0 && place < BUSTER_CHECK_TIER1_DEBUG_CARRIERS &&
                                   ((BUSTER_TIER0_ARGUMENT_REGISTERS >> place) & 1) != 0;
            if (supported)
            {
                valid = candidate.count < BUSTER_CHECK_TIER1_DEBUG_CARRIERS && seed[place] == BUSTER_CHECK_TIER1_DEBUG_UNKNOWN;
                if (valid)
                {
                    seed[place] = (uint8_t)candidate.count;
                    candidate.values[candidate.count++] = value;
                }
            }
        }
    }
    bool supported = valid && candidate.count != 0 && count != 0;
    CheckTier1DebugTransfer* transfers = supported ? ir_allocate(scratch, count, sizeof(CheckTier1DebugTransfer), &valid) : NULL;
    uint8_t* cuts = supported && valid ? ir_allocate(scratch, (uint64_t)count + 1, 1, &valid) : NULL;
    uint32_t* targets = supported && valid ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    uint32_t* blocks = supported && valid ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    if (supported && valid)
    {
        memset(cuts, 0, (uint64_t)count + 1);
        memset(targets, 0xff, ((uint64_t)count + 1) * 4);
        cuts[0] = 1;
        cuts[count] = 1;
    }
    for (uint32_t k = 0; supported && valid && k < branch_count; k += 1)
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
    for (uint32_t r = 0; supported && valid && r < count; r += 1)
    {
        const uint32_t old = record_order != NULL ? record_order[r] : r;
        transfers[r] = check_tier1_debug_transfer(emitted->code.records + old);
        const uint32_t control = transfers[r].control;
        const bool branch = control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_BRANCH;
        supported = control != BUSTER_X86_CONTROL_UNKNOWN && control != BUSTER_X86_CONTROL_TRAP && (!branch || targets[r] != BUSTER_IR_NONE);
        valid = valid && (targets[r] == BUSTER_IR_NONE || branch);
        if (branch || control == BUSTER_X86_CONTROL_RETURN)
        {
            cuts[r + 1] = 1;
        }
    }
    uint32_t block_count = 0;
    for (uint32_t r = 0; supported && valid && r < count; r += 1)
    {
        block_count += cuts[r] != 0;
        blocks[r] = block_count - 1;
    }
    uint32_t* first = supported && valid ? ir_allocate(scratch, (uint64_t)block_count + 1, 4, &valid) : NULL;
    uint8_t* states = supported && valid ? ir_allocate(scratch, (uint64_t)block_count * BUSTER_CHECK_TIER1_DEBUG_CARRIERS, 1, &valid) : NULL;
    uint8_t* reached = supported && valid ? ir_allocate(scratch, block_count, 1, &valid) : NULL;
    uint8_t* queued = supported && valid ? ir_allocate(scratch, block_count, 1, &valid) : NULL;
    uint32_t* queue = supported && valid ? ir_allocate(scratch, block_count, 4, &valid) : NULL;
    if (supported && valid)
    {
        for (uint32_t r = 0; r < count; r += 1)
        {
            if (cuts[r] != 0)
            {
                first[blocks[r]] = r;
            }
        }
        first[block_count] = count;
        blocks[count] = block_count;
        memset(reached, 0, block_count);
        memset(queued, 0, block_count);
        memcpy(states, seed, sizeof(seed));
        reached[0] = 1;
        queued[0] = 1;
        queue[0] = 0;
        uint32_t pending = 1;
        uint32_t head = 0;
        uint32_t tail = block_count == 1 ? 0 : 1;
        while (pending != 0)
        {
            const uint32_t block = queue[head];
            head = head + 1 == block_count ? 0 : head + 1;
            pending -= 1;
            queued[block] = 0;
            uint8_t state[BUSTER_CHECK_TIER1_DEBUG_CARRIERS];
            memcpy(state, states + (uint64_t)block * BUSTER_CHECK_TIER1_DEBUG_CARRIERS, sizeof(state));
            for (uint32_t r = first[block]; r < first[block + 1]; r += 1)
            {
                check_tier1_debug_step(&candidate, transfers[r], vector, state);
            }
            const uint32_t last = first[block + 1] - 1;
            const uint32_t control = transfers[last].control;
            const bool fallthrough = control != BUSTER_X86_CONTROL_JUMP && control != BUSTER_X86_CONTROL_RETURN;
            const uint32_t successors[2] = { targets[last], fallthrough ? first[block + 1] : BUSTER_IR_NONE };
            for (uint32_t edge = 0; edge < 2; edge += 1)
            {
                if (successors[edge] < count)
                {
                    const uint32_t next = blocks[successors[edge]];
                    uint8_t* destination = states + (uint64_t)next * BUSTER_CHECK_TIER1_DEBUG_CARRIERS;
                    bool changed = reached[next] == 0;
                    if (changed)
                    {
                        memcpy(destination, state, sizeof(state));
                        reached[next] = 1;
                    }
                    else
                    {
                        changed = check_tier1_debug_meet(state, vector, destination);
                    }
                    if (changed && queued[next] == 0)
                    {
                        queue[tail] = next;
                        tail = tail + 1 == block_count ? 0 : tail + 1;
                        pending += 1;
                        queued[next] = 1;
                    }
                }
            }
        }
        candidate.transfers = transfers;
        candidate.first = first;
        candidate.states = states;
        candidate.reached = reached;
        candidate.block_count = block_count;
    }
    else
    {
        candidate.count = 0;
    }
    *result = valid ? candidate : (CheckTier1DebugParameters){ 0 };
    return valid;
}

// Count or write the proven register runs. At a call's own PC, a suspended caller cannot recover a caller-saved
// register from the callee, even when that register still held the value just before calling. Prefer preserved
// copies and publish no caller-saved carrier for the call record. The following state also kills those registers.
static uint64_t check_tier1_debug_parameter_ranges(const CheckTier1DebugParameters* parameters, bool vector, EmittedLocalRange* output)
{
    const uint32_t parameter_count = parameters->count;
    const uint32_t block_count = parameters->block_count;
    uint8_t active[BUSTER_CHECK_TIER1_DEBUG_CARRIERS];
    uint32_t begins[BUSTER_CHECK_TIER1_DEBUG_CARRIERS] = { 0 };
    memset(active, BUSTER_CHECK_TIER1_DEBUG_UNKNOWN, sizeof(active));
    uint64_t ranges = 0;
    for (uint32_t block = 0; parameter_count != 0 && block < block_count; block += 1)
    {
        uint8_t state[BUSTER_CHECK_TIER1_DEBUG_CARRIERS];
        if (parameters->reached[block] != 0)
        {
            memcpy(state, parameters->states + (uint64_t)block * BUSTER_CHECK_TIER1_DEBUG_CARRIERS, sizeof(state));
        }
        else
        {
            memset(state, BUSTER_CHECK_TIER1_DEBUG_UNKNOWN, sizeof(state));
        }
        for (uint32_t r = parameters->first[block]; r < parameters->first[block + 1]; r += 1)
        {
            const bool call = parameters->transfers[r].control == BUSTER_X86_CONTROL_CALL;
            for (uint32_t p = 0; p < parameter_count; p += 1)
            {
                uint32_t carriers = 0;
                if (BUSTER_VECTOR_PATH(vector))
                {
                    const __m256i values = _mm256_loadu_si256((const __m256i*)state);
                    carriers = _mm256_cmpeq_epi8_mask(values, _mm256_set1_epi8((int8_t)p));
                }
                else
                {
                    for (uint32_t reg = 0; reg < BUSTER_CHECK_TIER1_DEBUG_CARRIERS; reg += 1)
                    {
                        carriers |= (uint32_t)(state[reg] == p) << reg;
                    }
                }
                const uint32_t preserved = carriers & BUSTER_CHECK_TIER1_DEBUG_PRESERVED;
                carriers = call || preserved != 0 ? preserved : carriers;
                const uint32_t chosen = carriers != 0 ? (uint32_t)__builtin_ctz(carriers) : BUSTER_CHECK_TIER1_DEBUG_UNKNOWN;
                if (chosen != active[p])
                {
                    if (active[p] != BUSTER_CHECK_TIER1_DEBUG_UNKNOWN)
                    {
                        if (output != NULL)
                        {
                            output[ranges] = (EmittedLocalRange){
                                .home = { .reg = (uint8_t)(active[p] & 15), .is_register = true },
                                .local = parameters->values[p].local,
                                .begin = begins[p],
                                .end = r,
                            };
                        }
                        ranges += 1;
                    }
                    active[p] = (uint8_t)chosen;
                    begins[p] = r;
                }
            }
            check_tier1_debug_step(parameters, parameters->transfers[r], vector, state);
        }
    }
    for (uint32_t p = 0; p < parameter_count; p += 1)
    {
        if (active[p] != BUSTER_CHECK_TIER1_DEBUG_UNKNOWN)
        {
            if (output != NULL)
            {
                output[ranges] = (EmittedLocalRange){
                    .home = { .reg = (uint8_t)(active[p] & 15), .is_register = true },
                    .local = parameters->values[p].local,
                    .begin = begins[p],
                    .end = parameters->first[block_count],
                };
            }
            ranges += 1;
        }
    }
    return ranges;
}

// Constants are ordered by their exact initializing entry with a bounded counting pass. At a record, the admitted
// constants are a prefix of that order. Opening/closing only the changed prefix makes publication proportional to
// records and output ranges, with no local-by-record table or repeated scan for every source local.
static bool check_tier1_debug_constants(const IrDebugInfo* debug, Arena* scratch, CheckTier1DebugConstants* result)
{
    CheckTier1DebugConstants candidate = { .values = debug->scalars };
    for (uint32_t k = 0; k < debug->scalar_count; k += 1)
    {
        candidate.count += debug->scalars[k].kind == IR_DEBUG_CONSTANT;
    }
    bool valid = true;
    uint32_t* entries = candidate.count != 0 ? ir_allocate(scratch, (uint64_t)debug->entry_count + 1, 4, &valid) : NULL;
    candidate.order = valid && candidate.count != 0 ? ir_allocate(scratch, candidate.count, 4, &valid) : NULL;
    candidate.first = valid && candidate.count != 0 ? ir_allocate(scratch, candidate.count, 4, &valid) : NULL;
    if (valid && candidate.count != 0)
    {
        memset(entries, 0xff, ((uint64_t)debug->entry_count + 1) * 4);
        for (uint32_t k = 0; valid && k < debug->scalar_count; k += 1)
        {
            if (debug->scalars[k].kind == IR_DEBUG_CONSTANT)
            {
                const uint32_t entry = debug->scalars[k].entry;
                valid = entries[entry] == BUSTER_IR_NONE;
                entries[entry] = k;
            }
        }
        uint32_t at = 0;
        for (uint32_t entry = 0; valid && entry < debug->entry_count; entry += 1)
        {
            if (entries[entry] != BUSTER_IR_NONE)
            {
                candidate.order[at++] = entries[entry];
            }
        }
        valid = valid && at == candidate.count;
    }
    *result = valid ? candidate : (CheckTier1DebugConstants){ 0 };
    return valid;
}

static uint64_t check_tier1_debug_constant_ranges(const CheckTier1DebugConstants* constants, const uint32_t* entries, const uint8_t* safe,
                                                uint32_t body, uint32_t epilogue, EmittedLocalRange* output)
{
    uint64_t ranges = 0;
    uint32_t active = 0;
    for (uint32_t r = body; constants->count != 0 && r < epilogue; r += 1)
    {
        if (r == body || entries[r] != entries[r - 1] || safe[r] != safe[r - 1])
        {
            uint32_t first = 0;
            uint32_t end = safe[r] != 0 ? constants->count : 0;
            while (first < end)
            {
                const uint32_t middle = first + (end - first) / 2;
                const bool initialized = constants->values[constants->order[middle]].entry < entries[r];
                first = initialized ? middle + 1 : first;
                end = initialized ? end : middle;
            }
            for (uint32_t k = first; k < active; k += 1)
            {
                if (output != NULL)
                {
                    const IrDebugScalar value = constants->values[constants->order[k]];
                    output[ranges] = (EmittedLocalRange){ .value = value.value, .local = value.local, .begin = constants->first[k], .end = r, .value_bytes = value.bytes };
                }
                ranges += 1;
            }
            for (uint32_t k = active; k < first; k += 1)
            {
                constants->first[k] = r;
            }
            active = first;
        }
    }
    for (uint32_t k = 0; k < active; k += 1)
    {
        if (output != NULL)
        {
            const IrDebugScalar value = constants->values[constants->order[k]];
            output[ranges] = (EmittedLocalRange){ .value = value.value, .local = value.local, .begin = constants->first[k], .end = epilogue, .value_bytes = value.bytes };
        }
        ranges += 1;
    }
    return ranges;
}

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

// A retained original local's whole object in the physical frame. A shared row needs the additional lifetime proof
// below. Slot flags for ABI bookkeeping do not describe source objects. The original local's size must fit.
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
    valid = valid && frame->slot[row] < frame->slot_count && uses[frame->slot[row]] != 0;
    valid = valid && tier1_emit_cfg_object(ir, emitted, slot, &displacement);
    valid = valid && displacement >= bias && (int64_t)displacement + (int64_t)object.size <= bias + frame->payload_bytes;
    *home = (Tier0Home){
        .displacement = displacement,
        .reg = (uint8_t)frame->base_register,
        .mode = valid ? BUSTER_X86_MEMORY_BASE : 0,
    };
    return valid;
}

#include "c_check_tier1_debug_objects.c"
#include "c_check_tier1_debug_values.c"

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
                 debug->scalar_count <= locals && (debug->scalar_count == 0 || debug->scalars != NULL) &&
                 body <= epilogue && epilogue <= count && (record_order == NULL || record_order[count] == count) &&
                 (!emitted->general || emitted->cfg_input.block_count >= ir->block_count);
    CheckTier1Debug candidate = { 0 };
    for (uint32_t k = 0; valid && k < debug->scalar_count; k += 1)
    {
        const IrDebugScalar scalar = debug->scalars[k];
        valid = scalar.local < locals && (k == 0 || debug->scalars[k - 1].local < scalar.local);
        valid = valid && scalar.bytes != 0 && scalar.bytes <= 8 && scalar.bytes == tier0_class_bytes(function->local_classes[scalar.local]);
        valid = valid && ((scalar.kind == IR_DEBUG_PARAMETER && scalar.local < function->parameter_count && scalar.entry == BUSTER_IR_NONE) ||
                          (scalar.kind == IR_DEBUG_CONSTANT && scalar.local >= function->parameter_count && scalar.entry < function->count));
    }
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
    for (uint32_t local = 0; valid && local < locals; local += 1)
    {
        const uint32_t slot = local_slots[local];
        const bool found = framed && slot < slots && check_tier1_debug_home(function, ir, emitted, uses, slot, homes + local);
        local_slots[local] = found ? slot : BUSTER_IR_NONE;
    }
    const bool bindings = debug->binding_flags != NULL;
    for (uint32_t local = 0; valid && bindings && local < locals; local += 1)
    {
        if ((debug->binding_flags[local] & BUSTER_IR_DEBUG_BIND_ENTRY) != 0)
        {
            local_slots[local] = BUSTER_IR_NONE;
        }
    }
    for (uint32_t k = 0; valid && !bindings && k < debug->scalar_count; k += 1)
    {
        const uint32_t local = debug->scalars[k].local;
        local_slots[local] = BUSTER_IR_NONE;
    }
    CheckTier1DebugSpan* object_clips = NULL;
    valid = valid && (!framed || check_tier1_debug_object_clips(function, ir, emitted, record_order, uses, local_slots, homes, vector, scratch, &object_clips));
    CheckTier1DebugParameters parameters = { 0 };
    CheckTier1DebugConstants constants = { 0 };
    CheckTier1DebugValues values = { 0 };
    valid = valid && (!bindings || check_tier1_debug_values(function, ir, emitted, record_order, vector, scratch, &values));
    valid = valid && (bindings || check_tier1_debug_parameters(function, debug, emitted, record_order, vector, scratch, &parameters));
    valid = valid && (bindings || check_tier1_debug_constants(debug, scratch, &constants));
    const uint64_t parameter_ranges = valid && !bindings ? check_tier1_debug_parameter_ranges(&parameters, vector, NULL) : 0;
    const uint64_t constant_ranges = valid && !bindings ? check_tier1_debug_constant_ranges(&constants, entries, safe, body, epilogue, NULL) : 0;
    const uint64_t value_ranges = valid && bindings ? check_tier1_debug_value_ranges(&values, entries, safe, body, epilogue, vector, NULL) : 0;
    const uint64_t object_ranges = valid ? check_tier1_debug_object_ranges(homes, local_slots, locals, spans, span_count, object_clips, NULL) : 0;
    const uint64_t ranges = object_ranges + parameter_ranges + constant_ranges + value_ranges;
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
        if (object_ranges != 0)
        {
            candidate.local_range_count += (uint32_t)check_tier1_debug_object_ranges(homes, local_slots, locals, spans, span_count, object_clips, candidate.local_ranges);
        }
        if (parameter_ranges != 0)
        {
            candidate.local_range_count += (uint32_t)check_tier1_debug_parameter_ranges(&parameters, vector, candidate.local_ranges + candidate.local_range_count);
        }
        if (constant_ranges != 0)
        {
            candidate.local_range_count += (uint32_t)check_tier1_debug_constant_ranges(&constants, entries, safe, body, epilogue, candidate.local_ranges + candidate.local_range_count);
        }
        if (value_ranges != 0)
        {
            candidate.local_range_count += (uint32_t)check_tier1_debug_value_ranges(&values, entries, safe, body, epilogue, vector, candidate.local_ranges + candidate.local_range_count);
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
