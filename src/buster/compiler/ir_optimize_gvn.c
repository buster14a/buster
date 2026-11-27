// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original SSA value numbering (#139), included after ir_optimize.c. The solver computes the coarsest congruence
// partition: values no cycle is reachable from are numbered once each in dependency order (#866), and only the values
// that reach a cycle are refined in rounds. Hashes never establish equality: full keys and predecessor-ordered phi
// operands are compared on every collision. Loads, calls, environment-sensitive floating operations and all
// undef-tainted values are excluded.

#define BUSTER_IR_GVN_FIELDS 8
#define BUSTER_IR_GVN_HASH_BASIS UINT64_C(1469598103934665603)
#define BUSTER_IR_GVN_HASH_MULTIPLIER UINT64_C(1099511628211)
#define BUSTER_IR_GVN_PROBES_PER_INSTRUCTION 8
// The acyclic numbering keeps a representative's key in four of an instruction's eight field words.
#define BUSTER_IR_GVN_ENTRY_WORDS 4

typedef struct IrGvnOptions IrGvnOptions;
struct IrGvnOptions
{
    uint32_t sweep_limit;
    // Key comparisons a grouping may make per instruction before it completes another way. Tests lower it.
    uint32_t probes;
    // Refine every instruction in synchronous rounds instead of numbering the acyclic ones first: the exact oracle
    // of the acyclic numbering.
    bool complete;
    // Apply a congruence only to a value of vector or mask type (#2781, Tier 1's default policy): the constants and
    // tables vector code shares, without the general registers' longer live ranges (#2719). The congruences are
    // computed as without it; every other value stays its own, a forwarded copy included.
    bool vectors;
    // Apply only the congruences that pay for the live range they make (#2719, for promoted SSA): never a free value's
    // (ir_gvn_free: a constant, an address, a cast), which selection folds into each user or recomputes at no
    // cost, and whose merged copy would hold a register across blocks; and a truth value's only within its block, since a
    // branch tests a comparison's flags where a merged one takes a register and a setcc. The congruences are computed as
    // without it. A function where nothing merges is returned as it was given: *result's function is the input's, and
    // nothing is written to the output.
    bool pressure;
    // The function is verified already (IrOptimizeWork.validated, #2719: Tier 1's promoted function): its preparation
    // builds the CFG, use lists and dominator tree without verifying it again. A function the pass rewrites is verified
    // as ever.
    bool validated;
    // A caller consuming only the rewritten function may omit the dependency-cycle census.
    bool apply_only;
};

// IrGvnOptions.pressure's free values by operation: each copy costs nothing at its user.
static const uint8_t ir_gvn_free[BUSTER_IR_OP_TABLE] = {
    [IR_CONST] = 1,
    [IR_SYMBOL_ADDR] = 1,
    [IR_STACK_ADDR] = 1,
    [IR_ADDRESS] = 1,
    [IR_INTTOPTR] = 1,
    [IR_PTRTOINT] = 1,
    [IR_BITCAST] = 1,
    [IR_COPY] = 1,
    [IR_TRUNC] = 1,
    [IR_ZEXT] = 1,
    [IR_SEXT] = 1,
};

typedef struct IrGvnResult IrGvnResult;
struct IrGvnResult
{
    IrFunction function;
    uint32_t removed;
    uint32_t classes;
    // Instructions numbered before cyclic refinement, or BUSTER_IR_NONE when the caller omits the cycle census.
    uint32_t acyclic;
    uint64_t rounds;
    uint64_t partition_probes;
    uint64_t partition_sort_fallbacks;
    uint64_t scratch_bytes;
    bool fallback;
};

typedef struct IrGvnPayload IrGvnPayload;
struct IrGvnPayload
{
    uint64_t low;
    uint64_t high;
};

typedef struct IrGvnWait IrGvnWait;
struct IrGvnWait
{
    IrGvnWait* next;
    uint32_t user;
};

typedef struct IrGvnWork IrGvnWork;
struct IrGvnWork
{
    IrOptimizeWork ir;
    uint32_t* alias;
    uint32_t* classes;
    // Indexed by position in members: the partition a round is computing.
    uint32_t* next_classes;
    uint32_t* order;
    uint32_t* temporary_order;
    // The instructions a refinement round partitions, by position; their keys and hashes are stored by position.
    uint32_t* members;
    uint32_t* waiting;
    uint64_t* fields;
    uint64_t* hashes;
    uint8_t* pure;
    uint8_t* tainted;
    IrGvnWait** waiters;
    IrGvnWait* wait_free;
    uint64_t wait_left;
    bool wait_failed;
    uint32_t probes;
    IrGvnResult statistics;
    // IrGvnOptions.pressure, which ir_gvn_available applies, and the congruences it applied.
    bool pressure;
    bool ordered;
    bool relative_keys;
    bool repeats_only;
    bool fused;
    bool vectors_only;
    uint32_t staged;
    uint32_t* staged_targets;
    uint32_t* tags;
    uint32_t* owners;
    uint32_t current_block;
    uint32_t position;
    bool copies;
    uint32_t merged;
    uint32_t taint_count;
    uint32_t unique;
    uint32_t pending;
};

static __attribute__((unused)) IrGvnOptions ir_gvn_options(void)
{
    const IrGvnOptions options = {
        .sweep_limit = 64,
        .probes = BUSTER_IR_GVN_PROBES_PER_INSTRUCTION,
    };
    return options;
}

static uint32_t ir_gvn_resolve(uint32_t* aliases, uint32_t value)
{
    uint32_t result = value;
    while (aliases[result] != result)
    {
        result = aliases[result];
    }
    while (aliases[value] != value)
    {
        const uint32_t next = aliases[value];
        aliases[value] = result;
        value = next;
    }
    return result;
}

// In reachable SSA without phis every operand precedes its use in reverse postorder. Such a walk can number
// instructions directly. Only undef propagation needs the complete use lists.
static IrError ir_gvn_prepare_uses(IrGvnWork* work, Arena* scratch, Arena* output)
{
    const IrFunction* f = work->ir.function;
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    bool phis = false;
    for (uint64_t first = 0; first < f->instruction_count; first += 16)
    {
        const uint32_t left = f->instruction_count - (uint32_t)first;
        const __mmask16 valid = (__mmask16)_bzhi_u32(0xffff, left < 16 ? left : 16);
        const __m128i op = _mm_maskz_loadu_epi8(valid, f->op + first);
        phis |= _mm_mask_cmpeq_epi8_mask(valid, op, _mm_set1_epi8(IR_PHI)) != 0;
        work->copies |= _mm_mask_cmpeq_epi8_mask(valid, op, _mm_set1_epi8(IR_COPY)) != 0;
        _mm512_mask_storeu_epi32(work->alias + first, valid, _mm512_add_epi32(lanes, _mm512_set1_epi32((int32_t)first)));
        const __mmask16 undef = _mm_mask_cmpeq_epi8_mask(valid, op, _mm_set1_epi8(IR_UNDEF));
        if (undef != 0)
        {
            _mm_mask_storeu_epi8(work->tainted + first, undef, _mm_set1_epi8(1));
            const __m512i ids = _mm512_add_epi32(lanes, _mm512_set1_epi32((int)first));
            _mm512_mask_compressstoreu_epi32(work->members + work->taint_count, undef, ids);
            work->taint_count += (uint32_t)__builtin_popcount(undef);
        }
    }
    work->ordered = !phis && work->ir.dominators.reachable_count == f->block_count;
    return work->taint_count != 0 ? ir_uses_validated(f, scratch, output, &work->ir.uses) : IR_OK;
}

