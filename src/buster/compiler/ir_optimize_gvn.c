// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original SSA value numbering (#139), included after ir_optimize.c. The vector path computes the coarsest congruence
// partition: values no cycle is reachable from are numbered once each in dependency order (#866), and only the values
// that reach a cycle are refined in rounds. The independent scalar path hashes available expressions while walking
// the dominator tree with scope rollback. Hashes never establish equality: full keys and predecessor-ordered phi
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
    bool scalar;
    // Refine every instruction in synchronous rounds instead of numbering the acyclic ones first: the exact oracle
    // of the acyclic numbering.
    bool complete;
};

typedef struct IrGvnResult IrGvnResult;
struct IrGvnResult
{
    IrFunction function;
    uint32_t removed;
    uint32_t classes;
    // Instructions the acyclic numbering gave their final class; the rest were refined in rounds.
    uint32_t acyclic;
    uint64_t rounds;
    uint64_t partition_probes;
    uint64_t partition_sort_fallbacks;
    uint64_t scratch_bytes;
    bool fallback;
};

typedef struct IrGvnFrame IrGvnFrame;
struct IrGvnFrame
{
    uint32_t block;
    uint32_t child;
    uint32_t mark;
    bool entered;
};

typedef struct IrGvnPayload IrGvnPayload;
struct IrGvnPayload
{
    uint64_t low;
    uint64_t high;
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
    uint8_t* awaited;
    uint32_t probes;
    IrGvnResult statistics;
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

static void ir_gvn_classify(IrGvnWork* work)
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
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        work->alias[i] = i;
        if (f->op[i] == IR_UNDEF)
        {
            work->tainted[i] = 1;
            ir_optimize_enqueue(&work->ir, i);
        }
    }
    while (work->ir.queued_count != 0)
    {
        const uint32_t value = ir_optimize_dequeue(&work->ir);
        for (uint32_t use = work->ir.uses.offsets[value]; use < work->ir.uses.offsets[(uint64_t)value + 1]; use += 1)
        {
            const uint32_t instruction = work->ir.uses.uses[use].instruction;
            if (work->tainted[instruction] == 0)
            {
                work->tainted[instruction] = 1;
                ir_optimize_enqueue(&work->ir, instruction);
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
        // its third operand's place past it), the AES rounds and the floating operations at their own rounding (which
        // read no MXCSR and set none of its flags) are pure and total.
        const __m512i kind_bits = _mm512_and_si512(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_KIND));
        const __mmask16 ternary = _mm512_cmpge_epu32_mask(kind_bits, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_TERNARY)) &
                                  _mm512_cmplt_epu32_mask(kind_bits, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_TERNARY_END));
        const __mmask16 splat = _mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(3)) & (_mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_SPLAT)) |
                                                                          (_mm512_cmpge_epu32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_EXTRACT)) &
                                                                           _mm512_cmple_epu32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_LANE_OP))) | ternary |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_AES)) |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_FROUND)));
        const __mmask16 eligible = (_mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(1)) | integer_compare | splat) & _mm_cmpeq_epi8_mask(tainted, _mm_setzero_si128());
        _mm_mask_storeu_epi8(work->pure + first, valid, _mm_maskz_set1_epi8(eligible, 1));
    }
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

