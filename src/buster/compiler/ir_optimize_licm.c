// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original conservative loop-invariant motion (#139). Include after ir_optimize.c. Only total, side-effect-free,
// undef-independent operations move into existing dedicated preheaders of reducible natural loops. Each loop's movable
// values are found by one walk of its own blocks, a value at a time (ir_licm_loop_walk, #2979): LICM's one
// implementation (#2924). On the 9700X, under --tier1-passes=+all on sqlite3.c, Lua and musl, the whole-function vector
// sweeps it replaced took 1.07 to 3.00 times its ticks and their worklist twin 1.35 to 1.85, in every size bin
// (buster-performance's scalar-paths-removal-2924-2026-11-17/c-licm-2979).

typedef struct IrLicmOptions IrLicmOptions;
struct IrLicmOptions
{
    // Move only the values of vector or mask type (#2781, Tier 1's default policy), each with the movable values its
    // operands are: the constants vector code builds at every use, without the general registers' longer live ranges
    // (#2719). A function without loops isn't classified, a loop that holds no such value is passed over, and each
    // other loop is walked over its own blocks (ir_licm_loop_vectors). A loop that holds a call moves nothing (#2743):
    // every vector and mask register is the caller's across a call, so a value moved out of such a loop is stored and
    // reloaded around each of its calls; a vectorized loop inlined into sqlite3VdbeExec had its two splats moved out of
    // the interpreter's loop, and the function grew from 87 KB to 131 KB, 1,788 call stores more. A function where
    // nothing moves is returned as it was given: *result's function is the input's, and nothing is written to the output.
    bool vectors;
    // Move what pays for the register it holds through the loop (#2719, Tier 1's policy for functions without vector or
    // mask values; ir_licm_loop_pressure). Every value moved stays live from the preheader through the whole loop, which
    // Tier 1's allocation then spills where its registers run out:
    // - a loop that holds a call moves nothing: across a call a value needs one of the five callee-saved registers, and
    //   the values the loop already keeps across its calls hold them (Lua's interpreter loop: +305% instructions);
    // - a free value (BUSTER_IR_LICM_FREE: a constant, a symbol's or a frame object's address, a cast of no bits, or
    //   arithmetic of free values of the loop alone) moves only as the operand of a costly value that moves: at its
    //   users, selection folds it into an immediate or an address, or it costs nothing;
    // - a truth value stays: a branch tests its comparison's flags, where a hoisted one takes a register and a setcc;
    // - a loop moves at most BUSTER_IR_LICM_REGISTERS costly general-register values, less the ones live through it.
    // A function where nothing moves is returned as it was given, as with `vectors`. `vectors` takes precedence over it.
    bool pressure;
    // The function is verified already (IrOptimizeWork.validated, #2719: Tier 1's): its preparation builds the CFG and
    // the loops without verifying it again, and with `pressure` and no undef, no use lists. A function the pass rewrites
    // is verified as ever.
    bool validated;
};

// IrLicmOptions.pressure's register budget: the general registers Tier 1 allocates past rsp, rbp and its scratch ones,
// less two for the loop's own temporaries.
#define BUSTER_IR_LICM_REGISTERS 12
// ir_licm_classify's `free` kinds: 1, a value at no cost where it is used (a constant, an address, a cast of no bits); 2,
// arithmetic that is free where its operands are free values of the loop (a constant's sum or an address's offset).
#define BUSTER_IR_LICM_FREE 1
#define BUSTER_IR_LICM_FREE_ARITHMETIC 2
// IrLicmWork.candidate's states under IrLicmOptions.pressure: a costly movable value moves; a free one is pending until
// a moving value reads it (ir_licm_loop_pressure's closure), then kept.
#define BUSTER_IR_LICM_MOVES 1
#define BUSTER_IR_LICM_KEPT 2
#define BUSTER_IR_LICM_PENDING 3

typedef struct IrLicmResult IrLicmResult;
struct IrLicmResult
{
    IrFunction function;
    uint32_t moved;
    uint32_t eligible_loops;
    uint32_t skipped_loops;
    // IrLicmOptions.pressure and .vectors: the eligible loops passed over because they hold a call.
    uint32_t called_loops;
    // The instructions the loop walks visited.
    uint64_t visits;
    uint64_t scratch_bytes;
};

typedef struct IrLicmWork IrLicmWork;
struct IrLicmWork
{
    IrOptimizeWork ir;
    Dominators loops;
    uint32_t* destination;
    uint32_t* available;
    uint8_t* safe;
    uint8_t* tainted;
    uint8_t* candidate;
    // IrLicmOptions.pressure: each value's BUSTER_IR_LICM_FREE kind (0 costly), and the loop walk's stamps of the values
    // outside a loop it has counted live through it.
    uint8_t* free_kinds;
    uint32_t* counted;
    // The values sent to a preheader so far.
    uint32_t moves;
    IrLicmResult statistics;
};

static __attribute__((unused)) IrLicmOptions ir_licm_options(void)
{
    const IrLicmOptions options = { 0 };
    return options;
}