static void ir_gvn_classify(IrGvnWork* work, bool number_leaves)
{
    const IrFunction* f = work->ir.function;
    static const uint8_t pure[BUSTER_IR_OP_TABLE] = {
        [IR_CONST] = 1,
        [IR_VCONST] = 1,
        [IR_VCMP] = 2,
        [IR_VSELECT] = 1,
        [IR_INTRINSIC] = 3,
        [IR_PARAM] = 1,
        [IR_ADD] = 1,
        [IR_SUB] = 1,
        [IR_MUL] = 1,
        [IR_SDIV] = 1,
        [IR_UDIV] = 1,
        [IR_SREM] = 1,
        [IR_UREM] = 1,
        [IR_AND] = 1,
        [IR_OR] = 1,
        [IR_XOR] = 1,
        [IR_SHL] = 1,
        [IR_LSHR] = 1,
        [IR_ASHR] = 1,
        [IR_NEG] = 1,
        [IR_NOT] = 1,
        [IR_FNEG] = 1,
        [IR_ICMP] = 1,
        [IR_SELECT] = 1,
        [IR_TRUNC] = 1,
        [IR_ZEXT] = 1,
        [IR_SEXT] = 1,
        [IR_INTTOPTR] = 1,
        [IR_PTRTOINT] = 1,
        [IR_BITCAST] = 1,
        [IR_STACK_ADDR] = 1,
        [IR_SYMBOL_ADDR] = 1,
        [IR_ADDRESS] = 1,
        [IR_PHI] = 1,
    };
    // Taint is permanent. Each user enters this list at most once, before partitioning reuses the column.
    for (uint32_t at = 0; at < work->taint_count; at += 1)
    {
        const uint32_t value = work->members[at];
        for (uint32_t use = work->ir.uses.offsets[value]; use < work->ir.uses.offsets[(uint64_t)value + 1]; use += 1)
        {
            const uint32_t instruction = work->ir.uses.uses[use].instruction;
            if (work->tainted[instruction] == 0)
            {
                work->tainted[instruction] = 1;
                work->members[work->taint_count++] = instruction;
            }
        }
    }
    for (uint64_t first = 0; first < f->instruction_count; first += BUSTER_IR_OPTIMIZE_BATCH)
    {
        const uint32_t count = f->instruction_count - first < BUSTER_IR_OPTIMIZE_BATCH ? f->instruction_count - first : BUSTER_IR_OPTIMIZE_BATCH;
        const __mmask16 valid = (UINT32_C(1) << count) - 1;
        const __m128i operations = _mm_maskz_loadu_epi8(valid, f->op + first);
        const __m128i kinds = _mm512_castsi512_si128(_mm512_permutex2var_epi8(_mm512_loadu_si512(pure), _mm512_zextsi128_si512(operations), _mm512_loadu_si512(pure + 64)));
        const __m128i tainted = _mm_maskz_loadu_epi8(valid, work->tainted + first);
        const __m128i flags = _mm_maskz_loadu_epi8(valid, f->flags + first);
        const __mmask16 integer_compare = _mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(2)) & _mm_cmplt_epu8_mask(flags, _mm_set1_epi8(IR_SIMD_LANE_F32));
        const __m512i discriminators = _mm512_maskz_loadu_epi32(valid, f->c + first);
        // A splat, the width changes, the permutes, the conversions, the scalar shifts, the lane operations and the
        // ternary kinds (#1840: extract to permute2, consecutive kinds, a ternary one's `c` its kind in the low byte and
        // its third operand's place past it), the AES rounds, the reductions, the lanes' unary operations, the floating
        // operations at their own rounding (which read no MXCSR and set none of its flags), the bit shuffles into a mask
        // and the quadword operations are pure and total.
        const __m512i kind_bits = _mm512_and_si512(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_KIND));
        const __mmask16 ternary = _mm512_cmpge_epu32_mask(kind_bits, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_TERNARY)) &
                                  _mm512_cmplt_epu32_mask(kind_bits, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_TERNARY_END));
        const __mmask16 splat = _mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(3)) & (_mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_SPLAT)) |
                                                                          (_mm512_cmpge_epu32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_EXTRACT)) &
                                                                           _mm512_cmple_epu32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_LANE_OP))) | ternary |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_AES)) |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_REDUCE)) |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_LANE_UNARY)) |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_FROUND)) |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_BITSHUFFLE)) |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_QUADWORD_OP)) |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_PAIRWISE)));
        const __mmask16 eligible = (_mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(1)) | integer_compare | splat) & _mm_cmpeq_epi8_mask(tainted, _mm_setzero_si128());
        _mm_mask_storeu_epi8(work->pure + first, valid, _mm_maskz_set1_epi8(eligible, 1));
        if (number_leaves)
        {
            // Impure rows are distinct leaves even when their own operands form cycles. Give them dense class
            // identifiers now, so the dependency walk only visits rows whose equivalence needs a key.
            const __mmask16 leaves = valid & ~eligible;
            const __m512i lanes = _mm512_setr_epi32(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
            const __m512i ids = _mm512_add_epi32(lanes, _mm512_set1_epi32((int32_t)work->unique));
            _mm512_mask_storeu_epi32(work->classes + first, valid, _mm512_maskz_expand_epi32(leaves, ids));
            work->unique += (uint32_t)__builtin_popcount(leaves);
        }
    }
    if (work->copies)
    {
        for (uint32_t i = 0; i < f->instruction_count; i += 1)
        {
            if (f->op[i] == IR_COPY && work->tainted[i] == 0 && work->ir.dominators.rpo_index[work->ir.cfg.instruction_blocks[i]] != BUSTER_DOMINATOR_NONE)
            {
                work->alias[i] = f->a[i];
            }
        }
        for (uint32_t i = 0; i < f->instruction_count; i += 1)
        {
            ir_gvn_resolve(work->alias, i);
        }
    }
}

// Phi operands may be stored in any order. Try the canonical predecessor position first. A verified phi
// contains every predecessor exactly once, including when two instructions share an extra slice.
static uint32_t ir_gvn_phi_class(const IrGvnWork* work, uint32_t instruction, uint32_t predecessor, uint32_t position)
{
    const IrFunction* f = work->ir.function;
    const uint32_t* inputs = f->extra + f->a[instruction];
    if (inputs[(uint64_t)position * 2] != predecessor)
    {
        position = 0;
        while (inputs[(uint64_t)position * 2] != predecessor)
        {
            position += 1;
        }
    }
    return work->classes[work->alias[inputs[(uint64_t)position * 2 + 1]]];
}

// The static key words beside the opcode, type, flags and alignment. The acyclic numbering and the refinement rounds
// both read them here, so the two cannot disagree on which instructions are alike.
static IrGvnPayload ir_gvn_payload(const IrGvnWork* work, uint32_t i)
{
    const IrFunction* f = work->ir.function;
    const uint32_t op = f->op[i];
    IrGvnPayload payload = { 0 };
    if (op == IR_CONST)
    {
        payload.low = f->constants[f->a[i]].low;
        payload.high = f->constants[f->a[i]].high;
    }
    else if (op == IR_VCONST)
    {
        // The remaining six words take part in hashing and in collision equality, in the acyclic numbering
        // (ir_gvn_number) and in the refinement (ir_gvn_hash_payload, ir_gvn_compare): table indices alone would miss
        // identical payloads at distinct indices, and two-word keys would merge different upper lanes.
        payload.low = f->vector_constants[f->a[i]].words[0];
        payload.high = f->vector_constants[f->a[i]].words[1];
    }
    else if (op == IR_PARAM || op == IR_STACK_ADDR || op == IR_SYMBOL_ADDR)
    {
        payload.low = f->a[i];
    }
    else if (op == IR_ICMP || op == IR_VCMP)
    {
        payload.low = f->c[i];
    }
    else if (op == IR_INTRINSIC)
    {
        // Its kind: a ternary kind's `c` also holds its third operand's place, which is no part of its value.
        payload.low = ir_intrinsic_kind(f->c[i]);
    }
    else if (op == IR_ADDRESS)
    {
        payload.low = (uint64_t)f->addresses[f->c[i]].offset;
        payload.high = f->addresses[f->c[i]].scale | ((uint64_t)(f->b[i] == BUSTER_IR_NONE) << 8);
    }
    else if (op == IR_PHI)
    {
        payload.low = work->ir.cfg.instruction_blocks[i];
        payload.high = f->b[i];
    }
    return payload;
}

static uint64_t ir_gvn_meta(const IrFunction* f, uint32_t i)
{
    return f->op[i] | ((uint64_t)f->type[i] << 8) | ((uint64_t)f->flags[i] << 16) | ((uint64_t)f->alignment_log2[i] << 24);
}

static uint64_t ir_gvn_mix(uint64_t hash, uint64_t value)
{
    return (hash ^ value) * BUSTER_IR_GVN_HASH_MULTIPLIER;
}

// Writes instruction i's key at position in the field columns.
static void ir_gvn_key(IrGvnWork* work, uint32_t position, uint32_t i, bool initial)
{
    const IrFunction* f = work->ir.function;
    const uint64_t n = f->instruction_count;
    const uint32_t mask = ir_value_mask3(f, i);
    const IrGvnPayload payload = ir_gvn_payload(work, i);
    work->fields[position] = ir_gvn_meta(f, i);
    work->fields[n + position] = !initial ? work->classes[i] : 0;
    work->fields[n * 2 + position] = payload.low;
    work->fields[n * 3 + position] = payload.high;
    work->fields[n * 4 + position] = !initial && (mask & 1) != 0 ? work->classes[ir_gvn_resolve(work->alias, f->a[i])] : 0;
    work->fields[n * 5 + position] = !initial && (mask & 2) != 0 ? work->classes[ir_gvn_resolve(work->alias, f->b[i])] : 0;
    work->fields[n * 6 + position] = !initial && (mask & 4) != 0 ? work->classes[ir_gvn_resolve(work->alias, ir_value_third(f, i))] : 0;
    work->fields[n * 7 + position] = work->pure[i] == 0 ? (uint64_t)i + 1 : 0;
}

static uint64_t ir_gvn_hash_payload(const IrGvnWork* work, uint64_t hash, uint32_t i, bool initial)
{
    const IrFunction* f = work->ir.function;
    if (f->op[i] == IR_VCONST)
    {
        const uint64_t* const words = f->vector_constants[f->a[i]].words;
        for (uint32_t word = 2; word < 8; word += 1)
        {
            hash = (hash ^ words[word]) * BUSTER_IR_GVN_HASH_MULTIPLIER;
        }
    }
    if (!initial && f->op[i] == IR_PHI && work->pure[i] != 0)
    {
        const uint32_t block = work->ir.cfg.instruction_blocks[i];
        for (uint32_t edge = work->ir.cfg.predecessor_offsets[block]; edge < work->ir.cfg.predecessor_offsets[(uint64_t)block + 1]; edge += 1)
        {
            hash = ir_gvn_mix(hash, ir_gvn_phi_class(work, i, work->ir.cfg.predecessors[edge], edge - work->ir.cfg.predecessor_offsets[block]));
        }
    }
    return hash;
}