static uint32_t ir_gvn_phi_class(const IrGvnWork* work, uint32_t instruction, uint32_t predecessor)
{
    const IrFunction* f = work->ir.function;
    uint32_t result = 0;
    for (uint32_t incoming = 0; incoming < f->b[instruction]; incoming += 1)
    {
        const uint32_t at = f->a[instruction] + incoming * 2;
        if (f->extra[at] == predecessor)
        {
            result = work->classes[work->alias[f->extra[at + 1]]];
        }
    }
    return result;
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
        // (ir_gvn_number) and in the refinement (ir_gvn_hash_one, ir_gvn_compare): table indices alone would miss
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
static void ir_gvn_key(IrGvnWork* work, uint32_t position, uint32_t i, bool initial, bool parent)
{
    const IrFunction* f = work->ir.function;
    const uint64_t n = f->instruction_count;
    const uint32_t mask = ir_value_mask3(f, i);
    const IrGvnPayload payload = ir_gvn_payload(work, i);
    work->fields[position] = ir_gvn_meta(f, i);
    work->fields[n + position] = parent && !initial ? work->classes[i] : 0;
    work->fields[n * 2 + position] = payload.low;
    work->fields[n * 3 + position] = payload.high;
    work->fields[n * 4 + position] = !initial && (mask & 1) != 0 ? work->classes[ir_gvn_resolve(work->alias, f->a[i])] : 0;
    work->fields[n * 5 + position] = !initial && (mask & 2) != 0 ? work->classes[ir_gvn_resolve(work->alias, f->b[i])] : 0;
    work->fields[n * 6 + position] = !initial && (mask & 4) != 0 ? work->classes[ir_gvn_resolve(work->alias, ir_value_third(f, i))] : 0;
    work->fields[n * 7 + position] = work->pure[i] == 0 ? (uint64_t)i + 1 : 0;
}

static uint64_t ir_gvn_hash_one(const IrGvnWork* work, uint32_t position, uint32_t i, bool initial)
{
    const IrFunction* f = work->ir.function;
    uint64_t hash = BUSTER_IR_GVN_HASH_BASIS;
    for (uint32_t field = 0; field < BUSTER_IR_GVN_FIELDS; field += 1)
    {
        hash = ir_gvn_mix(hash, work->fields[(uint64_t)field * f->instruction_count + position]);
    }
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
            hash = ir_gvn_mix(hash, ir_gvn_phi_class(work, i, work->ir.cfg.predecessors[edge]));
        }
    }
    return hash;
}

// Keys and hashes of the first count members, by position.
static void ir_gvn_keys(IrGvnWork* work, uint32_t count, bool initial, bool scalar)
{
    (void)scalar;

    const uint64_t n = work->ir.function->instruction_count;
    for (uint32_t position = 0; position < count; position += 1)
    {
        ir_gvn_key(work, position, work->members[position], initial, true);
    }
    for (uint64_t first = 0; first < count; first += BUSTER_IR_OPTIMIZE_BATCH)
    {
        const uint32_t batch = count - first < BUSTER_IR_OPTIMIZE_BATCH ? count - first : BUSTER_IR_OPTIMIZE_BATCH;
        if (!BUSTER_SCALAR_PATH(scalar))
        {
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
        }
        for (uint32_t lane = 0; lane < batch; lane += 1)
        {
            const uint32_t position = first + lane;
            const uint32_t i = work->members[position];
            if (BUSTER_SCALAR_PATH(scalar) || work->ir.function->op[i] == IR_VCONST || (!initial && work->ir.function->op[i] == IR_PHI && work->pure[i] != 0))
            {
                work->hashes[position] = ir_gvn_hash_one(work, position, i, initial);
            }
        }
    }
}