// IrLicmOptions.pressure's screen (#2719), before any analysis: whether a verified function may have a loop that holds no
// call, the only loop the policy moves values out of. Every cycle has an edge to a block at or before its source (the
// edge that leaves its last block in the function's order), and a loop without a call has a cycle of blocks that hold
// no call (an IR_CALL or IR_CALL_INDIRECT, as ir_licm_analyze's bitset finds them). So a function where no such edge
// joins two blocks without a call moves nothing under the policy, and Tier 1 passes it over without the preparation's
// CFG, loop analysis and use lists; one where some edge does may still hold a call in each loop, which ir_licm_analyze
// then finds. The operations go 64 at a time, a call's block by a cursor over the blocks' offsets that only goes
// forward; the terminators of the blocks without a call are read one at a time, as ir_build_cfg reads them: there is
// one a block, and a gather of them (64 uops on Zen 5) would cost more than the loads. True where the bitset doesn't
// fit in `scratch`, which it leaves as it was.
static __attribute__((unused)) bool ir_licm_pressure_screen(const IrFunction* f, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint64_t words = ((uint64_t)f->block_count + 63) / 64;
    bool ok = true;
    uint64_t* callers = ir_allocate(scratch, words, sizeof(uint64_t), &ok);
    uint32_t block = 0;
    for (uint64_t first = 0; ok && words != 0 && first < f->instruction_count; first += 64)
    {
        const uint64_t count = f->instruction_count - first < 64 ? f->instruction_count - first : 64;
        const __mmask64 valid = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
        const __m512i operations = _mm512_maskz_loadu_epi8(valid, f->op + first);
        __mmask64 calls = _mm512_mask_cmpeq_epi8_mask(valid, operations, _mm512_set1_epi8(IR_CALL)) | _mm512_mask_cmpeq_epi8_mask(valid, operations, _mm512_set1_epi8(IR_CALL_INDIRECT));
        while (calls != 0)
        {
            const uint64_t call = first + (uint64_t)__builtin_ctzll(calls);
            while (f->block_offsets[block + 1] <= call)
            {
                block += 1;
            }
            callers[block / 64] |= UINT64_C(1) << (block % 64);
            calls &= calls - 1;
        }
    }
    bool found = !ok;
    for (uint64_t word = 0; !found && word < words; word += 1)
    {
        const uint64_t blocks = f->block_count - word * 64;
        const uint64_t valid = blocks >= 64 ? UINT64_MAX : (UINT64_C(1) << blocks) - 1;
        uint64_t sources = ~callers[word] & valid;
        while (!found && sources != 0)
        {
            const uint32_t source = (uint32_t)(word * 64 + (uint64_t)__builtin_ctzll(sources));
            const uint32_t last = f->block_offsets[source + 1] - 1;
            const uint32_t edges = ir_successor_count(f, last);
            for (uint32_t edge = 0; !found && edge < edges; edge += 1)
            {
                const uint32_t target = ir_successor(f, last, edge);
                found = target <= source && (callers[target / 64] >> (target % 64) & 1) == 0;
            }
            sources &= sources - 1;
        }
    }
    arena_pop_to(scratch, mark);
    return found;
}

// Whether a function has an undef, whose users ir_licm_classify taints through the use lists: the operations 64 at a
// time.
static bool ir_licm_has_undef(const IrFunction* f)
{
    bool found = false;
    for (uint64_t first = 0; !found && first < f->instruction_count; first += 64)
    {
        const uint64_t count = f->instruction_count - first < 64 ? f->instruction_count - first : 64;
        const __mmask64 valid = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
        const __m512i operations = _mm512_maskz_loadu_epi8(valid, f->op + first);
        found = _mm512_mask_cmpeq_epi8_mask(valid, operations, _mm512_set1_epi8(IR_UNDEF)) != 0;
    }
    return found;
}

// IrLicmOptions.pressure's free kinds by operation (BUSTER_IR_LICM_FREE and _ARITHMETIC): a truncation and the pointer
// and bit casts change no bits a register holds.
static const uint8_t ir_licm_free[BUSTER_IR_OP_TABLE] = {
    [IR_CONST] = BUSTER_IR_LICM_FREE,
    [IR_SYMBOL_ADDR] = BUSTER_IR_LICM_FREE,
    [IR_STACK_ADDR] = BUSTER_IR_LICM_FREE,
    [IR_INTTOPTR] = BUSTER_IR_LICM_FREE,
    [IR_PTRTOINT] = BUSTER_IR_LICM_FREE,
    [IR_BITCAST] = BUSTER_IR_LICM_FREE,
    [IR_COPY] = BUSTER_IR_LICM_FREE,
    [IR_TRUNC] = BUSTER_IR_LICM_FREE,
    [IR_ADD] = BUSTER_IR_LICM_FREE_ARITHMETIC,
    [IR_SUB] = BUSTER_IR_LICM_FREE_ARITHMETIC,
    [IR_MUL] = BUSTER_IR_LICM_FREE_ARITHMETIC,
    [IR_NEG] = BUSTER_IR_LICM_FREE_ARITHMETIC,
    [IR_ADDRESS] = BUSTER_IR_LICM_FREE_ARITHMETIC,
};