// The previous partition already distinguishes every static field, including all vector-constant words.
// A later round can only split a parent by the classes of its direct operands or its phi inputs.
static void ir_gvn_relative_keys(IrGvnWork* work, uint32_t count)
{
    const IrFunction* f = work->ir.function;
    const uint32_t n = f->instruction_count;
    const __m512i multiplier = _mm512_set1_epi64(BUSTER_IR_GVN_HASH_MULTIPLIER);
    for (uint64_t first = 0; first < count; first += 8)
    {
        const uint32_t amount = count - first < 8 ? count - first : 8;
        const __mmask8 live = (__mmask8)_bzhi_u32(255, amount);
        uint32_t phis = 0;
        for (uint32_t lane = 0; lane < amount; lane += 1)
        {
            const uint32_t j = (uint32_t)first + lane;
            const uint32_t i = work->members[j];
            const uint32_t mask = ir_value_mask3(f, i);
            const uint64_t a = (mask & 1) != 0 ? work->classes[work->alias[f->a[i]]] : 0;
            const uint64_t b = (mask & 2) != 0 ? work->classes[work->alias[f->b[i]]] : 0;
            const uint64_t c = (mask & 4) != 0 ? work->classes[work->alias[ir_value_third(f, i)]] : 0;
            work->fields[j] = work->classes[i] | c << 32;
            work->fields[(uint64_t)n + j] = a | b << 32;
            phis |= (uint32_t)(f->op[i] == IR_PHI && work->pure[i] != 0) << lane;
        }
        const __m512i parents = _mm512_maskz_loadu_epi64(live, work->fields + first);
        const __m512i pairs = _mm512_maskz_loadu_epi64(live, work->fields + n + first);
        const __m512i prefix = _mm512_mullo_epi64(_mm512_xor_si512(_mm512_set1_epi64(BUSTER_IR_GVN_HASH_BASIS), parents), multiplier);
        _mm512_mask_storeu_epi64(work->hashes + first, live, _mm512_mullo_epi64(_mm512_xor_si512(prefix, pairs), multiplier));
        while (phis != 0)
        {
            const uint32_t j = (uint32_t)first + (uint32_t)__builtin_ctz(phis);
            const uint32_t i = work->members[j];
            const uint32_t block = work->ir.cfg.instruction_blocks[i];
            const uint32_t start = work->ir.cfg.predecessor_offsets[block];
            uint64_t hash = work->hashes[j];
            for (uint32_t edge = start; edge < work->ir.cfg.predecessor_offsets[(uint64_t)block + 1]; edge += 1)
            {
                hash = ir_gvn_mix(hash, ir_gvn_phi_class(work, i, work->ir.cfg.predecessors[edge], edge - start));
            }
            work->hashes[j] = hash;
            phis &= phis - 1;
        }
    }
}

// Keys and hashes of the first count members, by position.
static void ir_gvn_keys(IrGvnWork* work, uint32_t count, bool initial)
{
    const uint64_t n = work->ir.function->instruction_count;
    for (uint32_t position = 0; position < count; position += 1)
    {
        ir_gvn_key(work, position, work->members[position], initial);
    }
    for (uint64_t first = 0; first < count; first += BUSTER_IR_OPTIMIZE_BATCH)
    {
        const uint32_t batch = count - first < BUSTER_IR_OPTIMIZE_BATCH ? count - first : BUSTER_IR_OPTIMIZE_BATCH;
        for (uint32_t half = 0; half < batch; half += 8)
        {
            const uint32_t amount = batch - half < 8 ? batch - half : 8;
            const __mmask8 valid = (UINT32_C(1) << amount) - 1;
            __m512i hash = _mm512_set1_epi64(BUSTER_IR_GVN_HASH_BASIS);
            for (uint32_t field = 0; field < BUSTER_IR_GVN_FIELDS; field += 1)
            {
                const __m512i key = _mm512_maskz_loadu_epi64(valid, work->fields + (uint64_t)field * n + first + half);
                hash = _mm512_mullo_epi64(_mm512_xor_si512(hash, key), _mm512_set1_epi64(BUSTER_IR_GVN_HASH_MULTIPLIER));
            }
            _mm512_mask_storeu_epi64(work->hashes + first + half, valid, hash);
        }
        for (uint32_t lane = 0; lane < batch; lane += 1)
        {
            const uint32_t position = first + lane;
            const uint32_t i = work->members[position];
            if (work->ir.function->op[i] == IR_VCONST || (!initial && work->ir.function->op[i] == IR_PHI && work->pure[i] != 0))
            {
                work->hashes[position] = ir_gvn_hash_payload(work, work->hashes[position], i, initial);
            }
        }
    }
}

// Compares the keys at positions a and b, of instructions ia and ib.
static int ir_gvn_compare(const IrGvnWork* work, uint32_t a, uint32_t b, uint32_t ia, uint32_t ib, bool initial)
{
    int result = work->hashes[a] < work->hashes[b] ? -1 : work->hashes[a] != work->hashes[b];
    const IrFunction* f = work->ir.function;
    const uint32_t fields = work->relative_keys ? 2 : BUSTER_IR_GVN_FIELDS;
    for (uint32_t field = 0; result == 0 && field < fields; field += 1)
    {
        const uint64_t left = work->fields[(uint64_t)field * f->instruction_count + a];
        const uint64_t right = work->fields[(uint64_t)field * f->instruction_count + b];
        result = left < right ? -1 : left != right;
    }
    if (result == 0 && !work->relative_keys && f->op[ia] == IR_VCONST)
    {
        const uint64_t* const left = f->vector_constants[f->a[ia]].words;
        const uint64_t* const right = f->vector_constants[f->a[ib]].words;
        for (uint32_t word = 2; result == 0 && word < 8; word += 1)
        {
            result = left[word] < right[word] ? -1 : left[word] != right[word];
        }
    }
    if (result == 0 && !initial && f->op[ia] == IR_PHI && work->pure[ia] != 0)
    {
        const uint32_t block = work->ir.cfg.instruction_blocks[ia];
        for (uint32_t edge = work->ir.cfg.predecessor_offsets[block]; result == 0 && edge < work->ir.cfg.predecessor_offsets[(uint64_t)block + 1]; edge += 1)
        {
            const uint32_t predecessor = work->ir.cfg.predecessors[edge];
            const uint32_t left = ir_gvn_phi_class(work, ia, predecessor, edge - work->ir.cfg.predecessor_offsets[block]);
            const uint32_t right = ir_gvn_phi_class(work, ib, predecessor, edge - work->ir.cfg.predecessor_offsets[block]);
            result = left < right ? -1 : left != right;
        }
    }
    return result;
}

// The independent comparison-sort completion also bounds adversarial hash-table work. Neither grouping path
// mutates the old classes: parent-class keys remain immutable until the complete partition is published.
static uint32_t ir_gvn_partition_sorted(IrGvnWork* work, uint32_t count, bool initial, uint32_t base)
{
    const uint32_t* members = work->members;
    for (uint32_t position = 0; position < count; position += 1)
    {
        work->order[position] = position;
    }
    for (uint64_t width = 1; width < count; width *= 2)
    {
        for (uint64_t first = 0; first < count; first += width * 2)
        {
            const uint64_t middle = first + width < count ? first + width : count;
            const uint64_t end = middle + width < count ? middle + width : count;
            uint64_t left = first;
            uint64_t right = middle;
            for (uint64_t out = first; out < end; out += 1)
            {
                const bool take_left = left < middle && (right == end || ir_gvn_compare(work, work->order[left], work->order[right], members[work->order[left]], members[work->order[right]], initial) <= 0);
                work->temporary_order[out] = take_left ? work->order[left++] : work->order[right++];
            }
        }
        uint32_t* swap = work->order;
        work->order = work->temporary_order;
        work->temporary_order = swap;
    }
    uint32_t classes = 0;
    for (uint32_t position = 0; position < count; position += 1)
    {
        const uint32_t current = work->order[position];
        if (position == 0 || ir_gvn_compare(work, work->order[position - 1], current, members[work->order[position - 1]], members[current], initial) != 0)
        {
            classes += 1;
        }
        work->next_classes[current] = base + classes;
    }
    return classes;
}

// Reuse the sort's two n-element u32 columns as bucket heads and representative links. The largest power of two
// not exceeding count needs no extra allocation or padding. Only representatives enter a chain, so a wide duplicate
// class costs one comparison per member, not a growing chain. Full keys (including ordered phi inputs) decide
// equality; the folded hash selects a bucket only. Class IDs need not follow key order, only the same equivalence.
// They run from base + 1, so they never collide with the classes the acyclic numbering already published.
//
// Stop after a linear probe budget and restart the unchanged comparison sort on the already materialized keys.
// This bounds a hostile all-collision round by O(n log n) key comparisons instead of quadratic probing. The table
// probes use scalar indexed loads, not gathers; the preceding eight-lane SIMD key hashing stays unchanged.
static uint32_t ir_gvn_partition_group(IrGvnWork* work, uint32_t count, bool initial, bool sorted, uint32_t base)
{
    const uint32_t* members = work->members;
    uint32_t classes = 0;
    if (!sorted && count != 0)
    {
        uint32_t buckets = 1;
        while (buckets <= count / 2)
        {
            buckets *= 2;
        }
        memset(work->temporary_order, 0xff, (uint64_t)buckets * sizeof(uint32_t));
        const uint64_t budget = (uint64_t)count * work->probes;
        uint64_t probes = 0;
        for (uint32_t position = 0; position < count && !sorted; position += 1)
        {
            const uint64_t hash = work->hashes[position];
            const uint32_t bucket = (hash ^ (hash >> 32)) & (buckets - 1);
            uint32_t match = work->temporary_order[bucket];
            bool equal = false;
            while (match != BUSTER_IR_NONE && !equal && probes < budget)
            {
                probes += 1;
                equal = ir_gvn_compare(work, position, match, members[position], members[match], initial) == 0;
                if (!equal)
                {
                    match = work->order[match];
                }
            }
            sorted = match != BUSTER_IR_NONE && !equal;
            if (!sorted)
            {
                if (match == BUSTER_IR_NONE)
                {
                    work->order[position] = work->temporary_order[bucket];
                    work->temporary_order[bucket] = position;
                    classes += 1;
                }
                work->next_classes[position] = match == BUSTER_IR_NONE ? base + classes : work->next_classes[match];
            }
        }
        work->statistics.partition_probes += probes;
        work->statistics.partition_sort_fallbacks += sorted;
    }
    if (sorted)
    {
        classes = ir_gvn_partition_sorted(work, count, initial, base);
    }
    return classes;
}