// Compares the keys at positions a and b, of instructions ia and ib.
static int ir_gvn_compare(const IrGvnWork* work, uint32_t a, uint32_t b, uint32_t ia, uint32_t ib, bool initial)
{
    int result = work->hashes[a] < work->hashes[b] ? -1 : work->hashes[a] != work->hashes[b];
    const IrFunction* f = work->ir.function;
    for (uint32_t field = 0; result == 0 && field < BUSTER_IR_GVN_FIELDS; field += 1)
    {
        const uint64_t left = work->fields[(uint64_t)field * f->instruction_count + a];
        const uint64_t right = work->fields[(uint64_t)field * f->instruction_count + b];
        result = left < right ? -1 : left != right;
    }
    if (result == 0 && f->op[ia] == IR_VCONST)
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
            const uint32_t left = ir_gvn_phi_class(work, ia, predecessor);
            const uint32_t right = ir_gvn_phi_class(work, ib, predecessor);
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

static uint32_t ir_gvn_partition(IrGvnWork* work, uint32_t count, bool initial, bool scalar, uint32_t base)
{
    ir_gvn_keys(work, count, initial, BUSTER_SCALAR_PATH(scalar));
    const uint32_t classes = ir_gvn_partition_group(work, count, initial, scalar, base);
    for (uint32_t position = 0; position < count; position += 1)
    {
        work->classes[work->members[position]] = work->next_classes[position];
    }
    work->statistics.rounds += 1;
    return classes;
}

// Refine the first count members from their static keys to the coarsest partition stable under operand classes. An
// operand outside the members keeps its published class throughout. After sweep_limit rounds, scalar hashing and
// comparison sorting complete the same fixed point.
static uint32_t ir_gvn_refine(IrGvnWork* work, uint32_t count, uint32_t base, uint32_t sweep_limit)
{
    uint32_t classes = ir_gvn_partition(work, count, true, false, base);
    bool changed = true;
    while (changed)
    {
        const bool fallback = work->statistics.rounds > sweep_limit;
        const uint32_t refined = ir_gvn_partition(work, count, false, fallback, base);
        changed = refined != classes;
        classes = refined;
        work->statistics.fallback = work->statistics.fallback || fallback;
    }
    return classes;
}

// #866: the acyclic numbering. Partition refinement needs as many rounds as the longest dependency chain, each over
// every instruction. But a value no cycle of operand dependences is reachable from is congruent exactly to the values
// with its static key and congruent operands, by induction on its depth, and never to a value that reaches a cycle:
// matching operands position by position would follow that cycle forever. So these values are numbered once each,
// in dependency order, by hashing (static key, operand classes) into a table of representatives: Kahn's algorithm,
// with use lists to wake an instruction whose operand was numbered after it was visited. Whatever is left reaches a
// cycle, and only those values are refined in rounds, with the numbered operands' classes fixed. The partition is the
// coarsest congruence either way; only class labels may differ from the synchronous refinement's.
//
// The visits follow reverse postorder, so in reachable SSA code every operand but a phi's input along a back edge is
// numbered first and the use lists are walked only from the few values such a phi waits for. Numbering a value is a
// dependent chain (its key needs its operands' classes, published by the instructions just before it), so it is
// scalar; the refinement rounds keep the eight-lane key hashing.
//
// A representative's key is four words at four times its index in the field columns: payload low and high, the meta
// word with operand class c above it, and operand classes a and b. Its hash is in hashes, its chain link in order and
// the bucket heads in temporary_order.
static uint32_t ir_gvn_waiting(IrGvnWork* work, uint32_t i)
{
    const IrFunction* f = work->ir.function;
    uint32_t waiting = 0;
    if (work->pure[i] != 0 && f->op[i] == IR_PHI)
    {
        // A verified phi has exactly one input per predecessor edge, so every input is part of its key.
        for (uint32_t incoming = 0; incoming < f->b[i]; incoming += 1)
        {
            const uint32_t value = work->alias[f->extra[f->a[i] + incoming * 2 + 1]];
            const bool unnumbered = work->classes[value] == 0;
            waiting += unnumbered;
            work->awaited[value] |= unnumbered;
        }
    }
    else if (work->pure[i] != 0)
    {
        const uint32_t mask = ir_value_mask3(f, i);
        const uint32_t operands[3] = { f->a[i], f->b[i], ir_value_third(f, i) };
        for (uint32_t operand = 0; operand < 3; operand += 1)
        {
            if ((mask >> operand & 1) != 0)
            {
                const uint32_t value = work->alias[operands[operand]];
                const bool unnumbered = work->classes[value] == 0;
                waiting += unnumbered;
                work->awaited[value] |= unnumbered;
            }
        }
    }
    return waiting;
}

// Number instruction i, whose operands all have classes. Returns false, publishing nothing for i, when the probe
// budget runs out with candidates left in its chain.
static bool ir_gvn_number(IrGvnWork* work, uint32_t i, uint32_t buckets, uint64_t budget, uint32_t* count)
{
    const IrFunction* f = work->ir.function;
    bool ok = true;
    if (work->pure[i] == 0)
    {
        *count += 1;
        work->classes[i] = *count;
    }
    else
    {
        const uint32_t mask = ir_value_mask3(f, i);
        const IrGvnPayload payload = ir_gvn_payload(work, i);
        const uint64_t a = (mask & 1) != 0 ? work->classes[work->alias[f->a[i]]] : 0;
        const uint64_t b = (mask & 2) != 0 ? work->classes[work->alias[f->b[i]]] : 0;
        const uint64_t c = (mask & 4) != 0 ? work->classes[work->alias[ir_value_third(f, i)]] : 0;
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
        const uint32_t block = work->ir.cfg.instruction_blocks[i];
        const uint32_t first_edge = work->ir.cfg.predecessor_offsets[block];
        const uint32_t end_edge = work->ir.cfg.predecessor_offsets[(uint64_t)block + 1];
        for (uint32_t edge = first_edge; phi && edge < end_edge; edge += 1)
        {
            hash = ir_gvn_mix(hash, ir_gvn_phi_class(work, i, work->ir.cfg.predecessors[edge]));
        }
        const uint32_t bucket = (hash ^ (hash >> 32)) & (buckets - 1);
        uint32_t match = work->temporary_order[bucket];
        bool equal = false;
        while (match != BUSTER_IR_NONE && !equal && work->statistics.partition_probes < budget)
        {
            work->statistics.partition_probes += 1;
            const uint64_t* entry = work->fields + (uint64_t)match * BUSTER_IR_GVN_ENTRY_WORDS;
            equal = work->hashes[match] == hash && entry[0] == key[0] && entry[1] == key[1] && entry[2] == key[2] && entry[3] == key[3];
            // Equal keys make both vector constants: compare their whole payloads.
            equal = equal && (!vector || memcmp(words, f->vector_constants[f->a[match]].words, sizeof(IrSimdValue)) == 0);
            // Equal keys put both phis in one block: compare their inputs in that block's predecessor order.
            for (uint32_t edge = first_edge; phi && equal && edge < end_edge; edge += 1)
            {
                const uint32_t predecessor = work->ir.cfg.predecessors[edge];
                equal = ir_gvn_phi_class(work, i, predecessor) == ir_gvn_phi_class(work, match, predecessor);
            }
            if (!equal)
            {
                match = work->order[match];
            }
        }
        ok = equal || match == BUSTER_IR_NONE;
        if (ok && equal)
        {
            work->classes[i] = work->classes[match];
        }
        else if (ok)
        {
            *count += 1;
            work->classes[i] = *count;
            work->hashes[i] = hash;
            work->order[i] = work->temporary_order[bucket];
            work->temporary_order[bucket] = i;
            memcpy(work->fields + (uint64_t)i * BUSTER_IR_GVN_ENTRY_WORDS, key, sizeof(key));
        }
    }
    return ok;
}

// Value was just numbered and something waits for it: wake its users, and the users of every copy that resolves to
// it, which read its class through the copy. Number every instruction that releases, and wake whatever waits for
// those in turn. next_classes is the stack of released instructions and members the stack of values whose users to
// wake; each instruction is released once and each copy is walked once per walk, so neither stack outgrows n.
static bool ir_gvn_wake(IrGvnWork* work, uint32_t value, uint32_t buckets, uint64_t budget, uint32_t* count)
{
    uint32_t* ready = work->next_classes;
    uint32_t* values = work->members;
    uint32_t depth = 0;
    uint32_t woken = value;
    bool ok = true;
    bool walk = true;
    while (ok && walk)
    {
        uint32_t pending = 1;
        values[0] = woken;
        while (pending != 0)
        {
            pending -= 1;
            const uint32_t used = values[pending];
            for (uint32_t use = work->ir.uses.offsets[used]; use < work->ir.uses.offsets[(uint64_t)used + 1]; use += 1)
            {
                const uint32_t user = work->ir.uses.uses[use].instruction;
                if (work->alias[user] != user)
                {
                    values[pending] = user;
                    pending += 1;
                }
                else if (work->classes[user] == 0 && work->waiting[user] != 0)
                {
                    work->waiting[user] -= 1;
                    if (work->waiting[user] == 0)
                    {
                        ready[depth] = user;
                        depth += 1;
                    }
                }
            }
        }
        walk = false;
        while (ok && !walk && depth != 0)
        {
            depth -= 1;
            woken = ready[depth];
            ok = ir_gvn_number(work, woken, buckets, budget, count);
            walk = ok && work->awaited[woken] != 0;
        }
    }
    return ok;
}

static bool ir_gvn_visit(IrGvnWork* work, uint32_t i, uint32_t buckets, uint64_t budget, uint32_t* count)
{
    const uint32_t waiting = ir_gvn_waiting(work, i);
    bool ok = true;
    if (waiting != 0)
    {
        work->waiting[i] = waiting;
    }
    else
    {
        ok = ir_gvn_number(work, i, buckets, budget, count);
        // A user visited earlier (a phi, along its back edge) may already wait for i.
        if (ok && work->awaited[i] != 0)
        {
            ok = ir_gvn_wake(work, i, buckets, budget, count);
        }
    }
    return ok;
}

// Returns false when the probe budget runs out; the caller then refines every instruction from its static key.
static bool ir_gvn_acyclic(IrGvnWork* work, uint32_t* count)
{
    const IrFunction* f = work->ir.function;
    const uint32_t n = f->instruction_count;
    const Dominators* dominators = &work->ir.dominators;
    uint32_t buckets = 1;
    while (buckets <= n / 2)
    {
        buckets *= 2;
    }
    if (n != 0)
    {
        memset(work->temporary_order, 0xff, (uint64_t)buckets * sizeof(uint32_t));
    }
    const uint64_t budget = work->statistics.partition_probes + (uint64_t)n * work->probes;
    bool ok = true;
    for (uint32_t position = 0; ok && position < dominators->reachable_count; position += 1)
    {
        const uint32_t block = dominators->rpo[position];
        for (uint32_t i = f->block_offsets[block]; ok && i < f->block_offsets[(uint64_t)block + 1]; i += 1)
        {
            ok = ir_gvn_visit(work, i, buckets, budget, count);
        }
    }
    for (uint32_t block = 0; ok && block < f->block_count; block += 1)
    {
        for (uint32_t i = f->block_offsets[block]; ok && dominators->rpo_index[block] == BUSTER_DOMINATOR_NONE && i < f->block_offsets[(uint64_t)block + 1]; i += 1)
        {
            ok = ir_gvn_visit(work, i, buckets, budget, count);
        }
    }
    work->statistics.partition_sort_fallbacks += !ok;
    return ok;
}

static IrError ir_gvn_available(IrGvnWork* work, Arena* scratch, bool scalar)
{
    (void)scalar;

    const IrFunction* f = work->ir.function;
    uint64_t buckets = 1;
    while (buckets < (uint64_t)f->instruction_count * 2 + 1)
    {
        buckets *= 2;
    }
    bool ok = true;
    uint32_t* heads = ir_allocate(scratch, buckets, sizeof(uint32_t), &ok);
    uint32_t* previous = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
    uint32_t* inserted = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
    IrGvnFrame* frames = ir_allocate(scratch, f->block_count, sizeof(IrGvnFrame), &ok);
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    if (ok)
    {
        memset(heads, 0xff, buckets * sizeof(uint32_t));
        if (scalar)
        {
            for (uint32_t i = 0; i < f->instruction_count; i += 1)
            {
                work->classes[i] = i + 1;
                if (f->op[i] == IR_PHI)
                {
                    work->pure[i] = 0;
                }
            }
        }
        uint32_t depth = f->block_count != 0;
        uint32_t insertions = 0;
        if (depth != 0)
        {
            frames[0] = (IrGvnFrame){ .block = 0 };
        }
        while (depth != 0)
        {
            IrGvnFrame* frame = &frames[depth - 1];
            if (!frame->entered)
            {
                frame->entered = true;
                frame->mark = insertions;
                frame->child = work->ir.dominators.tree_offsets[frame->block];
                for (uint32_t i = f->block_offsets[frame->block]; i < f->block_offsets[(uint64_t)frame->block + 1]; i += 1)
                {
                    if (work->pure[i] != 0 && work->alias[i] == i)
                    {
                        if (scalar)
                        {
                            ir_gvn_key(work, i, i, false, false);
                            work->hashes[i] = ir_gvn_hash_one(work, i, i, false);
                        }
                        else
                        {
                            work->hashes[i] = work->classes[i];
                        }
                        const uint64_t bucket = work->hashes[i] & (buckets - 1);
                        uint32_t match = heads[bucket];
                        while (match != BUSTER_IR_NONE && (scalar ? ir_gvn_compare(work, i, match, i, match, false) != 0 : work->classes[i] != work->classes[match]))
                        {
                            match = previous[match];
                        }
                        if (match != BUSTER_IR_NONE)
                        {
                            work->alias[i] = match;
                            if (scalar)
                            {
                                work->classes[i] = work->classes[match];
                            }
                        }
                        else
                        {
                            previous[i] = heads[bucket];
                            heads[bucket] = i;
                            inserted[insertions++] = i;
                        }
                    }
                }
            }
            if (frame->child < work->ir.dominators.tree_offsets[(uint64_t)frame->block + 1])
            {
                const uint32_t child = work->ir.dominators.tree_children[frame->child++];
                frames[depth++] = (IrGvnFrame){ .block = child };
            }
            else
            {
                while (insertions > frame->mark)
                {
                    const uint32_t value = inserted[--insertions];
                    heads[work->hashes[value] & (buckets - 1)] = previous[value];
                }
                depth -= 1;
            }
        }
    }
    return error;
}

// Materialize private operand columns and independent variable slices before remapping. Input side slices may alias
// each other, even with different metadata roles; mutating a copied extra array in place would corrupt such inputs.
static IrError ir_gvn_alias_view(IrGvnWork* work, Arena* scratch, IrFunction* result)
{
    const IrFunction* f = work->ir.function;
    uint64_t extras = 0;
    uint64_t calls = 0;
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        if (f->op[i] == IR_PHI)
        {
            extras += (uint64_t)f->b[i] * 2;
        }
        else if (f->op[i] == IR_SWITCH)
        {
            extras += (uint64_t)f->c[i] * 2 + 1;
        }
        else if (f->op[i] == IR_BR_INDIRECT && f->b[i] != BUSTER_IR_NONE)
        {
            extras += f->c[i];
        }
        else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
        {
            extras += f->calls[f->b[i]].argument_count;
            calls += 1;
        }
        else if (ir_ternary_instruction(f, i))
        {
            extras += 1;
        }
    }
    IrError error = extras <= UINT32_MAX && calls <= UINT32_MAX ? IR_OK : IR_ERROR_LIMIT;
    IrFunction view = *f;
    if (error == IR_OK)
    {
        bool ok = true;
        view.a = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
        view.b = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
        view.c = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
        view.extra = ir_allocate(scratch, extras, sizeof(uint32_t), &ok);
        view.calls = ir_allocate(scratch, calls, sizeof(IrCall), &ok);
        view.extra_count = extras;
        view.call_count = calls;
        error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (error == IR_OK && !ir_debug_create(scratch, &view, f->debug))
    {
        error = IR_ERROR_MEMORY;
    }
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
    uint32_t extra = 0;
    uint32_t call = 0;
    for (uint32_t i = 0; error == IR_OK && i < f->instruction_count; i += 1)
    {
        const uint32_t mask = ir_value_mask(f, i);
        view.a[i] = (mask & 1) != 0 ? ir_gvn_resolve(work->alias, f->a[i]) : f->a[i];
        view.b[i] = (mask & 2) != 0 ? ir_gvn_resolve(work->alias, f->b[i]) : f->b[i];
        view.c[i] = (mask & 4) != 0 ? ir_gvn_resolve(work->alias, f->c[i]) : f->c[i];
        if (f->op[i] == IR_PHI)
        {
            view.a[i] = extra;
            for (uint32_t incoming = 0; incoming < f->b[i]; incoming += 1)
            {
                view.extra[extra++] = f->extra[f->a[i] + incoming * 2];
                view.extra[extra++] = ir_gvn_resolve(work->alias, f->extra[f->a[i] + incoming * 2 + 1]);
            }
        }
        else if (f->op[i] == IR_SWITCH)
        {
            view.b[i] = extra;
            const uint64_t count = (uint64_t)f->c[i] * 2 + 1;
            memcpy(view.extra + extra, f->extra + f->b[i], count * sizeof(uint32_t));
            extra += count;
        }
        else if (f->op[i] == IR_BR_INDIRECT && f->b[i] != BUSTER_IR_NONE)
        {
            view.b[i] = extra;
            memcpy(view.extra + extra, f->extra + f->b[i], (uint64_t)f->c[i] * sizeof(uint32_t));
            extra += f->c[i];
        }
        else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
        {
            view.b[i] = call;
            view.calls[call] = f->calls[f->b[i]];
            view.calls[call].argument_first = extra;
            for (uint32_t argument = 0; argument < view.calls[call].argument_count; argument += 1)
            {
                view.extra[extra++] = ir_gvn_resolve(work->alias, f->extra[f->calls[f->b[i]].argument_first + argument]);
            }
            call += 1;
        }
        else if (ir_ternary_instruction(f, i))
        {
            view.c[i] = ir_intrinsic_ternary_c(f->c[i], extra);
            view.extra[extra++] = ir_gvn_resolve(work->alias, ir_intrinsic_third(f, i));
        }
    }
    if (error == IR_OK)
    {
        *result = view;
    }
    return error;
}

static __attribute__((unused)) IrCheck ir_optimize_gvn(const IrFunction* function, Arena* output, Arena* scratch, IrGvnOptions options, IrGvnResult* result)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrGvnWork work = { 0 };
    IrCheck check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, 0);
    if (output != scratch)
    {
        check = ir_optimize_prepare(&work.ir, function, output, scratch, BUSTER_SCALAR_PATH(options.scalar), true);
    }
    if (check.error == IR_OK)
    {
        bool ok = true;
        const uint64_t n = function->instruction_count;
        work.alias = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        work.classes = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        work.next_classes = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        work.order = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        work.temporary_order = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        work.members = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        work.waiting = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        work.fields = ir_allocate(scratch, n * BUSTER_IR_GVN_FIELDS, sizeof(uint64_t), &ok);
        work.hashes = ir_allocate(scratch, n, sizeof(uint64_t), &ok);
        work.pure = ir_allocate(scratch, n, sizeof(uint8_t), &ok);
        work.tainted = ir_allocate(scratch, n, sizeof(uint8_t), &ok);
        work.awaited = ir_allocate(scratch, n, sizeof(uint8_t), &ok);
        work.probes = options.probes;
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (check.error == IR_OK)
    {
        ir_gvn_classify(&work);
        if (!options.scalar)
        {
            // Classes 1..numbered are the acyclic numbering's; the refinement numbers the rest after them.
            const uint32_t n = function->instruction_count;
            uint32_t numbered = 0;
            const bool acyclic = !options.complete && ir_gvn_acyclic(&work, &numbered);
            uint32_t count = 0;
            for (uint32_t i = 0; i < n; i += 1)
            {
                work.members[count] = i;
                count += !acyclic || work.classes[i] == 0;
            }
            numbered = acyclic ? numbered : 0;
            work.statistics.acyclic = n - count;
            work.statistics.classes = numbered + (count != 0 ? ir_gvn_refine(&work, count, numbered, options.sweep_limit) : 0);
        }
        check.error = ir_gvn_available(&work, scratch, options.scalar);
    }
    IrFunction view = { 0 };
    if (check.error == IR_OK)
    {
        check.error = ir_gvn_alias_view(&work, scratch, &view);
    }
    if (check.error == IR_OK)
    {
        work.ir.function = &view;
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
        IrOptimizeOptions rewrite = ir_optimize_options();
        rewrite.passes = 0;
        rewrite.scalar = BUSTER_SCALAR_PATH(options.scalar);
        check.error = ir_optimize_rewrite(&work.ir, output, scratch, rewrite, &work.statistics.function);
    }
    if (check.error == IR_OK)
    {
        check = ir_optimize_verify_result(&work.statistics.function, output, scratch, BUSTER_SCALAR_PATH(options.scalar));
    }
    if (check.error == IR_OK)
    {
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