static void ir_licm_classify(IrLicmWork* work)
{
    const IrFunction* f = work->ir.function;
    static const uint8_t safe[BUSTER_IR_OP_TABLE] = {
        [IR_VCONST] = 1,
        [IR_VCMP] = 2,
        [IR_VSELECT] = 1,
        [IR_SHL] = 3,
        [IR_LSHR] = 3,
        [IR_ASHR] = 3,
        [IR_INTRINSIC] = 4,
        [IR_CONST] = 1, [IR_PARAM] = 1, [IR_COPY] = 1,
        [IR_ADD] = 1, [IR_SUB] = 1, [IR_MUL] = 1,
        [IR_AND] = 1, [IR_OR] = 1, [IR_XOR] = 1, [IR_NEG] = 1, [IR_NOT] = 1,
        [IR_FNEG] = 1, [IR_ICMP] = 1, [IR_SELECT] = 1,
        [IR_TRUNC] = 1, [IR_ZEXT] = 1, [IR_SEXT] = 1,
        [IR_INTTOPTR] = 1, [IR_PTRTOINT] = 1, [IR_BITCAST] = 1,
        [IR_STACK_ADDR] = 1, [IR_SYMBOL_ADDR] = 1, [IR_ADDRESS] = 1,
    };
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        work->destination[i] = work->ir.cfg.instruction_blocks[i];
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
            const uint32_t user = work->ir.uses.uses[use].instruction;
            if (work->tainted[user] == 0)
            {
                work->tainted[user] = 1;
                ir_optimize_enqueue(&work->ir, user);
            }
        }
    }
    for (uint64_t first = 0; first < f->instruction_count; first += 16)
    {
        const uint32_t count = f->instruction_count - first < 16 ? f->instruction_count - first : 16;
        const __mmask16 valid = (UINT32_C(1) << count) - 1;
        const __m128i operations = _mm_maskz_loadu_epi8(valid, f->op + first);
        const __m128i kinds = _mm512_castsi512_si128(_mm512_permutex2var_epi8(_mm512_loadu_si512(safe), _mm512_zextsi128_si512(operations), _mm512_loadu_si512(safe + 64)));
        const __m128i tainted = _mm_maskz_loadu_epi8(valid, work->tainted + first);
        const __m128i flags = _mm_maskz_loadu_epi8(valid, f->flags + first);
        const __m128i types = _mm_maskz_loadu_epi8(valid, f->type + first);
        const __mmask16 integer_compare = _mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(2)) & _mm_cmplt_epu8_mask(flags, _mm_set1_epi8(IR_SIMD_LANE_F32));
        // Packed variable shifts are total even for oversized counts. Scalar shifts keep their fault exclusion.
        const __mmask16 vector_shift = _mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(3)) & _mm_cmpge_epu8_mask(types, _mm_set1_epi8(IR_V4I32)) & _mm_cmple_epu8_mask(types, _mm_set1_epi8(IR_V16I32));
        const __m512i discriminators = _mm512_maskz_loadu_epi32(valid, f->c + first);
        // A splat, the width changes, the permutes, the conversions, the scalar shifts, the lane operations and the
        // ternary kinds (#1840: extract to permute2, consecutive kinds, a ternary one's `c` its kind in the low byte and
        // its third operand's place past it), the AES rounds, the reductions, the lanes' unary operations, the floating
        // operations at their own rounding (which read no MXCSR and set none of its flags), the bit shuffles into a mask
        // and the quadword operations are pure and total.
        const __m512i kind_bits = _mm512_and_si512(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_KIND));
        const __mmask16 ternary = _mm512_cmpge_epu32_mask(kind_bits, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_TERNARY)) &
                                  _mm512_cmplt_epu32_mask(kind_bits, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_TERNARY_END));
        const __mmask16 splat = _mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(4)) & (_mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_SPLAT)) |
                                                                          (_mm512_cmpge_epu32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_EXTRACT)) &
                                                                           _mm512_cmple_epu32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_LANE_OP))) | ternary |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_AES)) |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_REDUCE)) |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_LANE_UNARY)) |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_FROUND)) |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_BITSHUFFLE)) |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_QUADWORD_OP)) |
                                                                           _mm512_cmpeq_epi32_mask(discriminators, _mm512_set1_epi32(BUSTER_IR_INTRINSIC_PAIRWISE)));
        const __mmask16 eligible = (_mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(1)) | integer_compare | vector_shift | splat) & _mm_cmpeq_epi8_mask(tainted, _mm_setzero_si128());
        _mm_mask_storeu_epi8(work->safe + first, valid, _mm_maskz_set1_epi8(eligible, 1));
        if (work->free_kinds != NULL)
        {
            const __m128i free_kinds = _mm512_castsi512_si128(_mm512_permutex2var_epi8(_mm512_loadu_si512(ir_licm_free), _mm512_zextsi128_si512(operations), _mm512_loadu_si512(ir_licm_free + 64)));
            _mm_mask_storeu_epi8(work->free_kinds + first, valid, free_kinds);
        }
    }
}