static uint32_t ir_gvn_partition(IrGvnWork* work, uint32_t count, bool initial, bool sorted, uint32_t base)
{
    if (work->relative_keys)
    {
        ir_gvn_relative_keys(work, count);
    }
    else
    {
        ir_gvn_keys(work, count, initial);
    }
    const uint32_t classes = ir_gvn_partition_group(work, count, initial, sorted, base);
    for (uint32_t position = 0; position < count; position += 1)
    {
        work->classes[work->members[position]] = work->next_classes[position];
    }
    work->statistics.rounds += 1;
    return classes;
}

// Refinement can only split a class. Give every singleton a permanent class before the next round and keep
// only members of the other classes in its worklist. Keys still read the permanent labels of all their operands.
static uint32_t ir_gvn_freeze_singletons(IrGvnWork* work, uint32_t* count, uint32_t base, uint32_t classes)
{
    uint32_t* sizes = work->temporary_order;
    uint32_t* labels = work->order;
    memset(sizes, 0, (uint64_t)classes * sizeof(*sizes));
    for (uint32_t j = 0; j < *count; j += 1)
    {
        sizes[work->classes[work->members[j]] - base - 1] += 1;
    }
    uint32_t frozen = 0;
    for (uint32_t c = 0; c < classes; c += 1)
    {
        frozen += sizes[c] == 1;
    }
    if (frozen != 0)
    {
        uint32_t singleton = base;
        uint32_t repeated = base + frozen;
        for (uint32_t c = 0; c < classes; c += 1)
        {
            labels[c] = sizes[c] == 1 ? ++singleton : ++repeated;
        }
        uint32_t next = 0;
        for (uint32_t j = 0; j < *count; j += 1)
        {
            const uint32_t value = work->members[j];
            const uint32_t c = work->classes[value] - base - 1;
            work->classes[value] = labels[c];
            work->members[next] = value;
            next += sizes[c] != 1;
        }
        *count = next;
    }
    return frozen;
}

// Refine the first count members from their static keys to the coarsest partition stable under operand classes. An
// operand outside the members keeps its published class throughout. After sweep_limit rounds, comparison sorting completes the same fixed point.
static uint32_t ir_gvn_refine(IrGvnWork* work, uint32_t count, uint32_t base, uint32_t sweep_limit, bool compact)
{
    const uint32_t first_base = base;
    // Every pending member still has class zero. A numbered operand has a permanent nonzero class, so
    // it can distinguish the initial groups immediately, including phi inputs in predecessor order.
    uint32_t classes = ir_gvn_partition(work, count, base == 0, false, base);
    bool changed = base == 0 || classes < count;
    work->relative_keys = compact;
    while (changed)
    {
        if (count >= 32)
        {
            const uint32_t frozen = ir_gvn_freeze_singletons(work, &count, base, classes);
            base += frozen;
            classes -= frozen;
        }
        const bool fallback = work->statistics.rounds > sweep_limit;
        const uint32_t refined = ir_gvn_partition(work, count, false, fallback, base);
        changed = refined != classes && (first_base == 0 || refined < count);
        classes = refined;
        work->statistics.fallback = work->statistics.fallback || fallback;
    }
    work->relative_keys = false;
    return (base - first_base) + classes;
}

// #866: the acyclic numbering. Partition refinement needs as many rounds as the longest dependency chain, each over
// every instruction. But a value no cycle of operand dependences is reachable from is congruent exactly to the values
// with its static key and congruent operands, by induction on its depth, and never to a value that reaches a cycle:
// matching operands position by position would follow that cycle forever. So these values are numbered once each,
// in dependency order, by hashing (static key, operand classes) into a table of representatives: Kahn's algorithm,
// with a wait list for each unresolved operand. Whatever is left reaches a
// cycle, and only those values are refined in rounds, with the numbered operands' classes fixed. The partition is the
// coarsest congruence either way; only class labels may differ from the synchronous refinement's.
//
// The visits follow reverse postorder, so in reachable SSA code every operand but a phi's input along a back edge is
// numbered first and only unresolved dependencies get a wait record. Numbering a value is a
// dependent chain (its key needs its operands' classes, published by the instructions just before it), so it is
// scalar; the refinement rounds keep the eight-lane key hashing.
//
// A representative's key is four words at four times its index in the field columns: payload low and high, the meta
// word with operand class c above it, and operand classes a and b. Its hash is in hashes, its chain link in order and
// the bucket heads in temporary_order.
// Record an unresolved operand at its copy root. A value can wake exactly the visits which actually waited
// for it. The acyclic keys occupy only the first four field columns, leaving room for the usual wait lists.
static bool ir_gvn_wait_for(IrGvnWork* work, uint32_t value, uint32_t user)
{
    if (work->wait_left == 0)
    {
        work->wait_free = arena_push_array(work->ir.scratch, 256, sizeof(IrGvnWait), _Alignof(IrGvnWait));
        if (work->wait_free == NULL)
        {
            work->wait_failed = true;
            return false;
        }
        work->wait_left = 256;
    }
    IrGvnWait* edge = work->wait_free++;
    work->wait_left -= 1;
    edge->user = user;
    edge->next = work->waiters[value];
    work->waiters[value] = edge;
    return true;
}