static uint32_t ir_licm_preheader(const IrLicmWork* work, uint32_t loop)
{
    const IrCfg* cfg = &work->ir.cfg;
    const uint32_t header = work->loops.loop_header[loop];
    uint32_t preheader = BUSTER_IR_NONE;
    uint32_t entries = 0;
    for (uint32_t edge = cfg->predecessor_offsets[header]; edge < cfg->predecessor_offsets[(uint64_t)header + 1]; edge += 1)
    {
        const uint32_t predecessor = cfg->predecessors[edge];
        if (work->loops.rpo_index[predecessor] != BUSTER_DOMINATOR_NONE && !dominators_loop_contains(&work->loops, loop, predecessor))
        {
            entries += 1;
            preheader = predecessor;
        }
    }
    if (work->loops.loop_irreducible[loop] != 0 || entries != 1 || cfg->successor_offsets[(uint64_t)preheader + 1] - cfg->successor_offsets[preheader] != 1)
    {
        preheader = BUSTER_IR_NONE;
    }
    return preheader;
}

// The walk of one loop (#2781, #2979), over the loop's own blocks in place of the whole function: the motion of a
// function of many loops then costs each loop's size, not the function's for each. In reverse postorder a value comes
// after its operands (a phi's aside, and a phi doesn't move), so one pass forward finds the movable values: the safe ones
// of the loop whose operands are each a movable value of the loop or a value whose block dominates the preheader.
// `candidate` is the walk's state (1 movable), current for the values `available` stamps with this walk's `epoch`: a
// value the walk hasn't stamped doesn't move, whatever another loop's walk left there. Returns the reverse-postorder
// index past the loop's last block, and in *found the movable values of vector or mask type. With `budget`
// (IrLicmOptions.pressure), a truth value doesn't move, a free value is BUSTER_IR_LICM_PENDING, and a costly
// general-register value moves while *budget lasts, each taking one: in reverse postorder, so the header's come first.
static uint32_t ir_licm_loop_walk(IrLicmWork* work, uint32_t loop, uint32_t preheader, uint32_t epoch, uint32_t* found, uint32_t* budget)
{
    const IrFunction* f = work->ir.function;
    const Dominators* loops = &work->loops;
    const uint32_t* rpo = work->ir.dominators.rpo;
    // The header is the first of the loop's blocks in reverse postorder; the walk ends at the last one.
    const uint32_t first = work->ir.dominators.rpo_index[loops->loop_header[loop]];
    uint32_t end = first;
    *found = 0;
    for (uint32_t r = first, left = loops->loop_size[loop]; left != 0 && r < work->ir.dominators.reachable_count; r += 1)
    {
        const uint32_t block = rpo[r];
        const bool member = dominators_loop_contains(loops, loop, block);
        left -= member;
        end = member ? r + 1 : end;
        for (uint32_t i = f->block_offsets[block]; member && i < f->block_offsets[(uint64_t)block + 1]; i += 1)
        {
            bool movable = work->safe[i] != 0 && dominators_loop_contains(loops, loop, work->destination[i]);
            const uint32_t mask = movable ? ir_value_mask3(f, i) : 0;
            const uint32_t operands[3] = { f->a[i], f->b[i], (mask & 4) != 0 ? ir_value_third(f, i) : BUSTER_IR_NONE };
            // Whether every operand is a free value of the loop (IrLicmOptions.pressure's free arithmetic).
            bool free_operands = mask != 0;
            for (uint32_t k = 0; k < 3; k += 1)
            {
                if (((mask >> k) & 1) != 0)
                {
                    const uint32_t operand = operands[k];
                    const bool inside = dominators_loop_contains(loops, loop, work->destination[operand]);
                    movable = movable && (inside ? work->available[operand] == epoch && work->candidate[operand] != 0 : dominators_dominates(loops, work->destination[operand], preheader));
                    free_operands = free_operands && inside && work->candidate[operand] == BUSTER_IR_LICM_PENDING;
                }
            }
            uint8_t state = movable;
            if (budget != NULL)
            {
                const bool costless = work->free_kinds[i] == BUSTER_IR_LICM_FREE || (work->free_kinds[i] == BUSTER_IR_LICM_FREE_ARITHMETIC && free_operands);
                const bool general = (f->type[i] >= IR_I8 && f->type[i] <= IR_I64) || f->type[i] == IR_PTR;
                const bool charged = movable && !costless && general;
                movable = movable && f->type[i] != IR_I1 && (!charged || *budget != 0);
                *budget -= charged && movable;
                state = !movable ? 0 : costless ? BUSTER_IR_LICM_PENDING : BUSTER_IR_LICM_MOVES;
            }
            work->available[i] = epoch;
            work->candidate[i] = state;
            *found += movable && f->type[i] >= IR_V4I32;
        }
        work->statistics.visits += member ? f->block_offsets[(uint64_t)block + 1] - f->block_offsets[block] : 0;
    }
    return end;
}