// Read operand classes once. A first visit can wait for an operand and leave its class zero. A released instruction
// or a block in dependency order can number immediately. An exhausted probe budget restarts the partition.
static bool ir_gvn_number(IrGvnWork* work, uint32_t i, uint32_t buckets, uint64_t budget, uint32_t* count, bool can_wait)
{
    const IrFunction* f = work->ir.function;
    const uint32_t mask = ir_value_mask3(f, i);
    const IrGvnPayload payload = ir_gvn_payload(work, i);
    const uint64_t a = (mask & 1) != 0 ? work->classes[work->alias[f->a[i]]] : 0;
    const uint64_t b = (mask & 2) != 0 ? work->classes[work->alias[f->b[i]]] : 0;
    const uint64_t c = (mask & 4) != 0 ? work->classes[work->alias[ir_value_third(f, i)]] : 0;
    if (can_wait)
    {
        uint32_t waiting = 0;
        const uint32_t pending = mask & ((uint32_t)(a == 0) | (uint32_t)(b == 0) << 1 | (uint32_t)(c == 0) << 2);
        if (pending != 0)
        {
            if (((pending & 1) != 0 && !ir_gvn_wait_for(work, work->alias[f->a[i]], i)) ||
                ((pending & 2) != 0 && !ir_gvn_wait_for(work, work->alias[f->b[i]], i)) ||
                ((pending & 4) != 0 && !ir_gvn_wait_for(work, work->alias[ir_value_third(f, i)], i)))
            {
                return false;
            }
            waiting = (uint32_t)__builtin_popcount(pending);
        }
        if (f->op[i] == IR_PHI)
        {
            for (uint32_t k = 0; k < f->b[i]; k += 1)
            {
                const uint32_t v = work->alias[f->extra[f->a[i] + (uint64_t)k * 2 + 1]];
                if (work->classes[v] == 0)
                {
                    if (!ir_gvn_wait_for(work, v, i))
                    {
                        return false;
                    }
                    waiting += 1;
                }
            }
        }
        if (waiting != 0)
        {
            work->pending += 1;
            work->waiting[i] = waiting;
            return true;
        }
    }
    const uint64_t key[BUSTER_IR_GVN_ENTRY_WORDS] = { payload.low, payload.high, ir_gvn_meta(f, i) | c << 32, a | b << 32 };
    uint64_t hash = BUSTER_IR_GVN_HASH_BASIS;
    for (uint32_t word = 0; word < BUSTER_IR_GVN_ENTRY_WORDS; word += 1)
    {
        hash = ir_gvn_mix(hash, key[word]);
    }
    // A vector constant's key holds its first two words (ir_gvn_payload): its other six are hashed and compared
    // here, as the refinement does, so that constants differing only in upper lanes stay apart (#1450).
    const bool vector = f->op[i] == IR_VCONST;
    const uint64_t* const words = vector ? f->vector_constants[f->a[i]].words : NULL;
    for (uint32_t word = 2; vector && word < 8; word += 1)
    {
        hash = ir_gvn_mix(hash, words[word]);
    }
    const bool phi = f->op[i] == IR_PHI;
    const uint32_t block = phi ? work->ir.cfg.instruction_blocks[i] : 0;
    const uint32_t first_edge = phi ? work->ir.cfg.predecessor_offsets[block] : 0;
    const uint32_t end_edge = phi ? work->ir.cfg.predecessor_offsets[(uint64_t)block + 1] : 0;
    for (uint32_t edge = first_edge; edge < end_edge; edge += 1)
    {
        hash = ir_gvn_mix(hash, ir_gvn_phi_class(work, i, work->ir.cfg.predecessors[edge], edge - work->ir.cfg.predecessor_offsets[block]));
    }
    // An empty tag terminates the group search. For a matching fingerprint compare the entire key,
    // including the extra phi or vector-constant words. Charge full groups against the collision budget too.
    const uint32_t width = buckets < 8 ? buckets : 8;
    const uint32_t tag = (uint32_t)(hash >> 32) | 1;
    uint32_t group = ((uint32_t)hash ^ (uint32_t)(hash >> 32)) & (buckets - width);
    const __mmask8 valid = (__mmask8)((1u << width) - 1);
    uint32_t slot = 0;
    uint32_t match = BUSTER_IR_NONE;
    bool equal = false;
    for (;;)
    {
        const __m256i tags = _mm256_maskz_loadu_epi32(valid, work->tags + group);
        __mmask8 matches = _mm256_cmpeq_epi32_mask(tags, _mm256_set1_epi32((int)tag));
        while (matches != 0 && !equal)
        {
            if (work->statistics.partition_probes == budget)
            {
                return false;
            }
            work->statistics.partition_probes += 1;
            const uint32_t lane = (uint32_t)__builtin_ctz(matches);
            matches &= matches - 1;
            match = work->owners[group + lane];
            const uint64_t* entry = work->fields + (uint64_t)match * BUSTER_IR_GVN_ENTRY_WORDS;
            equal = _mm256_cmpeq_epi64_mask(_mm256_loadu_si256((const __m256i*)entry), _mm256_loadu_si256((const __m256i*)key)) == 0xf;
            // Equal keys make both vector constants: compare their whole payloads.
            equal = equal && (!vector || memcmp(words, f->vector_constants[f->a[match]].words, sizeof(IrSimdValue)) == 0);
            // Equal keys put both phis in one block: compare their inputs in that block's predecessor order.
            for (uint32_t edge = first_edge; equal && edge < end_edge; edge += 1)
            {
                const uint32_t predecessor = work->ir.cfg.predecessors[edge];
                equal = ir_gvn_phi_class(work, i, predecessor, edge - first_edge) == ir_gvn_phi_class(work, match, predecessor, edge - first_edge);
            }
        }
        if (equal)
        {
            break;
        }
        const __mmask8 empty = valid & _mm256_cmpeq_epi32_mask(tags, _mm256_setzero_si256());
        if (empty != 0)
        {
            slot = group + (uint32_t)__builtin_ctz(empty);
            break;
        }
        if (work->statistics.partition_probes == budget)
        {
            return false;
        }
        work->statistics.partition_probes += 1;
        group = (group + width) & (buckets - 1);
    }
    if (equal)
    {
        work->classes[i] = work->classes[match];
        if (!work->fused)
        {
            work->pure[i] = 2;
            work->pure[match] = 2;
        }
        if (work->fused && (!work->vectors_only || f->type[i] >= IR_V4I32) && (!work->pressure || ir_gvn_free[f->op[i]] == 0))
        {
            const uint32_t rep = work->next_classes[match];
            const uint32_t rep_block = work->ir.cfg.instruction_blocks[rep];
            const bool available = work->pressure && f->type[i] == IR_I1 ? rep_block == work->current_block :
                                   work->position < work->ir.dominators.subtree_end[rep_block];
            if (available)
            {
                work->waiting[work->staged] = i;
                work->staged_targets[work->staged++] = rep;
            }
            else
            {
                work->next_classes[match] = i;
            }
        }
    }
    else
    {
        *count += 1;
        work->classes[i] = *count;
        if (work->fused)
        {
            work->next_classes[i] = i;
        }
        work->owners[slot] = i;
        work->tags[slot] = tag;
        memcpy(work->fields + (uint64_t)i * BUSTER_IR_GVN_ENTRY_WORDS, key, sizeof(key));
    }
    return true;
}

// Each wait edge is consumed once. A waiting instruction becomes ready after its last unnumbered input.
static bool ir_gvn_wake(IrGvnWork* work, uint32_t value, uint32_t buckets, uint64_t budget, uint32_t* count)
{
    uint32_t depth = 0;
    for (;;)
    {
        for (IrGvnWait* edge = work->waiters[value]; edge != NULL; edge = edge->next)
        {
            const uint32_t user = edge->user;
            if (--work->waiting[user] == 0)
            {
                work->pending -= 1;
                work->members[depth++] = user;
            }
        }
        if (depth == 0)
        {
            return true;
        }
        value = work->members[--depth];
        if (!ir_gvn_number(work, value, buckets, budget, count, false))
        {
            return false;
        }
    }
}

// Every phi belongs to its block's phi prefix, and phis in different blocks have different static keys.
// A fingerprint that occurs once in a prefix proves a singleton even before all back-edge classes are
// known. Equal phis have equal known inputs in canonical predecessor order. Collisions stay in the
// general refinement. Snapshot each prefix before waking users so every fingerprint uses one partition.
static bool ir_gvn_seed_phis(IrGvnWork* work, uint32_t buckets, uint64_t budget, uint32_t* classes)
{
    const IrFunction* f = work->ir.function;
    bool ok = true;
    bool seeded = false;
    for (uint32_t block = 0; ok && work->pending != 0 && block < f->block_count; block += 1)
    {
        const uint32_t begin = f->block_offsets[block];
        const uint32_t end = f->block_offsets[(uint64_t)block + 1];
        uint32_t limit = begin;
        uint64_t seen = 0;
        uint64_t crowded = 0;
        while (limit < end && f->op[limit] == IR_PHI)
        {
            const uint32_t i = limit++;
            if (work->classes[i] == 0)
            {
                uint64_t hash = ir_gvn_meta(f, i);
                const uint32_t start = work->ir.cfg.predecessor_offsets[block];
                for (uint32_t edge = start; edge < work->ir.cfg.predecessor_offsets[(uint64_t)block + 1]; edge += 1)
                {
                    hash = ir_gvn_mix(hash, ir_gvn_phi_class(work, i, work->ir.cfg.predecessors[edge], edge - start));
                }
                const uint32_t bit = (uint32_t)(hash ^ (hash >> 32)) & 63;
                work->next_classes[i] = bit;
                const uint64_t mask = UINT64_C(1) << bit;
                crowded |= seen & mask;
                seen |= mask;
            }
        }
        const uint64_t single = seen & ~crowded;
        for (uint32_t i = begin; ok && single != 0 && i < limit; i += 1)
        {
            if (work->classes[i] == 0 && ((single >> work->next_classes[i]) & 1) != 0)
            {
                // This phi no longer needs a wake. Leave one extra count after its remaining edges drain.
                work->waiting[i] += 1;
                work->classes[i] = ++*classes;
                work->pending -= 1;
                seeded = true;
                ok = ir_gvn_wake(work, i, buckets, budget, classes);
            }
        }
    }
    work->statistics.rounds += seeded && ok;
    return ok;
}

static bool ir_gvn_visit(IrGvnWork* work, uint32_t i, uint32_t buckets, uint64_t budget, uint32_t* count)
{
    bool ok = ir_gvn_number(work, i, buckets, budget, count, true);
    if (ok && work->classes[i] != 0 && work->waiters[i] != NULL)
    {
        ok = ir_gvn_wake(work, i, buckets, budget, count);
    }
    return ok;
}

static bool ir_gvn_visit_block(IrGvnWork* work, uint32_t block, uint32_t buckets, uint64_t budget, uint32_t* count)
{
    const IrFunction* f = work->ir.function;
    const uint32_t end = f->block_offsets[(uint64_t)block + 1];
    for (uint64_t first = f->block_offsets[block]; first < end; first += 16)
    {
        const uint32_t left = end - (uint32_t)first;
        const __mmask16 valid = (__mmask16)_bzhi_u32(0xffff, left < 16 ? left : 16);
        const __m128i pure = _mm_maskz_loadu_epi8(valid, work->pure + first);
        __mmask16 rows = _mm_test_epi8_mask(pure, pure);
        while (rows != 0)
        {
            const uint32_t i = (uint32_t)first + (uint32_t)__builtin_ctz(rows);
            rows &= rows - 1;
            const bool ok = work->ordered ? ir_gvn_number(work, i, buckets, budget, count, false) : ir_gvn_visit(work, i, buckets, budget, count);
            if (!ok)
            {
                return false;
            }
        }
    }
    return true;
}