// IrLicmOptions.vectors' motion of one loop (#2781): the walk (ir_licm_loop_walk), then one pass backward that keeps the
// movable values of vector or mask type and, before it reaches them, the movable values their operands are, and sends
// each kept value to the preheader (`candidate` 2 for kept).
static void ir_licm_loop_vectors(IrLicmWork* work, uint32_t loop, uint32_t preheader, uint32_t epoch, uint64_t* holders)
{
    const IrFunction* f = work->ir.function;
    const Dominators* loops = &work->loops;
    const uint32_t* rpo = work->ir.dominators.rpo;
    const uint32_t first = work->ir.dominators.rpo_index[loops->loop_header[loop]];
    uint32_t found = 0;
    const uint32_t end = ir_licm_loop_walk(work, loop, preheader, epoch, &found, NULL);
    for (uint32_t r = end; found != 0 && r != first; r -= 1)
    {
        const uint32_t block = rpo[r - 1];
        const bool member = dominators_loop_contains(loops, loop, block);
        for (uint32_t i = f->block_offsets[(uint64_t)block + 1]; member && i != f->block_offsets[block]; i -= 1)
        {
            const uint32_t value = i - 1;
            const bool vector = f->type[value] >= IR_V4I32;
            if (work->candidate[value] == 2 || (work->candidate[value] == 1 && vector))
            {
                const uint32_t mask = ir_value_mask3(f, value);
                const uint32_t operands[3] = { f->a[value], f->b[value], (mask & 4) != 0 ? ir_value_third(f, value) : BUSTER_IR_NONE };
                for (uint32_t k = 0; k < 3; k += 1)
                {
                    if (((mask >> k) & 1) != 0 && work->available[operands[k]] == epoch && work->candidate[operands[k]] == 1)
                    {
                        work->candidate[operands[k]] = 2;
                    }
                }
                work->destination[value] = preheader;
                work->moves += 1;
                if (vector)
                {
                    holders[preheader / 64] |= UINT64_C(1) << (preheader % 64);
                }
            }
        }
    }
}

// The unrestricted motion of one loop (#2979): the walk (ir_licm_loop_walk) sends every movable value to the preheader.
static void ir_licm_loop_all(IrLicmWork* work, uint32_t loop, uint32_t preheader, uint32_t epoch)
{
    const IrFunction* f = work->ir.function;
    const Dominators* loops = &work->loops;
    const uint32_t* rpo = work->ir.dominators.rpo;
    const uint32_t first = work->ir.dominators.rpo_index[loops->loop_header[loop]];
    uint32_t found = 0;
    const uint32_t end = ir_licm_loop_walk(work, loop, preheader, epoch, &found, NULL);
    for (uint32_t r = first; r < end; r += 1)
    {
        const uint32_t block = rpo[r];
        const bool member = dominators_loop_contains(loops, loop, block);
        for (uint32_t i = f->block_offsets[block]; member && i < f->block_offsets[(uint64_t)block + 1]; i += 1)
        {
            if (work->available[i] == epoch && work->candidate[i] != 0)
            {
                work->destination[i] = preheader;
                work->moves += 1;
            }
        }
    }
}

// IrLicmOptions.pressure's motion of one loop that holds no call (#2719). First the general-register values live through
// it, each counted once: its header's phis and the costly values from outside that its instructions read (a phi's
// incoming values aside, which live only to the header; the extra values of the few operations with more than three
// operands aren't counted). What is left of BUSTER_IR_LICM_REGISTERS is the walk's budget (ir_licm_loop_walk). Then one
// pass backward keeps each free value that a moving or kept value reads, before it reaches it (a value comes after its
// operands), and every moving or kept value goes to the preheader; the other free values stay at their users.
static void ir_licm_loop_pressure(IrLicmWork* work, uint32_t loop, uint32_t preheader, uint32_t epoch)
{
    const IrFunction* f = work->ir.function;
    const Dominators* loops = &work->loops;
    const uint32_t* rpo = work->ir.dominators.rpo;
    const uint32_t header = loops->loop_header[loop];
    const uint32_t first = work->ir.dominators.rpo_index[header];
    uint32_t through = 0;
    for (uint32_t r = first, left = loops->loop_size[loop]; left != 0 && r < work->ir.dominators.reachable_count; r += 1)
    {
        const uint32_t block = rpo[r];
        const bool member = dominators_loop_contains(loops, loop, block);
        left -= member;
        for (uint32_t i = f->block_offsets[block]; member && i < f->block_offsets[(uint64_t)block + 1]; i += 1)
        {
            const bool phi = f->op[i] == IR_PHI;
            through += phi && block == header && ((f->type[i] >= IR_I8 && f->type[i] <= IR_I64) || f->type[i] == IR_PTR);
            const uint32_t mask = phi ? 0 : ir_value_mask3(f, i);
            const uint32_t operands[3] = { f->a[i], f->b[i], (mask & 4) != 0 ? ir_value_third(f, i) : BUSTER_IR_NONE };
            for (uint32_t k = 0; k < 3; k += 1)
            {
                const uint32_t operand = operands[k];
                const bool read = ((mask >> k) & 1) != 0;
                const bool counts = read && work->free_kinds[operand] != BUSTER_IR_LICM_FREE && work->counted[operand] != epoch &&
                                    ((f->type[operand] >= IR_I8 && f->type[operand] <= IR_I64) || f->type[operand] == IR_PTR) &&
                                    !dominators_loop_contains(loops, loop, work->destination[operand]);
                if (counts)
                {
                    work->counted[operand] = epoch;
                }
                through += counts;
            }
        }
    }
    uint32_t budget = through < BUSTER_IR_LICM_REGISTERS ? BUSTER_IR_LICM_REGISTERS - through : 0;
    uint32_t found = 0;
    const uint32_t end = ir_licm_loop_walk(work, loop, preheader, epoch, &found, &budget);
    for (uint32_t r = end; r != first; r -= 1)
    {
        const uint32_t block = rpo[r - 1];
        const bool member = dominators_loop_contains(loops, loop, block);
        for (uint32_t i = f->block_offsets[(uint64_t)block + 1]; member && i != f->block_offsets[block]; i -= 1)
        {
            const uint32_t value = i - 1;
            const uint8_t state = work->available[value] == epoch ? work->candidate[value] : 0;
            if (state == BUSTER_IR_LICM_MOVES || state == BUSTER_IR_LICM_KEPT)
            {
                const uint32_t mask = ir_value_mask3(f, value);
                const uint32_t operands[3] = { f->a[value], f->b[value], (mask & 4) != 0 ? ir_value_third(f, value) : BUSTER_IR_NONE };
                for (uint32_t k = 0; k < 3; k += 1)
                {
                    if (((mask >> k) & 1) != 0 && work->available[operands[k]] == epoch && work->candidate[operands[k]] == BUSTER_IR_LICM_PENDING)
                    {
                        work->candidate[operands[k]] = BUSTER_IR_LICM_KEPT;
                    }
                }
                work->destination[value] = preheader;
                work->moves += 1;
            }
        }
    }
}

static IrError ir_licm_analyze(IrLicmWork* work, Arena* scratch, IrLicmOptions options)
{
    bool ok = true;
    uint32_t* order = ir_allocate(scratch, work->loops.loop_count, sizeof(uint32_t), &ok);
    // IrLicmOptions.vectors: the blocks that hold a safe value of vector or mask type, where it is or where an inner
    // loop's motion put it. A loop whose body has none of them moves nothing, and its instructions are not walked.
    const uint64_t words = ((uint64_t)work->ir.function->block_count + 63) / 64;
    uint64_t* holders = options.vectors ? ir_allocate(scratch, words, sizeof(uint64_t), &ok) : 0;
    for (uint32_t i = 0; ok && options.vectors && i < work->ir.function->instruction_count; i += 1)
    {
        if (work->safe[i] != 0 && work->ir.function->type[i] >= IR_V4I32)
        {
            holders[work->destination[i] / 64] |= UINT64_C(1) << (work->destination[i] % 64);
        }
    }
    // IrLicmOptions.pressure and .vectors: the blocks that hold a call, 64 operations at a time. A loop whose body has
    // one moves nothing, and its instructions are not walked.
    const bool pressure = options.pressure && !options.vectors;
    const bool calls = pressure || options.vectors;
    uint64_t* callers = calls ? ir_allocate(scratch, words, sizeof(uint64_t), &ok) : 0;
    for (uint64_t first = 0; ok && calls && first < work->ir.function->instruction_count; first += 64)
    {
        const uint64_t count = work->ir.function->instruction_count - first < 64 ? work->ir.function->instruction_count - first : 64;
        const __mmask64 valid = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
        const __m512i operations = _mm512_maskz_loadu_epi8(valid, work->ir.function->op + first);
        __mmask64 calls = _mm512_mask_cmpeq_epi8_mask(valid, operations, _mm512_set1_epi8(IR_CALL)) | _mm512_mask_cmpeq_epi8_mask(valid, operations, _mm512_set1_epi8(IR_CALL_INDIRECT));
        while (calls != 0)
        {
            const uint32_t block = work->destination[first + (uint64_t)__builtin_ctzll(calls)];
            callers[block / 64] |= UINT64_C(1) << (block % 64);
            calls &= calls - 1;
        }
    }
    for (uint32_t loop = 0; ok && loop < work->loops.loop_count; loop += 1)
    {
        uint32_t at = loop;
        while (at != 0 && work->loops.loop_size[order[at - 1]] > work->loops.loop_size[loop])
        {
            order[at] = order[at - 1];
            at -= 1;
        }
        order[at] = loop;
    }
    for (uint32_t at = 0; ok && at < work->loops.loop_count; at += 1)
    {
        const uint32_t loop = order[at];
        const uint32_t preheader = ir_licm_preheader(work, loop);
        if (preheader == BUSTER_IR_NONE)
        {
            work->statistics.skipped_loops += 1;
        }
        else
        {
            work->statistics.eligible_loops += 1;
            bool held = !options.vectors;
            const uint64_t* body = work->loops.loop_bodies + (uint64_t)loop * work->loops.row_words;
            for (uint64_t word = 0; !held && word < words; word += 1)
            {
                held = (body[word] & holders[word]) != 0;
            }
            bool called = false;
            for (uint64_t word = 0; calls && word < words; word += 1)
            {
                called = called || (body[word] & callers[word]) != 0;
            }
            work->statistics.called_loops += called;
            held = held && !(options.vectors && called);
            if (pressure)
            {
                if (!called)
                {
                    ir_licm_loop_pressure(work, loop, preheader, at + 1);
                }
            }
            else if (held && options.vectors)
            {
                ir_licm_loop_vectors(work, loop, preheader, at + 1, holders);
            }
            else if (held)
            {
                ir_licm_loop_all(work, loop, preheader, at + 1);
            }
        }
    }
    const IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    return error;
}