// Returns false when the probe budget runs out; the caller then refines every instruction from its static key.
static bool ir_gvn_acyclic(IrGvnWork* work, uint32_t* count)
{
    const IrFunction* f = work->ir.function;
    const uint32_t n = f->instruction_count;
    const Dominators* dominators = &work->ir.dominators;
    uint32_t buckets = 1;
    while (buckets <= n && buckets < (UINT32_C(1) << 31))
    {
        buckets *= 2;
    }
    const uint64_t mark = work->ir.scratch->length;
    if (n != 0)
    {
        uint64_t* spare = work->fields + (uint64_t)n * BUSTER_IR_GVN_ENTRY_WORDS;
        // Tags and owners consume at most two spare words per instruction. Unordered input uses the
        // remaining space for wait heads and edges until the acyclic partition is done.
        work->tags = (uint32_t*)spare;
        work->owners = work->tags + buckets;
        memset(work->tags, 0, (uint64_t)buckets * sizeof(*work->tags));
        if (!work->ordered)
        {
            work->waiters = (IrGvnWait**)(spare + buckets);
            memset(work->waiters, 0, (uint64_t)n * sizeof(*work->waiters));
            work->wait_free = (IrGvnWait*)(work->waiters + n);
            const uint64_t spare_bytes = ((uint64_t)n * (BUSTER_IR_GVN_FIELDS - BUSTER_IR_GVN_ENTRY_WORDS) - buckets) * sizeof(*spare);
            work->wait_left = (spare_bytes - (uint64_t)n * sizeof(*work->waiters)) / sizeof(*work->wait_free);
        }
    }
    const uint64_t budget = work->statistics.partition_probes + (uint64_t)n * work->probes;
    bool ok = true;
    const uint32_t* traversal = dominators->rpo;
    if (work->fused)
    {
        // Without phis every operand dominates its use. Dominator preorder is also a dependency order and gives
        // availability without a second hash-table traversal. Publish substitutions only when numbering succeeds.
        for (uint32_t block = 0; block < f->block_count; block += 1)
        {
            work->members[dominators->preorder[block]] = block;
        }
        traversal = work->members;
        work->staged_targets = work->temporary_order;
    }
    for (uint32_t position = 0; ok && position < dominators->reachable_count; position += 1)
    {
        work->position = position;
        work->current_block = traversal[position];
        ok = ir_gvn_visit_block(work, work->current_block, buckets, budget, count);
    }
    for (uint32_t block = 0; ok && dominators->reachable_count < f->block_count && block < f->block_count; block += 1)
    {
        if (dominators->rpo_index[block] == BUSTER_DOMINATOR_NONE)
        {
            ok = ir_gvn_visit_block(work, block, buckets, budget, count);
        }
    }
    if (ok)
    {
        work->statistics.acyclic = n - work->pending;
        if (work->pending != 0)
        {
            ok = ir_gvn_seed_phis(work, buckets, budget, count);
        }
    }
    if (work->fused && ok)
    {
        for (uint32_t k = 0; k < work->staged; k += 1)
        {
            work->alias[work->waiting[k]] = work->staged_targets[k];
        }
        work->merged += work->staged;
    }
    work->fused &= ok;
    arena_pop_to(work->ir.scratch, mark);
    work->statistics.partition_sort_fallbacks += !ok && !work->wait_failed;
    return ok;
}

// Each congruence class has one available representative. In dominator preorder a block occupies one half-open
// interval. Once a representative expires it cannot dominate a later block, so no stack or rollback is needed.
static void ir_gvn_available(IrGvnWork* work, bool vectors)
{
    const IrFunction* f = work->ir.function;
    const Dominators* tree = &work->ir.dominators;
    const __m128i flags = _mm_set1_epi8(work->repeats_only ? 2 : -1);
    uint32_t* block_order = work->temporary_order;
    uint32_t* representative = work->order;
    uint32_t* until = work->next_classes;
    if (work->statistics.classes != 0)
    {
        memset(until, 0, (uint64_t)work->statistics.classes * sizeof(uint32_t));
    }
    for (uint32_t block = 0; block < f->block_count; block += 1)
    {
        if (tree->subtree_end[block] != 0)
        {
            block_order[tree->preorder[block]] = block;
        }
    }
    for (uint32_t position = 0; position < tree->reachable_count; position += 1)
    {
        const uint32_t block = block_order[position];
        const uint32_t end = f->block_offsets[(uint64_t)block + 1];
        for (uint64_t first = f->block_offsets[block]; first < end; first += 16)
        {
            const uint32_t left = end - (uint32_t)first;
            const __mmask16 valid = (__mmask16)_bzhi_u32(0xffff, left < 16 ? left : 16);
            const __m128i pure = _mm_maskz_loadu_epi8(valid, work->pure + first);
            __mmask16 candidates = _mm_test_epi8_mask(pure, flags);
            if (vectors)
            {
                const __m128i types = _mm_maskz_loadu_epi8(valid, f->type + first);
                candidates &= _mm_cmpge_epu8_mask(types, _mm_set1_epi8(IR_V4I32));
            }
            while (candidates != 0)
            {
                const uint32_t i = (uint32_t)first + (uint32_t)__builtin_ctz(candidates);
                candidates &= candidates - 1;
                if (!work->pressure || ir_gvn_free[f->op[i]] == 0)
                {
                    const uint32_t c = work->classes[i] - 1;
                    if (position < until[c])
                    {
                        work->alias[i] = representative[c];
                        work->merged += 1;
                    }
                    else
                    {
                        representative[c] = i;
                        until[c] = work->pressure && f->type[i] == IR_I1 ? position + 1 : tree->subtree_end[block];
                    }
                }
            }
        }
    }
}

// A GVN substitution moves a debug binding's anchor as well as its value. The common IR rewrite handles operands,
// shared extra slices and call arguments with its forwarding map. Only debug records need a private input view.
static IrError ir_gvn_debug_view(IrGvnWork* work, Arena* scratch, IrFunction* result)
{
    const IrFunction* f = work->ir.function;
    IrFunction view = *f;
    IrError error = ir_debug_create(scratch, &view, f->debug) ? IR_OK : IR_ERROR_MEMORY;
    if (error == IR_OK)
    {
        ir_debug_remap(f, &view, NULL, NULL, NULL);
        for (uint32_t k = 0; view.debug != NULL && k < view.debug->binding_count; k += 1)
        {
            IrDebugBinding* binding = view.debug->bindings + k;
            binding->anchor = binding->anchor != BUSTER_IR_NONE ? ir_gvn_resolve(work->alias, binding->anchor) : BUSTER_IR_NONE;
            if (binding->kind == IR_DEBUG_BIND_VALUE)
            {
                binding->value = ir_gvn_resolve(work->alias, binding->value);
            }
        }
        for (uint32_t k = 0; view.debug != NULL && k < view.debug->region_count; k += 1)
        {
            IrDebugRegion* region = view.debug->regions + k;
            if (region->kind == IR_DEBUG_BIND_VALUE)
            {
                region->value = ir_gvn_resolve(work->alias, region->value);
            }
        }
    }
    if (error == IR_OK)
    {
        *result = view;
    }
    return error;
}

// A vector or mask alias has its own type. Restoring every other alias preserves entire vector chains.
static void ir_gvn_keep_vectors(IrGvnWork* work)
{
    const IrFunction* f = work->ir.function;
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint64_t first = 0; first < f->instruction_count; first += 16)
    {
        const uint32_t count = f->instruction_count - first < 16 ? (uint32_t)(f->instruction_count - first) : 16;
        const __mmask16 valid = (__mmask16)((UINT32_C(1) << count) - 1);
        const __m128i types = _mm_maskz_loadu_epi8(valid, f->type + first);
        const __mmask16 own = valid & _mm_cmplt_epu8_mask(types, _mm_set1_epi8(IR_V4I32));
        _mm512_mask_storeu_epi32(work->alias + first, own, _mm512_add_epi32(_mm512_set1_epi32((int32_t)first), lanes));
    }
}

// GVN can remove instructions without changing any edge. Check the rewritten rows, joins and successor lists
// from scratch. When the labeled graph is identical, its existing dominator intervals give the exact SSA check
// for the new instruction map. A different graph or failed check takes the standard verifier and its diagnostics.
static IrCheck ir_gvn_verify(const IrGvnWork* work, Arena* output, Arena* scratch)
{
    const IrFunction* f = &work->statistics.function;
    const IrCfg* before = &work->ir.cfg;
    IrCheck check = ir_check(IR_ERROR_DOMINANCE, BUSTER_IR_NONE, 0);
    if (f->block_count == before->block_count)
    {
        const uint64_t mark = scratch->length;
        IrCfg cfg = { 0 };
        check = ir_prepare_cfg(f, scratch, output, &cfg, false);
        if (check.error == IR_OK)
        {
            const bool same = cfg.edge_count == before->edge_count &&
                              memcmp(cfg.successor_offsets, before->successor_offsets, ((uint64_t)cfg.block_count + 1) * sizeof(uint32_t)) == 0 &&
                              (cfg.edge_count == 0 || memcmp(cfg.successors, before->successors, (uint64_t)cfg.edge_count * sizeof(uint32_t)) == 0);
            if (same)
            {
                const IrDominance tree = { .block_count = cfg.block_count, .enter = work->ir.dominators.preorder, .leave = work->ir.dominators.subtree_end };
                check = ir_verify_dominance(f, &cfg, &tree);
            }
            else
            {
                check.error = IR_ERROR_DOMINANCE;
            }
        }
        arena_pop_to(scratch, mark);
    }
    return check.error == IR_OK ? check : ir_optimize_verify_result(f, output, scratch);
}