// A private view keeps immutable metadata borrowed from the input while rebuilding every value-bearing slice.
// Shared compaction subsequently materializes every final column in the caller's output arena.
static IrError ir_licm_reorder(IrLicmWork* work, Arena* scratch, IrFunction* result)
{
    const IrFunction* f = work->ir.function;
    bool ok = true;
    uint32_t* offsets = ir_allocate(scratch, (uint64_t)f->block_count + 1, sizeof(uint32_t), &ok);
    uint32_t* cursors = ir_allocate(scratch, f->block_count, sizeof(uint32_t), &ok);
    uint32_t* movers = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
    uint32_t* order = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
    uint32_t* values = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
    IrFunction view = *f;
    view.op = ir_allocate(scratch, f->instruction_count, 1, &ok);
    view.type = ir_allocate(scratch, f->instruction_count, 1, &ok);
    view.flags = ir_allocate(scratch, f->instruction_count, 1, &ok);
    view.alignment_log2 = ir_allocate(scratch, f->instruction_count, 1, &ok);
    view.a = ir_allocate(scratch, f->instruction_count, 4, &ok);
    view.b = ir_allocate(scratch, f->instruction_count, 4, &ok);
    view.c = ir_allocate(scratch, f->instruction_count, 4, &ok);
    view.block_offsets = ir_allocate(scratch, (uint64_t)f->block_count + 1, 4, &ok);
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    uint64_t extras = 0;
    uint64_t calls = 0;
    for (uint32_t i = 0; error == IR_OK && i < f->instruction_count; i += 1)
    {
        if (work->destination[i] != work->ir.cfg.instruction_blocks[i])
        {
            offsets[(uint64_t)work->destination[i] + 1] += 1;
            work->statistics.moved += 1;
        }
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
        else if (ir_extra_value_count(f, i) != 0)
        {
            extras += ir_extra_value_count(f, i);
        }
    }
    if (error == IR_OK && (extras > UINT32_MAX || calls > UINT32_MAX))
    {
        error = IR_ERROR_LIMIT;
    }
    if (error == IR_OK)
    {
        view.extra = ir_allocate(scratch, extras, 4, &ok);
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
        for (uint32_t b = 0; b < f->block_count; b += 1)
        {
            offsets[(uint64_t)b + 1] += offsets[b];
            cursors[b] = offsets[b];
        }
        // Original RPO and in-block instruction order topologically order all non-phi SSA dependencies.
        for (uint32_t r = 0; r < work->ir.dominators.reachable_count; r += 1)
        {
            const uint32_t block = work->ir.dominators.rpo[r];
            for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
            {
                if (work->destination[i] != block)
                {
                    movers[cursors[work->destination[i]]++] = i;
                }
            }
        }
        uint32_t count = 0;
        for (uint32_t b = 0; b < f->block_count; b += 1)
        {
            view.block_offsets[b] = count;
            const uint32_t end = f->block_offsets[(uint64_t)b + 1];
            for (uint32_t i = f->block_offsets[b]; i + 1 < end; i += 1)
            {
                if (work->destination[i] == b)
                {
                    order[count++] = i;
                }
            }
            for (uint32_t at = offsets[b]; at < offsets[(uint64_t)b + 1]; at += 1)
            {
                order[count++] = movers[at];
            }
            order[count++] = end - 1;
        }
        view.block_offsets[f->block_count] = count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            values[order[i]] = i;
        }
        ir_debug_remap(f, &view, values, NULL, NULL);
        uint32_t extra = 0;
        uint32_t call = 0;
        for (uint32_t out = 0; out < count; out += 1)
        {
            const uint32_t i = order[out];
            if (view.debug != NULL && work->destination[i] != work->ir.cfg.instruction_blocks[i])
            {
                // The hoisted computation may execute before its original lexical block is entered.
                view.debug->origins[out] = BUSTER_IR_NONE;
            }
            const uint32_t mask = ir_value_mask(f, i);
            view.op[out] = f->op[i];
            view.type[out] = f->type[i];
            view.flags[out] = f->flags[i];
            view.alignment_log2[out] = f->alignment_log2[i];
            view.a[out] = (mask & 1) != 0 ? values[f->a[i]] : f->a[i];
            view.b[out] = (mask & 2) != 0 ? values[f->b[i]] : f->b[i];
            view.c[out] = (mask & 4) != 0 ? values[f->c[i]] : f->c[i];
            if (f->op[i] == IR_PHI)
            {
                view.a[out] = extra;
                for (uint32_t incoming = 0; incoming < f->b[i]; incoming += 1)
                {
                    view.extra[extra++] = f->extra[f->a[i] + incoming * 2];
                    view.extra[extra++] = values[f->extra[f->a[i] + incoming * 2 + 1]];
                }
            }
            else if (f->op[i] == IR_SWITCH)
            {
                view.b[out] = extra;
                const uint64_t words = (uint64_t)f->c[i] * 2 + 1;
                memcpy(view.extra + extra, f->extra + f->b[i], words * 4);
                extra += words;
            }
            else if (f->op[i] == IR_BR_INDIRECT && f->b[i] != BUSTER_IR_NONE)
            {
                view.b[out] = extra;
                memcpy(view.extra + extra, f->extra + f->b[i], (uint64_t)f->c[i] * 4);
                extra += f->c[i];
            }
            else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
            {
                view.b[out] = call;
                view.calls[call] = f->calls[f->b[i]];
                view.calls[call].argument_first = extra;
                for (uint32_t argument = 0; argument < view.calls[call].argument_count; argument += 1)
                {
                    view.extra[extra++] = values[f->extra[f->calls[f->b[i]].argument_first + argument]];
                }
                call += 1;
            }
            else if (ir_extra_value_count(f, i) != 0)
            {
                // A ternary intrinsic's third operand, or a scatter's stored vector and mask (#1840), at its new place in the extra array.
                view.c[out] = ir_intrinsic_ternary_c(f->c[i], extra);
                for (uint32_t k = 0, extra_values = ir_extra_value_count(f, i); k < extra_values; k += 1)
                {
                    view.extra[extra++] = values[ir_extra_value(f, i, k)];
                }
            }
        }
        *result = view;
    }
    return error;
}

static __attribute__((unused)) IrCheck ir_optimize_licm(const IrFunction* function, Arena* output, Arena* scratch, IrLicmOptions options, IrLicmResult* result)
{
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    IrLicmWork work = { 0 };
    IrCheck check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, 0);
    // The natural loops come with the preparation's dominator tree, from one analysis (#2719). A function past the loop
    // analysis' limits is refused (IR_ERROR_UNSUPPORTED), and goes on as it was.
    work.ir.dominator_outputs = BUSTER_DOMINATOR_LOOPS;
    work.ir.validated = options.validated;
    // IrLicmOptions.pressure reads the use lists only to taint what an undef reaches (ir_licm_classify): a verified
    // function without one builds none.
    work.ir.without_uses = options.validated && options.pressure && !options.vectors && !ir_licm_has_undef(function);
    if (output != scratch)
    {
        check = ir_optimize_prepare(&work.ir, function, output, scratch, false, true);
    }
    if (check.error == IR_OK)
    {
        work.loops = work.ir.dominators;
    }
    // IrLicmOptions.vectors and .pressure: a function without loops moves nothing, and isn't classified.
    const bool restricted = options.vectors || options.pressure;
    const bool looped = check.error == IR_OK && (!restricted || work.loops.loop_count != 0);
    if (looped)
    {
        bool ok = true;
        const uint64_t n = function->instruction_count;
        work.destination = ir_allocate(scratch, n, 4, &ok);
        work.available = ir_allocate(scratch, n, 4, &ok);
        work.safe = ir_allocate(scratch, n, 1, &ok);
        work.tainted = ir_allocate(scratch, n, 1, &ok);
        work.candidate = ir_allocate(scratch, n, 1, &ok);
        work.free_kinds = options.pressure && !options.vectors ? ir_allocate(scratch, n, 1, &ok) : NULL;
        work.counted = options.pressure && !options.vectors ? ir_allocate(scratch, n, 4, &ok) : NULL;
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (looped && check.error == IR_OK)
    {
        ir_licm_classify(&work);
        check.error = ir_licm_analyze(&work, scratch, options);
    }
    // IrLicmOptions.vectors and .pressure: a function where nothing moves stays the caller's, unwritten.
    const bool rewritten = check.error == IR_OK && (!restricted || work.moves != 0);
    IrFunction view = { 0 };
    if (rewritten)
    {
        check.error = ir_licm_reorder(&work, scratch, &view);
    }
    if (rewritten && check.error == IR_OK)
    {
        work.ir.function = &view;
        check.error = ir_build_cfg(&view, scratch, output, &work.ir.cfg);
    }
    if (rewritten && check.error == IR_OK)
    {
        for (uint32_t b = 0; b < view.block_count; b += 1)
        {
            work.ir.blocks[b] = work.ir.dominators.rpo_index[b] != BUSTER_DOMINATOR_NONE;
            for (uint32_t edge = work.ir.cfg.successor_offsets[b]; edge < work.ir.cfg.successor_offsets[(uint64_t)b + 1]; edge += 1)
            {
                work.ir.edges[edge] = work.ir.blocks[b];
            }
        }
        for (uint32_t i = 0; i < view.instruction_count; i += 1)
        {
            work.ir.states[i] = IROPT_OVERDEFINED;
            work.ir.live[i] = work.ir.blocks[work.ir.cfg.instruction_blocks[i]];
        }
        IrOptimizeOptions rewrite = ir_optimize_options();
        rewrite.passes = 0;
        check.error = ir_optimize_rewrite(&work.ir, output, scratch, rewrite, &work.statistics.function);
    }
    if (rewritten && check.error == IR_OK)
    {
        check = ir_optimize_verify_result(&work.statistics.function, output, scratch);
    }
    if (check.error == IR_OK)
    {
        work.statistics.function = rewritten ? work.statistics.function : *function;
        work.statistics.scratch_bytes = scratch->length - sm;
        *result = work.statistics;
    }
    else
    {
        arena_pop_to(output, om);
    }
    arena_pop_to(scratch, sm);
    return check;
}