// In verified reachable SSA, once phis are atoms every other use follows its definition in dominator
// preorder. A phi is an atom if no other phi in its block can match it. Prove that using static input
// keys, which are already necessary for congruence even for an unresolved back edge. A fingerprint
// collision declines the proof. Check the entire function before changing any class or eligibility bit.
static bool ir_gvn_distinct_phis(IrGvnWork* work)
{
    const IrFunction* f = work->ir.function;
    bool distinct = true;
    for (uint32_t block = 0; distinct && block < f->block_count; block += 1)
    {
        const uint32_t begin = f->block_offsets[block];
        const uint32_t end = f->block_offsets[(uint64_t)block + 1];
        if (end - begin <= 1 || f->op[begin + 1] != IR_PHI)
        {
            continue;
        }
        uint64_t seen = 0;
        for (uint32_t i = begin; distinct && i < end && f->op[i] == IR_PHI; i += 1)
        {
            if (work->pure[i] == 0)
            {
                continue;
            }
            uint64_t hash = ir_gvn_meta(f, i);
            // An order-independent sum also covers phis whose predecessor/value pairs are permuted.
            for (uint32_t k = 0; k < f->b[i]; k += 1)
            {
                const uint32_t* input = f->extra + f->a[i] + (uint64_t)k * 2;
                const uint32_t v = work->alias[input[1]];
                const IrGvnPayload payload = ir_gvn_payload(work, v);
                uint64_t term = ir_gvn_mix(ir_gvn_meta(f, v), work->pure[v] == 0 ? (uint64_t)v + 1 : 0);
                term = ir_gvn_mix(ir_gvn_mix(term, payload.low), payload.high);
                hash += ir_gvn_mix(term, input[0]);
            }
            const uint64_t bit = UINT64_C(1) << ((hash ^ (hash >> 32)) & 63);
            distinct = (seen & bit) == 0;
            seen |= bit;
        }
    }
    if (distinct)
    {
        for (uint32_t block = 0; block < f->block_count; block += 1)
        {
            const uint32_t end = f->block_offsets[(uint64_t)block + 1];
            for (uint32_t i = f->block_offsets[block]; i < end && f->op[i] == IR_PHI; i += 1)
            {
                if (work->pure[i] != 0)
                {
                    work->classes[i] = ++work->unique;
                    work->pure[i] = 0;
                }
            }
        }
    }
    return distinct;
}

static IrError ir_gvn_solve(IrGvnWork* work, IrGvnOptions options)
{
    // Classes 1..numbered are the acyclic numbering's; the refinement numbers the rest after them.
    const uint32_t n = work->ir.function->instruction_count;
    const bool census_omitted = !work->ordered && options.apply_only && !options.complete &&
                                work->ir.dominators.reachable_count == work->ir.function->block_count && ir_gvn_distinct_phis(work);
    work->ordered |= census_omitted;
    uint32_t numbered = work->unique;
    work->fused = work->ordered && !options.complete;
    work->vectors_only = options.vectors && !options.pressure;
    const bool acyclic = !options.complete && ir_gvn_acyclic(work, &numbered);
    if (work->wait_failed)
    {
        return IR_ERROR_MEMORY;
    }
    if (census_omitted)
    {
        work->statistics.acyclic = BUSTER_IR_NONE;
    }
    work->repeats_only = acyclic;
    if (acyclic && work->pending == 0)
    {
        work->statistics.classes = numbered;
        return IR_OK;
    }
    uint32_t count = 0;
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint64_t first = 0; first < n; first += 16)
    {
        const __mmask16 valid = (__mmask16)_bzhi_u32(0xFFFF, n - (uint32_t)first < 16 ? n - (uint32_t)first : 16);
        const __m512i ids = _mm512_add_epi32(lanes, _mm512_set1_epi32((int)first));
        const __m512i classes = _mm512_maskz_loadu_epi32(valid, work->classes + first);
        const __mmask16 pending = acyclic ? _mm512_mask_cmpeq_epi32_mask(valid, classes, _mm512_setzero_si512()) : valid;
        if (acyclic)
        {
            // Cyclic members still need refinement. Keep them eligible regardless of the class they receive.
            _mm_mask_storeu_epi8(work->pure + first, pending, _mm_set1_epi8(2));
        }
        _mm512_mask_compressstoreu_epi32(work->members + count, pending, ids);
        count += (uint32_t)__builtin_popcount(pending);
    }
    numbered = acyclic ? numbered : 0;
    if (!acyclic)
    {
        work->statistics.acyclic = 0;
    }
    work->statistics.classes = numbered + (count != 0 ? ir_gvn_refine(work, count, numbered, options.sweep_limit, !options.complete) : 0);
    return IR_OK;
}

// GVN keeps reachable blocks in their original order and substitutes congruent values. Number retained rows once, compress their seven
// columns in order and rebuild shared extra slices in the same order as the general rewrite.
static IrError ir_gvn_compact(IrGvnWork* work, Arena* output)
{
    const IrFunction* f = work->ir.function;
    const IrCfg* cfg = &work->ir.cfg;
    IrSizes sizes = ir_optimize_metadata_sizes(f);
    const bool pruning = work->ir.dominators.reachable_count != f->block_count;
    sizes.block_count = work->ir.dominators.reachable_count;
    uint32_t* blocks = work->order;
    uint32_t kept_blocks = 0;
    for (uint32_t block = 0; block < f->block_count; block += 1)
    {
        if (pruning && work->ir.dominators.rpo_index[block] == BUSTER_DOMINATOR_NONE)
        {
            blocks[block] = BUSTER_IR_NONE;
            const uint32_t begin = f->block_offsets[block];
            memset(work->alias + begin, 0xff, (uint64_t)(f->block_offsets[(uint64_t)block + 1] - begin) * sizeof(uint32_t));
        }
        else
        {
            blocks[block] = kept_blocks++;
        }
    }
    uint32_t* places = work->classes;
    uint32_t* operands = work->next_classes;
    uint32_t* special_rows = work->temporary_order;
    uint32_t special_count = 0;
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint64_t first = 0; first < f->instruction_count; first += 16)
    {
        const uint32_t left = f->instruction_count - (uint32_t)first;
        const __mmask16 active = (__mmask16)_bzhi_u32(0xffff, left < 16 ? left : 16);
        const __m512i ids = _mm512_add_epi32(lanes, _mm512_set1_epi32((int)first));
        const __m512i aliases = _mm512_maskz_loadu_epi32(active, work->alias + first);
        const __mmask16 kept = _mm512_mask_cmpeq_epi32_mask(active, aliases, ids);
        const __m512i numbered = _mm512_add_epi32(lanes, _mm512_set1_epi32((int)sizes.instruction_count));
        _mm512_mask_storeu_epi32(places + first, active, _mm512_mask_expand_epi32(_mm512_set1_epi32(-1), kept, numbered));
        sizes.instruction_count += (uint32_t)__builtin_popcount(kept);
        const __m128i op = _mm_maskz_loadu_epi8(active, f->op + first);
        const __m512i kind = ir_lookup_bytes(ir_optimize_rewrite_kinds, op);
        const __mmask16 special = kept & (_mm512_test_epi32_mask(kind, kind) | _mm_cmpeq_epi8_mask(op, _mm_set1_epi8(IR_BR)) |
                                          _mm_cmpeq_epi8_mask(op, _mm_set1_epi8(IR_SCATTER)) | _mm_cmpeq_epi8_mask(op, _mm_set1_epi8(IR_INTRINSIC)));
        _mm512_mask_compressstoreu_epi32(special_rows + special_count, special, ids);
        special_count += (uint32_t)__builtin_popcount(special);
    }
    for (uint32_t k = 0; k < special_count; k += 1)
    {
        const uint32_t i = special_rows[k];
        const uint32_t kind = ir_optimize_rewrite_kinds[f->op[i]];
        if (kind == 1)
        {
            uint32_t count = f->b[i];
            if (pruning)
            {
                count = 0;
                for (uint32_t j = 0; j < f->b[i]; j += 1)
                {
                    count += blocks[f->extra[f->a[i] + (uint64_t)j * 2]] != BUSTER_IR_NONE;
                }
            }
            sizes.extra_count += (uint64_t)2 * count;
        }
        else if (kind == 2)
        {
            sizes.call_count += 1;
            sizes.extra_count += f->calls[f->b[i]].argument_count;
        }
        else if (kind == 4)
        {
            const uint32_t block = cfg->instruction_blocks[i];
            if (cfg->successor_offsets[block + 1] - cfg->successor_offsets[block] != 1)
            {
                sizes.extra_count += 1 + (uint64_t)2 * f->c[i];
            }
        }
        else if (kind == 5 && f->b[i] != BUSTER_IR_NONE)
        {
            sizes.extra_count += f->c[i];
        }
        else
        {
            sizes.extra_count += ir_extra_value_count(f, i);
        }
    }
    IrFunction result = { 0 };
    const IrError error = ir_create(output, &sizes, &result);
    if (error != IR_OK)
    {
        return error;
    }
    if (!ir_debug_create(output, &result, f->debug))
    {
        return IR_ERROR_MEMORY;
    }
    ir_optimize_copy_metadata(f, &result);
    for (uint32_t k = 0; k < f->label_count; k += 1)
    {
        result.labels[k] = f->labels[k] == BUSTER_IR_NONE ? BUSTER_IR_NONE : blocks[f->labels[k]];
    }
    for (uint32_t b = 0; b < f->block_count; b += 1)
    {
        if (blocks[b] != BUSTER_IR_NONE)
        {
            result.block_offsets[blocks[b] + 1] = places[f->block_offsets[b + 1] - 1] + 1;
        }
    }
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        operands[i] = work->alias[i] == BUSTER_IR_NONE ? BUSTER_IR_NONE : places[ir_gvn_resolve(work->alias, i)];
    }
    // AVX-512 gathers sign-extend the index. Center the base of a table wider than INT32_MAX.
    const uint32_t center = f->instruction_count > INT32_MAX ? UINT32_C(1) << 31 : 0;
    uint32_t out = 0;
    for (uint64_t first = 0; first < f->instruction_count; first += 16)
    {
        const uint32_t left = f->instruction_count - (uint32_t)first;
        const __mmask16 active = (__mmask16)_bzhi_u32(0xffff, left < 16 ? left : 16);
        const __m512i ids = _mm512_add_epi32(lanes, _mm512_set1_epi32((int)first));
        const __mmask16 kept = _mm512_mask_cmpeq_epi32_mask(active, _mm512_maskz_loadu_epi32(active, work->alias + first), ids);
        if (kept == 0)
        {
            continue;
        }
        const __m128i op = _mm_maskz_loadu_epi8(active, f->op + first);
        _mm_mask_compressstoreu_epi8(result.op + out, kept, op);
        _mm_mask_compressstoreu_epi8(result.type + out, kept, _mm_maskz_loadu_epi8(active, f->type + first));
        _mm_mask_compressstoreu_epi8(result.flags + out, kept, _mm_maskz_loadu_epi8(active, f->flags + first));
        _mm_mask_compressstoreu_epi8(result.alignment_log2 + out, kept, _mm_maskz_loadu_epi8(active, f->alignment_log2 + first));
        const __m512i fixed = ir_lookup_bytes(ir_operand_table, op);
        const __m512i optional = ir_lookup_bytes(ir_optional_table, op);
        const uint32_t* inputs[] = { f->a, f->b, f->c };
        uint32_t* outputs[] = { result.a, result.b, result.c };
        for (uint32_t col = 0; col < 3; col += 1)
        {
            const __m512i bits = _mm512_set1_epi32(1 << col);
            const __m512i values = _mm512_maskz_loadu_epi32(active, inputs[col] + first);
            const __mmask16 use = kept & (_mm512_test_epi32_mask(fixed, bits) | (_mm512_test_epi32_mask(optional, bits) &
                                          _mm512_cmpneq_epi32_mask(values, _mm512_set1_epi32(-1))));
            const __m512i rewritten = _mm512_mask_i32gather_epi32(values, use, _mm512_sub_epi32(values, _mm512_set1_epi32((int)center)), operands + center, 4);
            _mm512_mask_compressstoreu_epi32(outputs[col] + out, kept, rewritten);
        }
        out += (uint32_t)__builtin_popcount(kept);
    }
    uint32_t extra = 0;
    uint32_t calls = 0;
    for (uint32_t k = 0; k < special_count; k += 1)
    {
        const uint32_t i = special_rows[k];
        const uint32_t to = places[i];
        const uint32_t kind = ir_optimize_rewrite_kinds[f->op[i]];
        const uint32_t block = cfg->instruction_blocks[i];
        const uint32_t first_edge = cfg->successor_offsets[block];
        if (f->op[i] == IR_BR || ((kind == 3 || kind == 4) && cfg->successor_offsets[block + 1] - first_edge == 1))
        {
            result.op[to] = IR_BR;
            result.flags[to] = 0;
            result.a[to] = blocks[cfg->successors[first_edge]];
            result.b[to] = BUSTER_IR_NONE;
            result.c[to] = BUSTER_IR_NONE;
        }
        else if (kind == 1)
        {
            result.a[to] = extra;
            result.b[to] = 0;
            for (uint32_t j = 0; j < f->b[i]; j += 1)
            {
                const uint32_t* pair = f->extra + f->a[i] + (uint64_t)j * 2;
                if (blocks[pair[0]] != BUSTER_IR_NONE)
                {
                    result.extra[extra++] = blocks[pair[0]];
                    result.extra[extra++] = operands[pair[1]];
                    result.b[to] += 1;
                }
            }
        }
        else if (kind == 2)
        {
            const IrCall* call = f->calls + f->b[i];
            result.b[to] = calls;
            result.calls[calls] = *call;
            result.calls[calls++].argument_first = extra;
            for (uint32_t j = 0; j < call->argument_count; j += 1)
            {
                result.extra[extra++] = operands[f->extra[call->argument_first + j]];
            }
        }
        else if (kind == 3)
        {
            result.b[to] = blocks[f->b[i]];
            result.c[to] = blocks[f->c[i]];
        }
        else if (kind == 4)
        {
            const uint32_t* entries = f->extra + f->b[i];
            result.b[to] = extra;
            result.extra[extra++] = blocks[entries[0]];
            for (uint32_t j = 0; j < f->c[i]; j += 1)
            {
                result.extra[extra++] = entries[1 + (uint64_t)j * 2];
                result.extra[extra++] = blocks[entries[2 + (uint64_t)j * 2]];
            }
        }
        else if (kind == 5 && f->b[i] != BUSTER_IR_NONE)
        {
            result.b[to] = extra;
            if (f->c[i] != 0)
            {
                memcpy(result.extra + extra, f->extra + f->b[i], (uint64_t)f->c[i] * sizeof(uint32_t));
                extra += f->c[i];
            }
        }
        else
        {
            const uint32_t count = ir_extra_value_count(f, i);
            if (count != 0)
            {
                result.c[to] = ir_intrinsic_ternary_c(f->c[i], extra);
                for (uint32_t j = 0; j < count; j += 1)
                {
                    result.extra[extra++] = operands[ir_extra_value(f, i, j)];
                }
            }
        }
    }
    ir_debug_remap(f, &result, places, NULL, blocks);
    work->statistics.function = result;
    return IR_OK;
}

static __attribute__((unused)) IrCheck ir_optimize_gvn(const IrFunction* function, Arena* output, Arena* scratch, IrGvnOptions options, IrGvnResult* result)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrGvnWork work = { 0 };
    IrCheck check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, 0);
    work.ir.validated = options.validated;
    work.ir.analysis_only = options.apply_only;
    work.ir.without_uses = true;
    if (output != scratch)
    {
        check = ir_optimize_prepare(&work.ir, function, output, scratch, true);
    }
    if (check.error == IR_OK)
    {
        bool ok = true;
        const uint64_t n = function->instruction_count;
        // Only the class and undef-taint columns need initial zeros. The partition's sort
        // and availability walk reuse the same columns. One reservation also avoids clearing the four-word keys
        // again before the dependency walk overwrites them.
        const uint64_t bytes_per_instruction = (BUSTER_IR_GVN_FIELDS + 1) * sizeof(uint64_t) + 7 * sizeof(uint32_t) + 2;
        uint8_t* const storage = n != 0 ? arena_push_array(scratch, n, bytes_per_instruction, 64) : NULL;
        ok = n == 0 || storage != NULL;
        if (storage != NULL)
        {
            work.fields = (uint64_t*)storage;
            work.hashes = work.fields + n * BUSTER_IR_GVN_FIELDS;
            work.alias = (uint32_t*)(work.hashes + n);
            work.next_classes = work.alias + n;
            work.order = work.next_classes + n;
            work.temporary_order = work.order + n;
            work.members = work.temporary_order + n;
            work.classes = work.members + n;
            work.waiting = work.classes + n;
            work.tainted = (uint8_t*)(work.waiting + n);
            work.pure = work.tainted + n;
            memset(work.classes, 0, n * sizeof(uint32_t));
            memset(work.tainted, 0, n);
        }
        work.probes = options.probes;
        work.pressure = options.pressure;
        check.error = ok ? ir_gvn_prepare_uses(&work, scratch, output) : IR_ERROR_MEMORY;
    }
    if (check.error == IR_OK)
    {
        ir_gvn_classify(&work, !options.complete);
        check.error = ir_gvn_solve(&work, options);
        if (check.error == IR_OK && !work.fused)
        {
            ir_gvn_available(&work, options.vectors && !options.pressure);
        }
        if (check.error == IR_OK && options.vectors)
        {
            ir_gvn_keep_vectors(&work);
        }
    }
    // IrGvnOptions.pressure: a function where nothing merges stays the caller's, unwritten.
    const bool rewritten = check.error == IR_OK && (!options.pressure || work.merged != 0);
    IrFunction view = { 0 };
    if (rewritten && function->debug != NULL)
    {
        check.error = ir_gvn_debug_view(&work, scratch, &view);
    }
    work.ir.function = rewritten && function->debug != NULL ? &view : function;
    if (rewritten && check.error == IR_OK && options.apply_only)
    {
        check.error = ir_gvn_compact(&work, output);
    }
    else if (rewritten && check.error == IR_OK)
    {
        for (uint32_t block = 0; block < function->block_count; block += 1)
        {
            work.ir.blocks[block] = work.ir.dominators.rpo_index[block] != BUSTER_DOMINATOR_NONE;
            for (uint32_t edge = work.ir.cfg.successor_offsets[block]; edge < work.ir.cfg.successor_offsets[(uint64_t)block + 1]; edge += 1)
            {
                work.ir.edges[edge] = work.ir.blocks[block];
            }
        }
        for (uint32_t i = 0; i < function->instruction_count; i += 1)
        {
            work.ir.states[i] = IROPT_OVERDEFINED;
            work.ir.live[i] = ir_gvn_resolve(work.alias, i) == i && work.ir.blocks[work.ir.cfg.instruction_blocks[i]] != 0;
        }
        work.ir.forward = work.alias;
        IrOptimizeOptions rewrite = ir_optimize_options();
        rewrite.passes = 0;
        check.error = ir_optimize_rewrite(&work.ir, output, scratch, rewrite, &work.statistics.function);
    }
    if (rewritten && check.error == IR_OK)
    {
        check = ir_gvn_verify(&work, output, scratch);
    }
    if (check.error == IR_OK)
    {
        work.statistics.function = rewritten ? work.statistics.function : *function;
        work.statistics.removed = function->instruction_count - work.statistics.function.instruction_count;
        work.statistics.scratch_bytes = scratch->length - scratch_mark;
        *result = work.statistics;
    }
    else
    {
        arena_pop_to(output, output_mark);
    }
    arena_pop_to(scratch, scratch_mark);
    return check;
}
