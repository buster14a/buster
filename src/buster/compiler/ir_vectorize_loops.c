// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Loop vectorization of countable innermost loops (#2743, docs/tier1-loop-vectorization.md). Include after ir.c and
// ir_optimize.c. ir_vectorize_loops(function, options, output, scratch, result) returns an IrCheck and,
// where it vectorized a loop, a fresh structurally verified function in result->function; elsewhere result->changed is
// false and result->function is the input itself.
//
// A loop is taken where it has this shape after promotion, truth simplification and if-conversion: a header whose
// only phis are an induction (i32 or i64, from an invariant start, `+ 1` on the back edge) and integer reductions, whose
// only other needed row is the exit test `iv < n` (unsigned, or signed) on which it branches into the loop or out; then
// a chain of blocks optionally containing min/max reduction diamonds, back to the header; and one preheader, whose only successor
// is the header. Every needed row of the chain is classified in order (IrVectorizeClass): uniform (no lane depends on
// the induction), an index or a pointer that is affine in the induction, or a vector value of one lane per iteration.
// A row nothing needs is ignored. Stores must be consecutive: their pointer advances by its access's bytes; a load may
// also advance by 2 to 8 times them (#2808: k loads and k - 1 permutes de-interleave it). A count of comparisons,
// `c += (x == y)`, is a reduction whose accumulator is a scalar, the population count of the comparison's mask added each
// iteration (#2808); a 32-bit index of a 32-bit induction is affine where G finds that it doesn't wrap. Anything else
// (calls, divisions, fences, volatile accesses, floating reductions, a data use of the induction, a strided store, a
// gathered access) refuses the loop, counted by reason (IrVectorizeRefusal).
//
// The rewrite leaves every original row where it is and appends three blocks per loop:
// - G, the guard, reached from the preheader instead of the header: the speculatable uniform rows' clones, the trip
//   count, `vector_n` (the trip count rounded down to the lanes), the splats, the reductions' identities, and, for each
//   store and each other access of another address, an exact test of their byte ranges over the vector iterations
//   (modular, so no end wraps). It enters the vector loop where vector_n is nonzero and no range overlaps, and the
//   scalar loop otherwise.
// - VB, the vector loop: lane 0's induction, the accumulators, the index and pointer chains for lane 0, and each vector
//   row in the original order, so a store and the loads of its own address keep their order element by element.
// - VX, the exit: each accumulator reduced (`reduce`, `lane_extract`) and combined with its initial value; then the
//   scalar loop, whose header phis take the start and the initial values from G or the vector loop's results from VX:
//   the original loop is both the remainder and the fallback.
// Where the target has masked moves and the loop computes no floating value (#2807, ir_vectorize_masked_legal), the
// remainder is one masked vector iteration instead: G's overlap tests cover the whole trip count, and G enters P where it
// is nonzero; P enters VB where a whole vector is, else T; and T, after VB, runs the remaining lanes' iteration under a
// mask of them (masked loads and stores, each reduction's update selected by it), reduces as VX does and enters the
// original header at the trip's end, whose exit test fails at once. A loop shorter than a vector runs in T alone.
// Vector widths come from IrVectorizeTarget, the operations the target selects at each width (Tier 1's tables give
// it: ir_vectorize_target.c): the widest width at which every row is legal and no value is narrower than 16 bytes.
//
// Data-parallel parts: every row's admission kind is an opcode table's lookup 64 rows at a time (vpermi2b), with the
// volatile and atomic flags folded in, and each loop's rows are scanned for a refusing kind 64 at a time. The
// classification of one loop's needed rows, their order and the emission are the small ordered part. Rows are counted
// first and then written by the same replay (IrVectorizeBuild.counting).

#ifndef BUSTER_IR_VECTORIZE_LOOPS_C
#define BUSTER_IR_VECTORIZE_LOOPS_C

#define BUSTER_IR_VECTORIZE_BATCH 64u
// Store/access pairs a loop may test at run time; past them it stays scalar.
#define BUSTER_IR_VECTORIZE_GUARD_LIMIT 8u
// The depth of the structural comparison of two pointers' chains.
#define BUSTER_IR_VECTORIZE_DEPTH 12u
// Reductions per loop: G, VB and VX keep one value each per reduction in a fixed array.
#define BUSTER_IR_VECTORIZE_REDUCTION_LIMIT 32u
// Widths: 16 << k bytes for k below this.
#define BUSTER_IR_VECTORIZE_WIDTHS 3u
// Above this many vector iterations' elements, a loop with guards stays scalar, so that no range's length wraps.
#define BUSTER_IR_VECTORIZE_RANGE_LIMIT (UINT64_C(1) << 56)
// A strided load's elements per iteration at most (#2808): its de-interleave is that many loads and one fewer permutes.
#define BUSTER_IR_VECTORIZE_STRIDE_LIMIT 8u
// A 32-bit index's coefficient's magnitude below this (#2808): the guard's product of it and a 32-bit trip count fits
// in 64 bits.
#define BUSTER_IR_VECTORIZE_NARROW_COEFFICIENT (INT64_C(1) << 30)
// A reduction's operation past the IR's (BUSTER_IR_REDUCE_*): a count of a mask's set lanes, `c += (x == y)` (#2808),
// accumulated in a scalar by the population count of the mask's bits.
#define BUSTER_IR_VECTORIZE_COUNT 15u

// A row's admission kind, by its opcode (ir_vectorize_kind_table); 0 refuses its loop.
#define BUSTER_IR_VECTORIZE_KIND_FORBID 0u
#define BUSTER_IR_VECTORIZE_KIND_PURE 1u
#define BUSTER_IR_VECTORIZE_KIND_FLOAT 2u
#define BUSTER_IR_VECTORIZE_KIND_LOAD 3u
#define BUSTER_IR_VECTORIZE_KIND_STORE 4u
#define BUSTER_IR_VECTORIZE_KIND_PHI 5u
#define BUSTER_IR_VECTORIZE_KIND_BRANCH 6u
#define BUSTER_IR_VECTORIZE_KIND_INTRINSIC 7u
// A pure row whose speculation can't fault (LICM's set): it may be computed in G.
#define BUSTER_IR_VECTORIZE_KIND_SAFE 8u

typedef enum IrVectorizeRefusal
{
    // Not innermost, irreducible, no dedicated preheader, or a body that isn't a chain of blocks.
    IR_VECTORIZE_REFUSE_SHAPE,
    // No exit test of the induction against an invariant bound, or an induction that isn't `+ 1`.
    IR_VECTORIZE_REFUSE_INDUCTION,
    // A header phi that is neither the induction nor an integer reduction.
    IR_VECTORIZE_REFUSE_PHI,
    // A row outside the admitted operations (a call, a division, a fence, a volatile access, ...).
    IR_VECTORIZE_REFUSE_OPERATION,
    // An access whose pointer isn't consecutive, or that the analysis can't classify.
    IR_VECTORIZE_REFUSE_ADDRESS,
    // A value used where its class can't go: the induction as data, a reduction's partial value.
    IR_VECTORIZE_REFUSE_USE,
    // A dependence the run-time test can't cover: more than BUSTER_IR_VECTORIZE_GUARD_LIMIT pairs, an access of one
    // address at two sizes, a uniform load in a loop that stores.
    IR_VECTORIZE_REFUSE_DEPENDENCE,
    // No width at which the target has every row's operation.
    IR_VECTORIZE_REFUSE_WIDTH,
    IR_VECTORIZE_REFUSALS,
} IrVectorizeRefusal;

// Which vector operations the target selects, a bit k for each width 16 << k bytes (Tier 1's: ir_vectorize_target.c).
typedef struct IrVectorizeTarget IrVectorizeTarget;
struct IrVectorizeTarget
{
    // Loads, stores, phis and copies of a vector.
    uint8_t moves;
    // An element-wise operation (IR_ADD to IR_ASHR, IR_FADD to IR_FDIV) at its lanes, by opcode.
    uint8_t binary[BUSTER_IR_OP_TABLE][IR_SIMD_LANE_COUNT];
    // vcmp of the lanes (both signednesses), at the operands' width.
    uint8_t compare[IR_SIMD_LANE_COUNT];
    // vselect at the lanes.
    uint8_t select[IR_SIMD_LANE_COUNT];
    // A splat of a scalar of the lanes' type.
    uint8_t splat[IR_SIMD_LANE_COUNT];
    // An extension from lanes of 1 << from bytes to 1 << to, zero (0) or sign (1), at the result's width.
    uint8_t extend[4][4][2];
    // A wrapping truncation from 1 << from bytes to 1 << to, at the source's width.
    uint8_t truncate[4][4];
    // A horizontal reduction and lane_extract of lane 0. For min/max this also covers lane_op at the full width.
    uint8_t reduce[4][BUSTER_IR_REDUCE_OPERATIONS];
    // A population count of lanes of 1 << size bytes.
    uint8_t popcount[4];
    // A masked load and a masked store of lanes of 1 << size bytes at the width: the masked tail's (#2807), EVEX's alone.
    uint8_t masked[4];
    // The masked tail's mask of 1 << k lanes from a general register, a bit k for 2 to 64 lanes (#2807): a kmov of the mask
    // type's bytes, which a loop with no vector load or store needs too (kmovb, of 8 lanes or fewer, is AVX512DQ's).
    uint8_t mask;
    // A permute of two tables of lanes of 1 << size bytes at the width (vpermt2b to vpermt2q): a strided load's
    // de-interleave (#2808), EVEX's alone.
    uint8_t permute2[4];
    // A count of a mask of 1 << k lanes' set bits in a general register, a bit k for 2 to 64 lanes (#2808): the kmov of the
    // mask type's bytes to one, and popcnt.
    uint8_t count;
};

// test-ir-vectorize's mutants (IrVectorizeOptions.mutation): each breaks one rule of the rewrite, and the tests' oracle
// must reject every one. A compilation's options have none.
#define BUSTER_IR_VECTORIZE_MUTATE_NONE 0u
// No run-time overlap test.
#define BUSTER_IR_VECTORIZE_MUTATE_GUARDS 1u
// vector_n not rounded down to the lanes: the vector loop runs past the trip count.
#define BUSTER_IR_VECTORIZE_MUTATE_TAIL 2u
// A reduction's accumulators start at its identity plus one.
#define BUSTER_IR_VECTORIZE_MUTATE_IDENTITY 3u
// An integer vector operation reads lanes of twice its elements' width (half their width for quadwords).
#define BUSTER_IR_VECTORIZE_MUTATE_LANES 4u
// The masked tail's mask has one lane more than the trip count's remainder (#2807).
#define BUSTER_IR_VECTORIZE_MUTATE_MASK 5u
// No test of a 32-bit index's wrap (#2808).
#define BUSTER_IR_VECTORIZE_MUTATE_NARROW 6u
// A strided load's permutes take each lane's next element (#2808).
#define BUSTER_IR_VECTORIZE_MUTATE_STRIDE 7u
#define BUSTER_IR_VECTORIZE_MUTATIONS 8u

typedef struct IrVectorizeOptions IrVectorizeOptions;
struct IrVectorizeOptions
{
    const IrVectorizeTarget* target;
    uint32_t mutation;
};

typedef struct IrVectorizeResult IrVectorizeResult;
struct IrVectorizeResult
{
    IrFunction function;
    bool changed;
    // Loops examined (each back edge's header in a depth-first walk once, outer loops included: they refuse as their
    // shape), those vectorized, by their widest vector (16, 32 and 64 bytes), their reductions and their run-time
    // overlap tests, and the refused ones by reason.
    uint32_t loops;
    uint32_t vectorized;
    uint32_t widths[BUSTER_IR_VECTORIZE_WIDTHS];
    uint32_t reductions;
    uint32_t guards;
    // Those whose tail is one masked vector iteration (#2807).
    uint32_t masked;
    uint32_t refused[IR_VECTORIZE_REFUSALS];
};

// A needed loop row's class (IrVectorizeWork.classes).
typedef enum IrVectorizeClass
{
    IR_VECTORIZE_CLASS_NONE,
    // Not needed by any root of the loop: neither cloned nor checked further.
    IR_VECTORIZE_CLASS_DEAD,
    // The same value in every lane: cloned once, in G where speculatable (IrVectorizeWork.placed_g), else in VB.
    IR_VECTORIZE_CLASS_UNIFORM,
    // The induction, or an integer affine in it (coefficients[i] times it, plus a uniform): cloned for lane 0.
    IR_VECTORIZE_CLASS_INDEX,
    // A pointer, or its integer, that advances by coefficients[i] bytes per iteration: cloned for lane 0.
    IR_VECTORIZE_CLASS_POINTER,
    // One lane per iteration, of 1 << sizes[i] bytes: a vector row.
    IR_VECTORIZE_CLASS_VECTOR,
    // A comparison's lanes, a mask.
    IR_VECTORIZE_CLASS_MASK,
    // A reduction's phi, and its update.
    IR_VECTORIZE_CLASS_REDUCTION,
    IR_VECTORIZE_CLASS_UPDATE,
    // The exit test, the induction's increment and the terminators.
    IR_VECTORIZE_CLASS_CONTROL,
} IrVectorizeClass;

typedef struct IrVectorizeReduction IrVectorizeReduction;
struct IrVectorizeReduction
{
    uint32_t phi;
    uint32_t lane;
    uint32_t operation;
};

// One loop's plan, from ir_vectorize_plan, for ir_vectorize_emit.
typedef struct IrVectorizeLoop IrVectorizeLoop;
struct IrVectorizeLoop
{
    uint32_t header;
    uint32_t preheader;
    uint32_t exit;
    uint32_t latch;
    // The loop's rows in order: the header's non-phi rows, then each chain block's (IrVectorizeWork.rows).
    uint32_t row_count;
    uint32_t header_rows;
    uint32_t induction;
    uint32_t start;
    uint32_t bound;
    uint32_t increment;
    uint32_t compare;
    // The exit test's `iv + offset` row in the header, or BUSTER_IR_NONE where it tests the induction itself.
    uint32_t offset_row;
    uint64_t offset;
    // IR_ULT or IR_LT: the exit test's predicate with the induction on the left.
    uint32_t predicate;
    uint32_t lanes;
    // Lanes per vector iteration, and the widest vector's width index.
    uint32_t width;
    // Whether the tail is one masked vector iteration (#2807, ir_vectorize_masked_legal), not the scalar loop's.
    bool masked;
    bool stores;
    // Whether a load is strided (#2808): its tail is the scalar loop's.
    bool strided;
    uint32_t reductions;
    uint32_t guards;
    // The accesses whose ranges G tests against each other: guard_pairs[2k] and [2k + 1].
    uint32_t guard_pairs[2 * BUSTER_IR_VECTORIZE_GUARD_LIMIT];
    // The 64-bit extensions of 32-bit indices (#2808), whose values G tests for a wrap over the vector iterations.
    uint32_t narrows;
    uint32_t narrow_rows[BUSTER_IR_VECTORIZE_GUARD_LIMIT];
};

// The function being built: original rows and blocks in place, then the new ones. While counting, nothing is
// written: the counts size the output for the replay that writes it.
typedef struct IrVectorizeBuild IrVectorizeBuild;
struct IrVectorizeBuild
{
    bool counting;
    IrFunction* view;
    uint32_t rows;
    uint32_t blocks;
    uint32_t extras;
    uint32_t constants;
    // The vector constants (a strided load's permutes' indices) and addresses (its loads' offsets) appended (#2808).
    uint32_t vector_constants;
    uint32_t addresses;
};

typedef struct IrVectorizeWork IrVectorizeWork;
struct IrVectorizeWork
{
    const IrFunction* function;
    const IrVectorizeTarget* target;
    uint32_t mutation;
    // The function's blocks from their terminators alone (ir_vectorize_graph): whether each is reachable from the entry,
    // its reachable predecessors' count (at most 3) and its first and last, and the headers of the depth-first walk's
    // back edges, each once: the loops examined.
    uint8_t* reached;
    uint8_t* predecessor_count;
    uint32_t* predecessor_first;
    uint32_t* predecessor_last;
    uint32_t* headers;
    uint32_t header_count;
    // The loop being planned's blocks and rows, marked with its ordinal plus one (no reset: each loop's mark differs).
    uint32_t* block_marks;
    uint32_t* row_marks;
    // Per instruction: its admission kind, the loop pass's class, needed bit, coefficient (an index's multiplier or a
    // pointer's bytes per iteration), lanes' bytes' log2 and whether floating, its clones' values (in VB or G, the
    // guards' pointer clone with the induction at its start, and its splat), and the extension's two constants.
    uint8_t* kinds;
    uint8_t* classes;
    // Valid for UPDATE rows only: the reduction whose partial result they produce.
    uint8_t* reduction_index;
    IrVectorizeReduction reduction[BUSTER_IR_VECTORIZE_REDUCTION_LIMIT];
    uint8_t* needed;
    uint8_t* sizes;
    uint8_t* floating;
    uint8_t* placed_g;
    int64_t* coefficients;
    uint32_t* values;
    uint32_t* starts;
    // The G clones of a 32-bit index's chain at the vector iterations' last induction (#2808).
    uint32_t* lasts;
    uint32_t* splats;
    uint32_t* ones;
    uint32_t* zeros;
    // The loop's rows in order, and the values whose per-loop entries were set (for their reset).
    uint32_t* rows;
    // ir_vectorize_start_chain's stack: values and whether their operands were pushed.
    uint32_t* chain;
    uint8_t* chain_expanded;
    uint64_t chain_capacity;
    uint32_t* touched;
    uint32_t touched_count;
    IrVectorizeResult statistics;
};

// The admission kinds by opcode. Integer and pointer arithmetic, comparisons, selects and conversions are PURE, SAFE
// where LICM would speculate them; floating arithmetic is FLOAT (vectorized, never speculated); a phi, a branch, a
// load and a store have their own kinds; everything else (calls, divisions, fences, atomics, undef, stack operations,
// masked and gathered memory, and existing vector rows) refuses. fma and the same-width integer/float
// conversions are FLOAT (#2806): vectorized, and never speculated or given a masked tail.
static const uint8_t ir_vectorize_kind_table[BUSTER_IR_OP_TABLE] = {
    [IR_CONST] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_PARAM] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_COPY] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_ADD] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_SUB] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_MUL] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_AND] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_OR] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_XOR] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_SHL] = BUSTER_IR_VECTORIZE_KIND_PURE,
    [IR_LSHR] = BUSTER_IR_VECTORIZE_KIND_PURE,
    [IR_ASHR] = BUSTER_IR_VECTORIZE_KIND_PURE,
    [IR_ICMP] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_SELECT] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_TRUNC] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_ZEXT] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_SEXT] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_INTTOPTR] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_PTRTOINT] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_STACK_ADDR] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_SYMBOL_ADDR] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_ADDRESS] = BUSTER_IR_VECTORIZE_KIND_PURE | BUSTER_IR_VECTORIZE_KIND_SAFE,
    [IR_FADD] = BUSTER_IR_VECTORIZE_KIND_FLOAT,
    [IR_FSUB] = BUSTER_IR_VECTORIZE_KIND_FLOAT,
    [IR_FMUL] = BUSTER_IR_VECTORIZE_KIND_FLOAT,
    [IR_FDIV] = BUSTER_IR_VECTORIZE_KIND_FLOAT,
    [IR_FMA] = BUSTER_IR_VECTORIZE_KIND_FLOAT,
    [IR_SITOFP] = BUSTER_IR_VECTORIZE_KIND_FLOAT,
    [IR_UITOFP] = BUSTER_IR_VECTORIZE_KIND_FLOAT,
    [IR_FPTOSI] = BUSTER_IR_VECTORIZE_KIND_FLOAT,
    [IR_FPTOUI] = BUSTER_IR_VECTORIZE_KIND_FLOAT,
    [IR_LOAD] = BUSTER_IR_VECTORIZE_KIND_LOAD,
    [IR_STORE] = BUSTER_IR_VECTORIZE_KIND_STORE,
    [IR_PHI] = BUSTER_IR_VECTORIZE_KIND_PHI,
    [IR_BR] = BUSTER_IR_VECTORIZE_KIND_BRANCH,
    [IR_CBR] = BUSTER_IR_VECTORIZE_KIND_BRANCH,
    [IR_INTRINSIC] = BUSTER_IR_VECTORIZE_KIND_INTRINSIC,
};

// Every row's admission kind: its opcode's in the table, and FORBID for a load or a store with a semantic access flag
// (volatile, atomic, a segment; COPY_PIECE is only a hint). 64 rows at a time: one two-register byte lookup of the
// opcodes and one compare of the flags.
static void ir_vectorize_kinds(const IrFunction* f, uint8_t* kinds)
{
    const __m512i low = _mm512_loadu_si512(ir_vectorize_kind_table);
    const __m512i high = _mm512_loadu_si512(ir_vectorize_kind_table + 64);
    for (uint64_t first = 0; first < f->instruction_count; first += BUSTER_IR_VECTORIZE_BATCH)
    {
        const uint64_t left = f->instruction_count - first;
        const __mmask64 valid = left >= BUSTER_IR_VECTORIZE_BATCH ? UINT64_MAX : (UINT64_C(1) << left) - 1;
        const __m512i op = _mm512_maskz_loadu_epi8(valid, f->op + first);
        const __m512i flags = _mm512_maskz_loadu_epi8(valid, f->flags + first);
        const __m512i kind = _mm512_permutex2var_epi8(low, op, high);
        const __m512i base = _mm512_and_si512(kind, _mm512_set1_epi8(7));
        const __mmask64 memory = _mm512_cmpeq_epi8_mask(base, _mm512_set1_epi8(BUSTER_IR_VECTORIZE_KIND_LOAD)) | _mm512_cmpeq_epi8_mask(base, _mm512_set1_epi8(BUSTER_IR_VECTORIZE_KIND_STORE));
        const __mmask64 flagged = memory & _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_IR_ACCESS_FLAGS & ~BUSTER_IR_COPY_PIECE));
        const __m512i result = _mm512_mask_mov_epi8(kind, flagged, _mm512_setzero_si512());
        _mm512_mask_storeu_epi8(kinds + first, valid, result);
    }
}

// Whether rows [first, end) hold a refusing kind, 64 at a time.
static bool ir_vectorize_forbidden(const uint8_t* kinds, uint32_t first, uint32_t end)
{
    bool forbidden = false;
    for (uint64_t at = first; at < end; at += BUSTER_IR_VECTORIZE_BATCH)
    {
        const uint64_t left = end - at;
        const __mmask64 valid = left >= BUSTER_IR_VECTORIZE_BATCH ? UINT64_MAX : (UINT64_C(1) << left) - 1;
        const __m512i kind = _mm512_maskz_loadu_epi8(valid, kinds + at);
        const __mmask64 bad = valid & _mm512_cmpeq_epi8_mask(_mm512_and_si512(kind, _mm512_set1_epi8(7)), _mm512_setzero_si512());
        forbidden = forbidden || bad != 0;
    }
    return forbidden;
}

// `value`'s bytes' log2 for a scalar of 1 to 8 bytes, or 4 for any other type.
static uint32_t ir_vectorize_size(uint32_t type)
{
    const uint32_t result = type == IR_I8 ? 0 : type == IR_I16 ? 1 : type == IR_I32 || type == IR_F32 ? 2 : type == IR_I64 || type == IR_F64 ? 3 : 4;
    return result;
}

static uint32_t ir_vectorize_lane(uint32_t size, bool floating)
{
    static const uint8_t integers[4] = { IR_SIMD_LANE_I8, IR_SIMD_LANE_I16, IR_SIMD_LANE_I32, IR_SIMD_LANE_I64 };
    const uint32_t result = floating ? (size == 2 ? IR_SIMD_LANE_F32 : IR_SIMD_LANE_F64) : integers[size & 3];
    return result;
}

static uint32_t ir_vectorize_vector_type(uint32_t bytes)
{
    const uint32_t result = bytes == 16 ? IR_V4I32 : bytes == 32 ? IR_V8I32 : IR_V16I32;
    return result;
}

static uint32_t ir_vectorize_mask_type(uint32_t lanes)
{
    const uint32_t result = lanes <= 8 ? IR_M8 : lanes == 16 ? IR_M16 : lanes == 32 ? IR_M32 : IR_M64;
    return result;
}

// Whether `bits` (a width bit set of IrVectorizeTarget) has width `bytes`.
static bool ir_vectorize_has(uint32_t bits, uint32_t bytes)
{
    const uint32_t index = bytes == 16 ? 0 : bytes == 32 ? 1 : bytes == 64 ? 2 : 3;
    const bool result = index < BUSTER_IR_VECTORIZE_WIDTHS && ((bits >> index) & 1) != 0;
    return result;
}

static uint64_t ir_vectorize_type_mask(uint32_t type)
{
    const uint32_t bits = ir_type_bits[type];
    const uint64_t result = bits >= 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    return result;
}

// Whether `value` is a constant, through constant rows and integer conversions of one (the lowering's `sext (const)`),
// and its bits at its type in *bits.
static bool ir_vectorize_constant(const IrFunction* f, uint32_t value, uint64_t* bits)
{
    uint32_t chain[4] = { 0 };
    uint32_t depth = 0;
    uint32_t at = value;
    while (at < f->instruction_count && depth < 4 && (f->op[at] == IR_SEXT || f->op[at] == IR_ZEXT || f->op[at] == IR_TRUNC || f->op[at] == IR_COPY))
    {
        chain[depth] = at;
        depth += 1;
        at = f->a[at];
    }
    const bool known = at < f->instruction_count && f->op[at] == IR_CONST && f->type[at] <= IR_I64 && f->type[at] >= IR_I1;
    uint64_t result = known ? f->constants[f->a[at]].low & ir_vectorize_type_mask(f->type[at]) : 0;
    uint32_t type = known ? f->type[at] : IR_VOID;
    for (uint32_t k = depth; known && k > 0; k -= 1)
    {
        const uint32_t row = chain[k - 1];
        const uint32_t to = f->type[row];
        const bool valid = to >= IR_I1 && to <= IR_I64;
        if (valid && f->op[row] == IR_SEXT)
        {
            const uint32_t bits = ir_type_bits[type];
            const uint64_t sign = bits >= 64 ? 0 : (result >> (bits - 1)) & 1;
            result = sign != 0 ? result | ~ir_vectorize_type_mask(type) : result;
        }
        result = valid ? result & ir_vectorize_type_mask(to) : result;
        type = to;
    }
    *bits = result;
    return known && type >= IR_I1 && type <= IR_I64;
}

// `value` past the conversions between a pointer and its integer and the copies, which move no bits.
static uint32_t ir_vectorize_root(const IrFunction* f, uint32_t value)
{
    uint32_t at = value;
    uint32_t depth = 0;
    while (at < f->instruction_count && depth < BUSTER_IR_VECTORIZE_DEPTH && (f->op[at] == IR_INTTOPTR || f->op[at] == IR_PTRTOINT || f->op[at] == IR_COPY))
    {
        at = f->a[at];
        depth += 1;
    }
    return at;
}

// Whether two values are the same address or index by their chains: one value, equal constants, or one operation of
// equal operands (an address's scale and offset by content), at most `depth` operations deep. The pairs still to compare
// are an explicit stack (the call graph's no-recursion rule): each pair leaves at most one sibling behind, so
// BUSTER_IR_VECTORIZE_SAME_STACK holds every depth's.
#define BUSTER_IR_VECTORIZE_SAME_STACK (2u * BUSTER_IR_VECTORIZE_DEPTH + 4u)
static bool ir_vectorize_same(const IrFunction* f, uint32_t x, uint32_t y, uint32_t depth)
{
    uint32_t lefts[BUSTER_IR_VECTORIZE_SAME_STACK] = { 0 };
    uint32_t rights[BUSTER_IR_VECTORIZE_SAME_STACK] = { 0 };
    uint32_t depths[BUSTER_IR_VECTORIZE_SAME_STACK] = { 0 };
    uint32_t count = 1;
    lefts[0] = x;
    rights[0] = y;
    depths[0] = depth;
    bool result = true;
    while (result && count != 0)
    {
        count -= 1;
        const uint32_t a = ir_vectorize_root(f, lefts[count]);
        const uint32_t b = ir_vectorize_root(f, rights[count]);
        const uint32_t level = depths[count];
        if (a != b)
        {
            const bool comparable = level != 0 && a < f->instruction_count && b < f->instruction_count && f->op[a] == f->op[b] && f->type[a] == f->type[b];
            const uint32_t op = comparable ? f->op[a] : IR_INVALID;
            uint64_t ka = 0;
            uint64_t kb = 0;
            // The operand pairs this pair needs equal: a, then b where the operation has it.
            uint32_t pairs = 0;
            bool second = false;
            if (op == IR_CONST)
            {
                result = ir_vectorize_constant(f, a, &ka) && ir_vectorize_constant(f, b, &kb) && ka == kb;
            }
            else if (op == IR_ADDRESS)
            {
                const IrAddress da = f->addresses[f->c[a]];
                const IrAddress db = f->addresses[f->c[b]];
                const bool indexed = f->b[a] != BUSTER_IR_NONE;
                result = da.offset == db.offset && da.scale == db.scale && indexed == (f->b[b] != BUSTER_IR_NONE);
                pairs = 1;
                second = indexed;
            }
            else if (op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_SHL)
            {
                pairs = 1;
                second = true;
            }
            else if (op == IR_SEXT || op == IR_ZEXT)
            {
                pairs = 1;
            }
            else
            {
                result = false;
            }
            const uint32_t pushes = pairs + (second ? 1u : 0u);
            result = result && count + pushes <= BUSTER_IR_VECTORIZE_SAME_STACK;
            if (result && pairs != 0)
            {
                lefts[count] = f->a[a];
                rights[count] = f->a[b];
                depths[count] = level - 1;
                count += 1;
            }
            if (result && second)
            {
                lefts[count] = f->b[a];
                rights[count] = f->b[b];
                depths[count] = level - 1;
                count += 1;
            }
        }
    }
    return result;
}

// Whether `value` is a row of loop `loop` (its ordinal), as ir_vectorize_shape marked its blocks' rows.
static bool ir_vectorize_inside(const IrVectorizeWork* work, uint32_t loop, uint32_t value)
{
    const bool result = value < work->function->instruction_count && work->row_marks[value] == loop + 1;
    return result;
}

// Whether `block` is a block of loop `loop`.
static bool ir_vectorize_block_inside(const IrVectorizeWork* work, uint32_t loop, uint32_t block)
{
    const bool result = block < work->function->block_count && work->block_marks[block] == loop + 1;
    return result;
}

// Whether row `value` is in block `block`.
static bool ir_vectorize_in_block(const IrFunction* f, uint32_t block, uint32_t value)
{
    const bool result = value >= f->block_offsets[block] && value < f->block_offsets[(uint64_t)block + 1];
    return result;
}

// A phi's incoming value from `block`, or BUSTER_IR_NONE.
static uint32_t ir_vectorize_incoming(const IrFunction* f, uint32_t phi, uint32_t block)
{
    uint32_t result = BUSTER_IR_NONE;
    for (uint32_t k = 0; k < f->b[phi]; k += 1)
    {
        result = f->extra[f->a[phi] + k * 2] == block ? f->extra[f->a[phi] + k * 2 + 1] : result;
    }
    return result;
}

static void ir_vectorize_touch(IrVectorizeWork* work, uint32_t value)
{
    work->touched[work->touched_count] = value;
    work->touched_count += 1;
}

// The per-loop entries back to their empty state, for the touched values alone.
static void ir_vectorize_reset(IrVectorizeWork* work)
{
    for (uint32_t k = 0; k < work->touched_count; k += 1)
    {
        const uint32_t v = work->touched[k];
        work->classes[v] = IR_VECTORIZE_CLASS_NONE;
        work->needed[v] = 0;
        work->sizes[v] = 0;
        work->floating[v] = 0;
        work->placed_g[v] = 0;
        work->coefficients[v] = 0;
        work->values[v] = BUSTER_IR_NONE;
        work->starts[v] = BUSTER_IR_NONE;
        work->lasts[v] = BUSTER_IR_NONE;
        work->splats[v] = BUSTER_IR_NONE;
        work->ones[v] = BUSTER_IR_NONE;
        work->zeros[v] = BUSTER_IR_NONE;
    }
    work->touched_count = 0;
}

// An operand's class for the loop: a value defined outside it is uniform.
static uint32_t ir_vectorize_class(const IrVectorizeWork* work, uint32_t loop, uint32_t value)
{
    const uint32_t result = !ir_vectorize_inside(work, loop, value) ? IR_VECTORIZE_CLASS_UNIFORM : work->classes[value];
    return result;
}

// Whether a uniform operand is available in G: defined outside the loop, or a uniform clone placed there.
static bool ir_vectorize_in_g(const IrVectorizeWork* work, uint32_t loop, uint32_t value)
{
    const bool result = !ir_vectorize_inside(work, loop, value) || work->placed_g[value] != 0;
    return result;
}

static void ir_vectorize_set(IrVectorizeWork* work, uint32_t i, uint32_t class, int64_t coefficient, uint32_t size, bool floating)
{
    work->classes[i] = (uint8_t)class;
    work->coefficients[i] = coefficient;
    work->sizes[i] = (uint8_t)size;
    work->floating[i] = floating ? 1 : 0;
    ir_vectorize_touch(work, i);
}

// A branch's condition past the lowering's truth chains, `icmp ne (zext c), 0`, which truth simplification removes in
// Tier 1's pipeline but other clients may leave: c.
static uint32_t ir_vectorize_truth(const IrFunction* f, uint32_t value)
{
    uint32_t at = value;
    uint64_t zero = 1;
    uint32_t depth = 0;
    while (depth < BUSTER_IR_VECTORIZE_DEPTH && at < f->instruction_count && f->op[at] == IR_ICMP && f->c[at] == IR_NE && f->a[at] < f->instruction_count && f->op[f->a[at]] == IR_ZEXT &&
           f->type[f->a[f->a[at]]] == IR_I1 && ir_vectorize_constant(f, f->b[at], &zero) && zero == 0)
    {
        at = f->a[f->a[at]];
        depth += 1;
    }
    return at;
}

// A comparison's operands and predicate as the vector loop compares them (#2808): where both operands extend one
// narrower integer type alike, the extensions' sources at that type, so that `a[i] == v` of bytes promoted to int
// compares bytes, as many lanes as they are; else its own. Both extensions preserve both orders, signed and unsigned,
// except that zero extensions' values are non-negative: a signed predicate of them is the sources' unsigned one.
static void ir_vectorize_compared(const IrFunction* f, uint32_t i, uint32_t* x, uint32_t* y, uint32_t* predicate)
{
    const uint32_t a = f->a[i];
    const uint32_t b = f->b[i];
    const uint32_t pred = f->c[i];
    const bool extensions = a < f->instruction_count && b < f->instruction_count && (f->op[a] == IR_ZEXT || f->op[a] == IR_SEXT) && f->op[b] == f->op[a];
    const uint32_t from = extensions ? f->type[f->a[a]] : IR_VOID;
    const bool narrow = extensions && from == f->type[f->a[b]] && from >= IR_I8 && from <= IR_I64 && ir_vectorize_size(from) < ir_vectorize_size(f->type[a]);
    const bool unsigned_order = narrow && f->op[a] == IR_ZEXT && pred >= IR_LT && pred <= IR_GE;
    *x = narrow ? f->a[a] : a;
    *y = narrow ? f->a[b] : b;
    *predicate = unsigned_order ? pred + (IR_ULT - IR_LT) : pred;
}

// Meet the branch's two edges at one join, directly or through a private block ending in br. Both
// predecessors of the join are accounted for. In particular an early exit or a second loop cannot be an arm.
static uint32_t ir_vectorize_join(const IrVectorizeWork* work, uint32_t header, uint32_t head, uint32_t from[2])
{
    const IrFunction* f = work->function;
    const uint32_t branch = f->block_offsets[(uint64_t)head + 1] - 1;
    uint32_t ends[2] = { f->b[branch], f->c[branch] };
    bool valid = ends[0] != ends[1];
    for (uint32_t side = 0; valid && side < 2; side += 1)
    {
        const uint32_t arm = ends[side];
        from[side] = head;
        valid = arm != 0 && arm != head && arm != header;
        if (valid && work->predecessor_count[arm] == 1)
        {
            const uint32_t end = f->block_offsets[(uint64_t)arm + 1] - 1;
            valid = work->predecessor_first[arm] == head && f->op[end] == IR_BR;
            ends[side] = f->a[end];
            from[side] = arm;
        }
    }
    const uint32_t join = ends[0];
    valid = valid && join == ends[1] && join != 0 && join != head && join != header && work->predecessor_count[join] == 2;
    return valid ? join : BUSTER_IR_NONE;
}

static bool ir_vectorize_record_reduction(IrVectorizeWork* work, IrVectorizeLoop* plan, uint32_t update, IrVectorizeReduction reduction)
{
    const uint32_t index = plan->reductions;
    const bool valid = index < BUSTER_IR_VECTORIZE_REDUCTION_LIMIT && work->classes[update] != IR_VECTORIZE_CLASS_UPDATE;
    if (valid)
    {
        work->reduction[index] = reduction;
        plan->reductions = index + 1;
        work->reduction_index[update] = (uint8_t)index;
        ir_vectorize_set(work, update, IR_VECTORIZE_CLASS_UPDATE, 0, ir_vectorize_size(work->function->type[update]), false);
    }
    return valid;
}

// Match both values selected by the comparison. Ties have the same bits in either arm. A distinct reload
// must be the head's own plain load with no intervening writer. The arm check below excludes any write there.
static bool ir_vectorize_minmax(IrVectorizeWork* work, IrVectorizeLoop* plan, uint32_t update, uint32_t condition, uint32_t yes, uint32_t no, uint32_t head, const uint32_t from[2])
{
    const IrFunction* f = work->function;
    const uint32_t cmp = ir_vectorize_truth(f, condition);
    const bool keep_true = ir_vectorize_in_block(f, plan->header, yes) && f->op[yes] == IR_PHI && ir_vectorize_incoming(f, yes, plan->latch) == update;
    const uint32_t phi = keep_true ? yes : no;
    const uint32_t chosen = keep_true ? no : yes;
    bool valid = ir_vectorize_in_block(f, plan->header, phi) && f->op[phi] == IR_PHI && f->type[phi] == IR_I32 && f->type[update] == IR_I32 &&
                 chosen < f->instruction_count && cmp < f->instruction_count && f->op[cmp] == IR_ICMP && ir_vectorize_incoming(f, phi, plan->latch) == update;
    if (valid)
    {
        const uint32_t pred = f->c[cmp];
        const bool left = f->a[cmp] == phi;
        const uint32_t lane = left ? f->b[cmp] : f->a[cmp];
        valid = (left != (f->b[cmp] == phi)) && pred >= IR_LT && pred <= IR_UGE;
        if (valid && chosen != lane)
        {
            const uint32_t arm = from != NULL ? from[keep_true ? 1 : 0] : head;
            valid = arm != head && ir_vectorize_in_block(f, arm, chosen) && ir_vectorize_in_block(f, head, lane) && f->op[lane] == IR_LOAD && f->op[chosen] == IR_LOAD &&
                    ((f->flags[lane] | f->flags[chosen]) & ~BUSTER_IR_COPY_PIECE) == 0 && f->type[chosen] == IR_I32 &&
                    ir_vectorize_same(f, f->a[lane], f->a[chosen], BUSTER_IR_VECTORIZE_DEPTH);
            const uint32_t end = valid ? f->block_offsets[(uint64_t)head + 1] - 1 : 0;
            for (uint32_t i = lane + 1; valid && i < end; i += 1)
            {
                const uint32_t kind = work->kinds[i] & 7;
                valid = kind == BUSTER_IR_VECTORIZE_KIND_PURE || kind == BUSTER_IR_VECTORIZE_KIND_LOAD || kind == BUSTER_IR_VECTORIZE_KIND_FLOAT;
            }
        }
        if (valid)
        {
            const bool greater = pred == IR_GT || pred == IR_GE || pred == IR_UGT || pred == IR_UGE;
            const bool true_selects_left = left == keep_true;
            const bool maximum = greater == true_selects_left;
            // lane_op and reduce share 0/1 for unsigned/signed min and 2/3 for unsigned/signed max.
            const IrVectorizeReduction reduction = { .phi = phi, .lane = lane, .operation = (maximum ? 2u : 0u) + (pred < IR_ULT) };
            valid = ir_vectorize_record_reduction(work, plan, update, reduction);
        }
    }
    return valid;
}

// Follow each header edge through single-entry blocks and two-edge joins back to the header. Each internal
// conditional must close at a private join (ir_vectorize_join), so the header dominates the entire body. The header's
// reachable predecessors must be the chain's last block (the latch) and one other, the preheader, which ends in a
// `br` to it. Fills the plan's blocks and returns the chain's first block, or BUSTER_IR_NONE where the blocks aren't
// the admitted shape. Reads the blocks alone: a function none of whose loops has it needs no row of the pass's.
static uint32_t ir_vectorize_chain(const IrVectorizeWork* work, uint32_t loop, IrVectorizeLoop* plan)
{
    const IrFunction* f = work->function;
    const uint32_t header = work->headers[loop];
    const uint32_t terminator = f->block_offsets[(uint64_t)header + 1] - 1;
    plan->header = header;
    plan->preheader = BUSTER_IR_NONE;
    plan->latch = BUSTER_IR_NONE;
    plan->exit = BUSTER_IR_NONE;
    // The entry has no preheader: its predecessors would all be the loop's.
    bool valid = header != 0 && f->op[terminator] == IR_CBR && f->b[terminator] != f->c[terminator] && work->predecessor_count[header] == 2;
    // Which of the branch's targets starts the chain back to the header.
    uint32_t first = BUSTER_IR_NONE;
    for (uint32_t side = 0; valid && side < 2; side += 1)
    {
        const uint32_t target = side == 0 ? f->b[terminator] : f->c[terminator];
        uint32_t previous = header;
        uint32_t block = target;
        uint32_t steps = 0;
        bool chain = true;
        bool join = false;
        while (chain && block != header && steps < f->block_count)
        {
            const uint32_t end = f->block_offsets[(uint64_t)block + 1] - 1;
            chain = block != 0 && work->reached[block] != 0 && (join || (work->predecessor_count[block] == 1 && work->predecessor_first[block] == previous));
            previous = block;
            join = chain && f->op[end] == IR_CBR;
            if (join)
            {
                uint32_t from[2];
                block = ir_vectorize_join(work, header, block, from);
                chain = block != BUSTER_IR_NONE;
            }
            else
            {
                chain = chain && f->op[end] == IR_BR;
                block = chain ? f->a[end] : block;
            }
            steps += 1;
        }
        const bool closes = chain && block == header;
        valid = !(closes && first != BUSTER_IR_NONE);
        first = closes ? target : first;
        plan->latch = closes ? previous : plan->latch;
        plan->exit = closes ? (side == 0 ? f->c[terminator] : f->b[terminator]) : plan->exit;
    }
    valid = valid && first != BUSTER_IR_NONE;
    if (valid)
    {
        const uint32_t a = work->predecessor_first[header];
        const uint32_t b = work->predecessor_last[header];
        plan->preheader = a == plan->latch ? b : a;
        valid = (a == plan->latch) != (b == plan->latch) && f->op[f->block_offsets[(uint64_t)plan->preheader + 1] - 1] == IR_BR;
    }
    return valid ? first : BUSTER_IR_NONE;
}

// The loop's shape (ir_vectorize_chain), then its blocks' and rows' marks and the plan's rows: the header's non-phi
// rows, then each unconditional block's. A private arm contributes only its winning value to a matched update.
static bool ir_vectorize_shape(IrVectorizeWork* work, uint32_t loop, IrVectorizeLoop* plan)
{
    const IrFunction* f = work->function;
    const uint32_t first = ir_vectorize_chain(work, loop, plan);
    const uint32_t header = plan->header;
    const uint32_t mark = loop + 1;
    const uint32_t terminator = f->block_offsets[(uint64_t)header + 1] - 1;
    bool valid = first != BUSTER_IR_NONE;
    uint32_t count = 0;
    if (valid)
    {
        work->block_marks[header] = mark;
        for (uint32_t i = f->block_offsets[header]; i <= terminator; i += 1)
        {
            work->row_marks[i] = mark;
            if (f->op[i] != IR_PHI && i < terminator)
            {
                work->rows[count] = i;
                count += 1;
            }
        }
    }
    plan->header_rows = count;
    uint32_t join_phi = BUSTER_IR_NONE;
    for (uint32_t block = first; valid && block != header;)
    {
        const uint32_t end = f->block_offsets[(uint64_t)block + 1] - 1;
        work->block_marks[block] = mark;
        for (uint32_t i = f->block_offsets[block]; i <= end; i += 1)
        {
            work->row_marks[i] = mark;
            // The matched join's first phi is the update. Every other phi on the chain must be a copy.
            valid = valid && (f->op[i] != IR_PHI || f->b[i] == 1 || i == join_phi);
            if (i < end)
            {
                work->rows[count] = i;
                count += 1;
            }
        }
        if (f->op[end] == IR_CBR)
        {
            uint32_t from[2];
            const uint32_t join = ir_vectorize_join(work, header, block, from);
            // ir_vectorize_chain already proved both edges meet here.
            join_phi = f->block_offsets[join];
            valid = valid && f->op[join_phi] == IR_PHI && f->b[join_phi] == 2;
            const uint32_t yes = valid ? ir_vectorize_incoming(f, join_phi, from[0]) : BUSTER_IR_NONE;
            const uint32_t no = valid ? ir_vectorize_incoming(f, join_phi, from[1]) : BUSTER_IR_NONE;
            valid = valid && ir_vectorize_minmax(work, plan, join_phi, f->a[end], yes, no, block, from);
            for (uint32_t side = 0; valid && side < 2; side += 1)
            {
                const uint32_t arm = from[side];
                if (arm != block)
                {
                    work->block_marks[arm] = mark;
                    const uint32_t stop = f->block_offsets[(uint64_t)arm + 1] - 1;
                    for (uint32_t i = f->block_offsets[arm]; valid && i < stop; i += 1)
                    {
                        work->row_marks[i] = mark;
                        // These rows disappear only from the vector path. Its only arm load may be the matched reload.
                        valid = (work->kinds[i] & BUSTER_IR_VECTORIZE_KIND_SAFE) != 0 ||
                                (work->kinds[i] == BUSTER_IR_VECTORIZE_KIND_LOAD && i == (side == 0 ? yes : no));
                    }
                }
            }
            block = join;
        }
        else
        {
            block = f->a[end];
        }
    }
    plan->row_count = valid ? count : 0;
    return valid;
}

// The index-th successor of `block` by its terminator, or BUSTER_IR_NONE past the last: a branch's target, a
// conditional branch's two, a switch's default and cases. A return, `unreachable` and an indirect branch (a function
// with labels is never planned) have none.
static uint32_t ir_vectorize_successor(const IrFunction* f, uint32_t block, uint32_t index)
{
    const uint32_t end = f->block_offsets[(uint64_t)block + 1] - 1;
    const uint32_t op = f->op[end];
    uint32_t result = BUSTER_IR_NONE;
    if (op == IR_BR)
    {
        result = index == 0 ? f->a[end] : BUSTER_IR_NONE;
    }
    else if (op == IR_CBR)
    {
        result = index == 0 ? f->b[end] : index == 1 ? f->c[end] : BUSTER_IR_NONE;
    }
    else if (op == IR_SWITCH)
    {
        result = index == 0 ? f->extra[f->b[end]] : index <= f->c[end] ? f->extra[f->b[end] + index * 2] : BUSTER_IR_NONE;
    }
    return result;
}

// The depth-first walk's first visit of `block`, whose ancestors are the active blocks: it becomes active, and each of
// its distinct successors counts it as a reachable predecessor (an edge repeated from one block once) and is a loop's
// header, once, where it is active (an ancestor's, or its own, back edge).
static void ir_vectorize_visit(IrVectorizeWork* work, uint32_t block, uint8_t* active, uint8_t* header, uint32_t* source)
{
    const IrFunction* f = work->function;
    work->reached[block] = 1;
    active[block] = 1;
    uint32_t next = ir_vectorize_successor(f, block, 0);
    for (uint32_t k = 1; next != BUSTER_IR_NONE; k += 1)
    {
        if (next < f->block_count && source[next] != block)
        {
            source[next] = block;
            work->predecessor_first[next] = work->predecessor_count[next] == 0 ? block : work->predecessor_first[next];
            work->predecessor_last[next] = block;
            work->predecessor_count[next] += work->predecessor_count[next] < 3 ? 1 : 0;
            if (active[next] != 0 && header[next] == 0)
            {
                header[next] = 1;
                work->headers[work->header_count] = next;
                work->header_count += 1;
            }
        }
        next = ir_vectorize_successor(f, block, k);
    }
}

// The blocks' graph from their terminators: one iterative depth-first walk from the entry marks the reachable blocks,
// counts each block's reachable predecessors with its first and last, and collects each back edge's header once (an
// edge to a block on the walk's stack). A block's edges are all read at its first visit, when the stack holds its
// ancestors, as they would be one by one. No instruction is verified and no dominator tree is built: the loops the pass
// takes need neither (ir_vectorize_shape). Returns false where scratch has no room.
static bool ir_vectorize_graph(IrVectorizeWork* work, Arena* scratch)
{
    const IrFunction* f = work->function;
    const uint64_t blocks = f->block_count;
    bool ok = true;
    work->reached = ir_allocate(scratch, blocks, 1, &ok);
    work->predecessor_count = ir_allocate(scratch, blocks, 1, &ok);
    work->predecessor_first = ir_allocate(scratch, blocks, 4, &ok);
    work->predecessor_last = ir_allocate(scratch, blocks, 4, &ok);
    work->headers = ir_allocate(scratch, blocks, 4, &ok);
    work->block_marks = ir_allocate(scratch, blocks, 4, &ok);
    uint8_t* active = ir_allocate(scratch, blocks, 1, &ok);
    uint8_t* header = ir_allocate(scratch, blocks, 1, &ok);
    uint32_t* source = ir_allocate(scratch, blocks, 4, &ok);
    uint32_t* stack = ir_allocate(scratch, blocks, 4, &ok);
    uint32_t* cursor = ir_allocate(scratch, blocks, 4, &ok);
    if (ok)
    {
        memset(source, 0xff, blocks * 4);
        uint32_t depth = 1;
        stack[0] = 0;
        cursor[0] = 0;
        ir_vectorize_visit(work, 0, active, header, source);
        while (depth != 0)
        {
            const uint32_t block = stack[depth - 1];
            const uint32_t next = ir_vectorize_successor(f, block, cursor[depth - 1]);
            cursor[depth - 1] += 1;
            if (next == BUSTER_IR_NONE)
            {
                active[block] = 0;
                depth -= 1;
            }
            else if (next < blocks && work->reached[next] == 0)
            {
                stack[depth] = next;
                cursor[depth] = 0;
                depth += 1;
                ir_vectorize_visit(work, next, active, header, source);
            }
        }
    }
    return ok;
}

// Whether `value` is a header phi, or a header row adding a constant to one (the exit test's `i + 1 < n`): its phi.
static uint32_t ir_vectorize_tested(const IrFunction* f, const IrVectorizeLoop* plan, uint32_t value, uint64_t* offset)
{
    uint32_t result = BUSTER_IR_NONE;
    uint64_t k = 0;
    *offset = 0;
    if (value < f->instruction_count && ir_vectorize_in_block(f, plan->header, value) && f->op[value] == IR_PHI)
    {
        result = value;
    }
    else if (value < f->instruction_count && ir_vectorize_in_block(f, plan->header, value) && f->op[value] == IR_ADD)
    {
        const uint32_t a = f->a[value];
        const uint32_t b = f->b[value];
        const bool left = f->op[a] == IR_PHI && ir_vectorize_in_block(f, plan->header, a) && ir_vectorize_constant(f, b, &k);
        const bool right = !left && f->op[b] == IR_PHI && ir_vectorize_in_block(f, plan->header, b) && ir_vectorize_constant(f, a, &k);
        result = left ? a : right ? b : BUSTER_IR_NONE;
        *offset = k;
    }
    return result;
}

// The exit test and the induction: the header's branch on `iv < n` or `iv + k < n` (unsigned or signed, either operand
// order and either branch sense) into the chain, the induction a phi of the header that starts at an invariant and
// steps by one.
static bool ir_vectorize_induction(IrVectorizeWork* work, uint32_t loop, IrVectorizeLoop* plan)
{
    const IrFunction* f = work->function;
    const uint32_t terminator = f->block_offsets[(uint64_t)plan->header + 1] - 1;
    const uint32_t compare = ir_vectorize_truth(f, f->a[terminator]);
    bool valid = compare < f->instruction_count && f->op[compare] == IR_ICMP && ir_vectorize_in_block(f, plan->header, compare);
    const bool into = valid && ir_vectorize_block_inside(work, loop, f->b[terminator]);
    uint32_t predicate = valid ? f->c[compare] : IR_PREDICATE_COUNT;
    uint32_t left = valid ? f->a[compare] : BUSTER_IR_NONE;
    uint32_t right = valid ? f->b[compare] : BUSTER_IR_NONE;
    // Normalize to the induction on the left, true into the loop: `n > iv` is `iv < n`, and `iv >= n` out is `iv < n` in.
    static const uint8_t swapped[IR_PREDICATE_COUNT] = { IR_EQ, IR_NE, IR_GT, IR_GE, IR_LT, IR_LE, IR_UGT, IR_UGE, IR_ULT, IR_ULE };
    static const uint8_t negated[IR_PREDICATE_COUNT] = { IR_NE, IR_EQ, IR_GE, IR_GT, IR_LE, IR_LT, IR_UGE, IR_UGT, IR_ULE, IR_ULT };
    uint64_t offset = 0;
    if (valid && ir_vectorize_tested(f, plan, left, &offset) == BUSTER_IR_NONE && ir_vectorize_tested(f, plan, right, &offset) != BUSTER_IR_NONE)
    {
        const uint32_t t = left;
        left = right;
        right = t;
        predicate = swapped[predicate];
    }
    predicate = valid && !into ? negated[predicate] : predicate;
    const uint32_t tested = valid ? ir_vectorize_tested(f, plan, left, &offset) : BUSTER_IR_NONE;
    plan->offset_row = tested != left ? left : BUSTER_IR_NONE;
    plan->offset = offset;
    left = tested;
    valid = valid && (predicate == IR_ULT || predicate == IR_LT) && left != BUSTER_IR_NONE && (f->type[left] == IR_I32 || f->type[left] == IR_I64) && f->b[left] == 2 &&
            f->type[right] == f->type[left] && (plan->offset_row == BUSTER_IR_NONE || f->type[plan->offset_row] == f->type[left]);
    const uint32_t start = valid ? ir_vectorize_incoming(f, left, plan->preheader) : BUSTER_IR_NONE;
    const uint32_t increment = valid ? ir_vectorize_incoming(f, left, plan->latch) : BUSTER_IR_NONE;
    valid = valid && start != BUSTER_IR_NONE && increment != BUSTER_IR_NONE && !ir_vectorize_inside(work, loop, start) && ir_vectorize_inside(work, loop, increment) &&
            f->op[increment] == IR_ADD && f->type[increment] == f->type[left] && (f->a[increment] == left || f->b[increment] == left);
    uint64_t step = 0;
    valid = valid && ir_vectorize_constant(f, f->a[increment] == left ? f->b[increment] : f->a[increment], &step) && step == 1;
    plan->compare = compare;
    plan->predicate = predicate;
    plan->induction = left;
    plan->bound = right;
    plan->start = start;
    plan->increment = increment;
    return valid;
}

// The loop's needed rows: its roots (loads, stores, floating operations, the exit test and the header phis' back edge
// values) and, in reverse order, the operands of each needed row inside the loop.
static void ir_vectorize_needed(IrVectorizeWork* work, uint32_t loop, const IrVectorizeLoop* plan)
{
    const IrFunction* f = work->function;
    for (uint32_t i = f->block_offsets[plan->header]; f->op[i] == IR_PHI; i += 1)
    {
        const uint32_t back = ir_vectorize_incoming(f, i, plan->latch);
        if (ir_vectorize_inside(work, loop, back))
        {
            work->needed[back] = 1;
            ir_vectorize_touch(work, back);
        }
    }
    work->needed[plan->compare] = 1;
    ir_vectorize_touch(work, plan->compare);
    for (uint32_t k = plan->row_count; k > 0; k -= 1)
    {
        const uint32_t i = work->rows[k - 1];
        const uint32_t kind = work->kinds[i] & 7;
        const bool root = kind == BUSTER_IR_VECTORIZE_KIND_LOAD || kind == BUSTER_IR_VECTORIZE_KIND_STORE || kind == BUSTER_IR_VECTORIZE_KIND_FLOAT || kind == BUSTER_IR_VECTORIZE_KIND_FORBID;
        if (root && work->needed[i] == 0)
        {
            work->needed[i] = 1;
            ir_vectorize_touch(work, i);
        }
        if (work->needed[i] != 0)
        {
            const bool update = work->classes[i] == IR_VECTORIZE_CLASS_UPDATE;
            const uint32_t mask = update || f->op[i] == IR_PHI ? 1u : ir_value_mask3(f, i);
            uint32_t first = update ? work->reduction[work->reduction_index[i]].lane : f->op[i] == IR_PHI ? f->extra[f->a[i] + 1] : f->a[i];
            uint32_t second = f->b[i];
            uint32_t predicate = 0;
            if (!update && f->op[i] == IR_ICMP)
            {
                ir_vectorize_compared(f, i, &first, &second, &predicate);
            }
            const uint32_t operands[3] = { first, second, ir_value_third(f, i) };
            for (uint32_t k2 = 0; k2 < 3; k2 += 1)
            {
                const uint32_t v = operands[k2];
                if (((mask >> k2) & 1) != 0 && ir_vectorize_inside(work, loop, v) && work->needed[v] == 0)
                {
                    work->needed[v] = 1;
                    ir_vectorize_touch(work, v);
                }
            }
        }
    }
}

// Whether a vector row's operand is admitted: a vector of the row's lanes' size, or a uniform scalar (which G or VB
// splats) of that type.
static bool ir_vectorize_lane_operand(const IrVectorizeWork* work, uint32_t loop, uint32_t value, uint32_t type)
{
    const uint32_t class = ir_vectorize_class(work, loop, value);
    const bool result = (class == IR_VECTORIZE_CLASS_VECTOR && work->function->type[value] == type) ||
                        (class == IR_VECTORIZE_CLASS_UNIFORM && work->function->type[value] == type && ir_vectorize_size(type) < 4);
    return result;
}

// One needed row's class from its operands' (the rules in this file's header). Returns the refusal, or
// IR_VECTORIZE_REFUSALS where it is admitted.
static uint32_t ir_vectorize_classify(IrVectorizeWork* work, uint32_t loop, IrVectorizeLoop* plan, uint32_t i)
{
    const IrFunction* f = work->function;
    const uint32_t op = f->op[i];
    const uint32_t type = f->type[i];
    const uint32_t kind = work->kinds[i];
    const uint32_t a = f->a[i];
    const uint32_t b = f->b[i];
    const uint32_t mask = op == IR_PHI ? 0u : ir_value_mask(f, i);
    const uint32_t ca = (mask & 1) != 0 ? ir_vectorize_class(work, loop, a) : IR_VECTORIZE_CLASS_UNIFORM;
    const uint32_t cb = (mask & 2) != 0 ? ir_vectorize_class(work, loop, b) : IR_VECTORIZE_CLASS_UNIFORM;
    const uint32_t cc = (mask & 4) != 0 ? ir_vectorize_class(work, loop, f->c[i]) : IR_VECTORIZE_CLASS_UNIFORM;
    const bool uniform = ca == IR_VECTORIZE_CLASS_UNIFORM && cb == IR_VECTORIZE_CLASS_UNIFORM && cc == IR_VECTORIZE_CLASS_UNIFORM;
    const uint32_t size = ir_vectorize_size(type);
    const bool integer = type >= IR_I8 && type <= IR_I64;
    uint32_t refusal = IR_VECTORIZE_REFUSALS;
    if ((kind & 7) == BUSTER_IR_VECTORIZE_KIND_FORBID || (op == IR_INTRINSIC && f->c[i] != BUSTER_IR_INTRINSIC_POPCNT))
    {
        refusal = IR_VECTORIZE_REFUSE_OPERATION;
    }
    else if (i == plan->compare || i == plan->increment || i == plan->offset_row)
    {
        ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_CONTROL, 0, 0, false);
    }
    else if (op == IR_PHI || op == IR_COPY)
    {
        // A chain block's one-input phi, or a copy: its operand's class and value.
        const uint32_t v = op == IR_PHI ? f->extra[f->a[i] + 1] : a;
        const uint32_t cv = ir_vectorize_class(work, loop, v);
        const bool admitted = cv == IR_VECTORIZE_CLASS_UNIFORM || cv == IR_VECTORIZE_CLASS_INDEX || cv == IR_VECTORIZE_CLASS_POINTER || cv == IR_VECTORIZE_CLASS_VECTOR || cv == IR_VECTORIZE_CLASS_MASK;
        refusal = admitted ? refusal : IR_VECTORIZE_REFUSE_USE;
        if (admitted)
        {
            const bool inside = ir_vectorize_inside(work, loop, v);
            ir_vectorize_set(work, i, cv, inside ? work->coefficients[v] : 0, inside ? work->sizes[v] : size, inside && work->floating[v] != 0);
            work->placed_g[i] = (uint8_t)(cv == IR_VECTORIZE_CLASS_UNIFORM && ir_vectorize_in_g(work, loop, v));
        }
    }
    else if (uniform && (kind & 7) != BUSTER_IR_VECTORIZE_KIND_FLOAT && (kind & 7) != BUSTER_IR_VECTORIZE_KIND_STORE && op != IR_INTRINSIC && type != IR_I128 && type != IR_F80)
    {
        // Every lane's value is one: cloned once. A load is uniform only in a loop without stores (checked at the
        // dependences); a row is placed in G where it is safe to speculate and its operands are there.
        ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_UNIFORM, 0, size, type == IR_F32 || type == IR_F64);
        const bool safe = (kind & BUSTER_IR_VECTORIZE_KIND_SAFE) != 0 && ((mask & 1) == 0 || ir_vectorize_in_g(work, loop, a)) && ((mask & 2) == 0 || ir_vectorize_in_g(work, loop, b)) &&
                          ((mask & 4) == 0 || ir_vectorize_in_g(work, loop, f->c[i]));
        work->placed_g[i] = (uint8_t)safe;
    }
    else if (op == IR_SEXT || op == IR_ZEXT)
    {
        const uint32_t from = f->type[a];
        if (a == plan->induction && type == IR_I64 && ((op == IR_SEXT && plan->predicate == IR_LT) || (op == IR_ZEXT && plan->predicate == IR_ULT)))
        {
            // An i32 induction's 64-bit index: it doesn't wrap where the exit test bounds it.
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_INDEX, 1, 3, false);
        }
        else if (ca == IR_VECTORIZE_CLASS_INDEX && f->type[a] == IR_I32 && type == IR_I64 && f->type[plan->induction] == IR_I32 && plan->narrows < BUSTER_IR_VECTORIZE_GUARD_LIMIT)
        {
            // A 32-bit index's 64-bit one (`rgb[3 * i + 1]` of an int i, #2808): affine where its 32-bit value doesn't wrap
            // over the vector iterations, which G tests.
            plan->narrow_rows[plan->narrows] = i;
            plan->narrows += 1;
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_INDEX, work->coefficients[a], 3, false);
        }
        else if (ca == IR_VECTORIZE_CLASS_MASK && integer)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_VECTOR, 0, size, false);
        }
        else if (ca == IR_VECTORIZE_CLASS_VECTOR && integer && ir_vectorize_size(from) < size)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_VECTOR, 0, size, false);
        }
        else
        {
            refusal = ca == IR_VECTORIZE_CLASS_INDEX || ca == IR_VECTORIZE_CLASS_POINTER ? IR_VECTORIZE_REFUSE_USE : IR_VECTORIZE_REFUSE_OPERATION;
        }
    }
    else if (op == IR_TRUNC)
    {
        const bool admitted = ca == IR_VECTORIZE_CLASS_VECTOR && integer && ir_vectorize_size(f->type[a]) > size;
        refusal = admitted ? refusal : IR_VECTORIZE_REFUSE_USE;
        if (admitted)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_VECTOR, 0, size, false);
        }
    }
    else if (op == IR_ADDRESS)
    {
        const IrAddress address = f->addresses[f->c[i]];
        const bool indexed = b != BUSTER_IR_NONE;
        if (ca == IR_VECTORIZE_CLASS_UNIFORM && indexed && cb == IR_VECTORIZE_CLASS_INDEX)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_POINTER, work->coefficients[b] * (int64_t)address.scale, 0, false);
        }
        else if (ca == IR_VECTORIZE_CLASS_POINTER && (!indexed || cb == IR_VECTORIZE_CLASS_UNIFORM))
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_POINTER, work->coefficients[a], 0, false);
        }
        else
        {
            refusal = IR_VECTORIZE_REFUSE_ADDRESS;
        }
    }
    else if (op == IR_INTTOPTR || op == IR_PTRTOINT)
    {
        const bool admitted = ca == IR_VECTORIZE_CLASS_POINTER && (type == IR_PTR || type == IR_I64);
        refusal = admitted ? refusal : IR_VECTORIZE_REFUSE_ADDRESS;
        if (admitted)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_POINTER, work->coefficients[a], 0, false);
        }
    }
    else if ((ca == IR_VECTORIZE_CLASS_INDEX || cb == IR_VECTORIZE_CLASS_INDEX || ca == IR_VECTORIZE_CLASS_POINTER || cb == IR_VECTORIZE_CLASS_POINTER) &&
             (op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_SHL))
    {
        // Affine 64-bit arithmetic of an index (or a pointer's integer) and a uniform: modular, as the address is.
        const bool left = ca == IR_VECTORIZE_CLASS_INDEX || ca == IR_VECTORIZE_CLASS_POINTER;
        const uint32_t variable = left ? ca : cb;
        const uint32_t other = left ? cb : ca;
        const int64_t coefficient = work->coefficients[left ? a : b];
        uint64_t k = 0;
        const bool constant = ir_vectorize_constant(f, left ? b : a, &k);
        int64_t result = 0;
        // A 32-bit index (#2808) is the 32-bit induction's affine function: its coefficient exact, its 64-bit extension
        // guarded against a wrap (ir_vectorize_narrow_guards).
        const bool narrow = type == IR_I32 && variable == IR_VECTORIZE_CLASS_INDEX;
        const int64_t factor = narrow ? (int64_t)(int32_t)(uint32_t)k : (int64_t)k;
        bool admitted = (type == IR_I64 || narrow) && other == IR_VECTORIZE_CLASS_UNIFORM;
        if (op == IR_ADD)
        {
            result = coefficient;
        }
        else if (op == IR_SUB)
        {
            result = left ? coefficient : (int64_t)(0 - (uint64_t)coefficient);
            admitted = admitted && (left || variable == IR_VECTORIZE_CLASS_INDEX);
        }
        else if (op == IR_MUL)
        {
            result = (int64_t)((uint64_t)coefficient * (uint64_t)factor);
            admitted = admitted && constant && variable == IR_VECTORIZE_CLASS_INDEX;
        }
        else
        {
            result = k < 64 ? (int64_t)((uint64_t)coefficient << k) : 0;
            admitted = admitted && constant && left && k < (narrow ? 31u : 64u) && variable == IR_VECTORIZE_CLASS_INDEX;
        }
        const bool bounded = result > -BUSTER_IR_VECTORIZE_NARROW_COEFFICIENT && result < BUSTER_IR_VECTORIZE_NARROW_COEFFICIENT;
        admitted = admitted && (!narrow || (bounded && coefficient > -BUSTER_IR_VECTORIZE_NARROW_COEFFICIENT && coefficient < BUSTER_IR_VECTORIZE_NARROW_COEFFICIENT));
        refusal = admitted ? refusal : IR_VECTORIZE_REFUSE_USE;
        if (admitted)
        {
            ir_vectorize_set(work, i, variable, result, 0, false);
        }
    }
    else if (op == IR_LOAD)
    {
        // Consecutive, or strided by a few elements (#2808): its pointer advances by a multiple of them.
        const int64_t element = (int64_t)(UINT64_C(1) << (size & 3));
        const int64_t step = ca == IR_VECTORIZE_CLASS_POINTER ? work->coefficients[a] : 0;
        const bool strided = step > element && step % element == 0 && step / element <= BUSTER_IR_VECTORIZE_STRIDE_LIMIT;
        const bool admitted = ca == IR_VECTORIZE_CLASS_POINTER && size < 4 && (step == element || strided);
        refusal = admitted ? refusal : IR_VECTORIZE_REFUSE_ADDRESS;
        if (admitted)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_VECTOR, 0, size, type == IR_F32 || type == IR_F64);
            plan->strided = plan->strided || strided;
        }
    }
    else if (op == IR_STORE)
    {
        const uint32_t vtype = f->type[b];
        const uint32_t vsize = ir_vectorize_size(vtype);
        const bool consecutive = ca == IR_VECTORIZE_CLASS_POINTER && vsize < 4 && work->coefficients[a] == (int64_t)(UINT64_C(1) << vsize);
        const bool value = cb == IR_VECTORIZE_CLASS_VECTOR || (cb == IR_VECTORIZE_CLASS_UNIFORM && vtype != IR_PTR);
        refusal = !consecutive ? IR_VECTORIZE_REFUSE_ADDRESS : !value ? IR_VECTORIZE_REFUSE_USE : refusal;
        if (consecutive && value)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_VECTOR, 0, vsize, vtype == IR_F32 || vtype == IR_F64);
        }
    }
    else if (op == IR_ICMP)
    {
        uint32_t x = a;
        uint32_t y = b;
        uint32_t predicate = 0;
        ir_vectorize_compared(f, i, &x, &y, &predicate);
        const uint32_t otype = f->type[x];
        const bool admitted = (otype >= IR_I8 && otype <= IR_I64) && ir_vectorize_lane_operand(work, loop, x, otype) && ir_vectorize_lane_operand(work, loop, y, otype);
        refusal = admitted ? refusal : IR_VECTORIZE_REFUSE_USE;
        if (admitted)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_MASK, 0, ir_vectorize_size(otype), false);
        }
    }
    else if (op == IR_SELECT)
    {
        const bool admitted = ca == IR_VECTORIZE_CLASS_MASK && size < 4 && type != IR_PTR && ir_vectorize_lane_operand(work, loop, b, type) && ir_vectorize_lane_operand(work, loop, f->c[i], type);
        refusal = admitted ? refusal : IR_VECTORIZE_REFUSE_USE;
        if (admitted)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_VECTOR, 0, size, type == IR_F32 || type == IR_F64);
        }
    }
    else if (op == IR_INTRINSIC)
    {
        // A population count of each lane.
        const bool admitted = ca == IR_VECTORIZE_CLASS_VECTOR && (type == IR_I32 || type == IR_I64);
        refusal = admitted ? refusal : IR_VECTORIZE_REFUSE_USE;
        if (admitted)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_VECTOR, 0, size, false);
        }
    }
    else if (op == IR_FMA)
    {
        const bool admitted = (type == IR_F32 || type == IR_F64) && ir_vectorize_lane_operand(work, loop, a, type) &&
                              ir_vectorize_lane_operand(work, loop, b, type) && ir_vectorize_lane_operand(work, loop, f->c[i], type);
        refusal = admitted ? refusal : IR_VECTORIZE_REFUSE_USE;
        if (admitted)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_VECTOR, 0, size, true);
        }
    }
    else if (op == IR_SITOFP || op == IR_UITOFP || op == IR_FPTOSI || op == IR_FPTOUI)
    {
        // Same width only: i32 with f32, i64 with f64. The float lane is the row's, and the integer operand or result
        // is that many bits.
        const bool to_float = op == IR_SITOFP || op == IR_UITOFP;
        const uint32_t source = f->type[a];
        const bool pair = to_float ? (type == IR_F32 && source == IR_I32) || (type == IR_F64 && source == IR_I64) :
                                     (type == IR_I32 && source == IR_F32) || (type == IR_I64 && source == IR_F64);
        const bool admitted = pair && ir_vectorize_lane_operand(work, loop, a, source);
        refusal = admitted ? refusal : IR_VECTORIZE_REFUSE_USE;
        if (admitted)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_VECTOR, 0, size, to_float);
        }
    }
    else if ((op >= IR_ADD && op <= IR_ASHR && op != IR_SDIV && op != IR_UDIV && op != IR_SREM && op != IR_UREM && integer) || ((kind & 7) == BUSTER_IR_VECTORIZE_KIND_FLOAT && (type == IR_F32 || type == IR_F64)))
    {
        const bool admitted = ir_vectorize_lane_operand(work, loop, a, type) && ir_vectorize_lane_operand(work, loop, b, type);
        refusal = admitted ? refusal : IR_VECTORIZE_REFUSE_USE;
        if (admitted)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_VECTOR, 0, size, type == IR_F32 || type == IR_F64);
        }
    }
    else
    {
        refusal = IR_VECTORIZE_REFUSE_OPERATION;
    }
    return refusal;
}

// Whether a reduction's addend counts a comparison (#2808): its zero extension from i1, extended again by either
// extension (of 0 or 1, both give 0 or 1), a comparison of the loop's: that comparison, or BUSTER_IR_NONE.
static uint32_t ir_vectorize_counted(const IrVectorizeWork* work, uint32_t loop, uint32_t value)
{
    const IrFunction* f = work->function;
    uint32_t at = value;
    uint32_t depth = 0;
    while (depth < 4 && at < f->instruction_count && (f->op[at] == IR_ZEXT || f->op[at] == IR_SEXT) && f->a[at] < f->instruction_count && f->type[f->a[at]] != IR_I1)
    {
        at = f->a[at];
        depth += 1;
    }
    const bool counted = at < f->instruction_count && f->op[at] == IR_ZEXT && f->a[at] < f->instruction_count && f->type[f->a[at]] == IR_I1 && f->op[f->a[at]] == IR_ICMP &&
                         ir_vectorize_inside(work, loop, f->a[at]);
    return counted ? f->a[at] : BUSTER_IR_NONE;
}

// The header phis are the induction and independent integer reductions. A min/max diamond's update was
// recorded at its join; a select or an arithmetic update is recognized here, before tracing the needed operands.
static uint32_t ir_vectorize_phis(IrVectorizeWork* work, uint32_t loop, IrVectorizeLoop* plan)
{
    const IrFunction* f = work->function;
    uint32_t refusal = IR_VECTORIZE_REFUSALS;
    for (uint32_t i = f->block_offsets[plan->header]; refusal == IR_VECTORIZE_REFUSALS && f->op[i] == IR_PHI; i += 1)
    {
        if (i == plan->induction)
        {
            ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_INDEX, 1, ir_vectorize_size(f->type[i]), false);
        }
        else
        {
            const uint32_t update = ir_vectorize_incoming(f, i, plan->latch);
            const uint32_t op = update < f->instruction_count ? f->op[update] : IR_INVALID;
            bool admitted = f->b[i] == 2 && (f->type[i] == IR_I32 || f->type[i] == IR_I64) && ir_vectorize_inside(work, loop, update) && f->type[update] == f->type[i];
            if (admitted && work->classes[update] == IR_VECTORIZE_CLASS_UPDATE)
            {
                admitted = work->reduction[work->reduction_index[update]].phi == i;
            }
            else if (admitted && op == IR_SELECT)
            {
                admitted = ir_vectorize_minmax(work, plan, update, f->a[update], f->b[update], f->c[update], BUSTER_IR_NONE, NULL) &&
                           work->reduction[work->reduction_index[update]].phi == i;
            }
            else if (admitted)
            {
                static const uint8_t operations[IR_OP_COUNT] = { [IR_ADD] = BUSTER_IR_REDUCE_ADD, [IR_MUL] = BUSTER_IR_REDUCE_MUL, [IR_AND] = BUSTER_IR_REDUCE_AND, [IR_OR] = BUSTER_IR_REDUCE_OR, [IR_XOR] = BUSTER_IR_REDUCE_XOR };
                const uint32_t other = f->a[update] == i ? f->b[update] : f->a[update];
                const uint32_t compare = op == IR_ADD ? ir_vectorize_counted(work, loop, other) : BUSTER_IR_NONE;
                const uint32_t operation = compare != BUSTER_IR_NONE ? BUSTER_IR_VECTORIZE_COUNT : operations[op];
                const IrVectorizeReduction reduction = { .phi = i, .lane = compare != BUSTER_IR_NONE ? compare : other, .operation = operation };
                admitted = operation != 0 && (f->a[update] == i) != (f->b[update] == i) && ir_vectorize_record_reduction(work, plan, update, reduction);
            }
            refusal = admitted ? refusal : IR_VECTORIZE_REFUSE_PHI;
            if (admitted)
            {
                ir_vectorize_set(work, i, IR_VECTORIZE_CLASS_REDUCTION, 0, ir_vectorize_size(f->type[i]), false);
            }
        }
    }
    return refusal;
}

// Every needed row's operands are checked where they are read: a reduction's phi read anywhere but its update, or an
// update read in the loop, refuses. Classifies the rows in order.
static uint32_t ir_vectorize_rows(IrVectorizeWork* work, uint32_t loop, IrVectorizeLoop* plan)
{
    const IrFunction* f = work->function;
    uint32_t refusal = IR_VECTORIZE_REFUSALS;
    for (uint32_t k = 0; refusal == IR_VECTORIZE_REFUSALS && k < plan->row_count; k += 1)
    {
        const uint32_t i = work->rows[k];
        if (work->needed[i] == 0)
        {
            work->classes[i] = IR_VECTORIZE_CLASS_DEAD;
            ir_vectorize_touch(work, i);
        }
        else if (work->classes[i] == IR_VECTORIZE_CLASS_UPDATE)
        {
            const IrVectorizeReduction reduction = work->reduction[work->reduction_index[i]];
            const bool counted = reduction.operation == BUSTER_IR_VECTORIZE_COUNT;
            const bool admitted = counted ? ir_vectorize_class(work, loop, reduction.lane) == IR_VECTORIZE_CLASS_MASK : ir_vectorize_lane_operand(work, loop, reduction.lane, f->type[i]);
            refusal = admitted ? refusal : IR_VECTORIZE_REFUSE_USE;
        }
        else
        {
            const uint32_t mask = f->op[i] == IR_PHI ? 1u : ir_value_mask3(f, i);
            const uint32_t operands[3] = { f->op[i] == IR_PHI ? f->extra[f->a[i] + 1] : f->a[i], f->b[i], ir_value_third(f, i) };
            for (uint32_t k2 = 0; k2 < 3; k2 += 1)
            {
                const uint32_t v = operands[k2];
                if (((mask >> k2) & 1) != 0 && ir_vectorize_inside(work, loop, v) &&
                    (work->classes[v] == IR_VECTORIZE_CLASS_REDUCTION || work->classes[v] == IR_VECTORIZE_CLASS_UPDATE))
                {
                    refusal = IR_VECTORIZE_REFUSE_USE;
                }
            }
            if (refusal == IR_VECTORIZE_REFUSALS)
            {
                refusal = ir_vectorize_classify(work, loop, plan, i);
            }
        }
    }
    // The exit test's bound must be available in G; the induction's only other readers are indices (classified above:
    // any other use refused); each reduction's update must have been found.
    const uint32_t bound_class = ir_vectorize_class(work, loop, plan->bound);
    refusal = refusal == IR_VECTORIZE_REFUSALS && (bound_class != IR_VECTORIZE_CLASS_UNIFORM || !ir_vectorize_in_g(work, loop, plan->bound)) ? IR_VECTORIZE_REFUSE_INDUCTION : refusal;
    for (uint32_t i = f->block_offsets[plan->header]; refusal == IR_VECTORIZE_REFUSALS && f->op[i] == IR_PHI; i += 1)
    {
        const uint32_t back = ir_vectorize_incoming(f, i, plan->latch);
        refusal = work->classes[i] == IR_VECTORIZE_CLASS_REDUCTION && work->classes[back] != IR_VECTORIZE_CLASS_UPDATE ? IR_VECTORIZE_REFUSE_USE : refusal;
    }
    // The header's other needed rows: only the exit test.
    for (uint32_t k = 0; refusal == IR_VECTORIZE_REFUSALS && k < plan->header_rows; k += 1)
    {
        const uint32_t i = work->rows[k];
        refusal = work->needed[i] != 0 && i != plan->compare && i != plan->offset_row && work->classes[i] != IR_VECTORIZE_CLASS_UNIFORM ? IR_VECTORIZE_REFUSE_INDUCTION : refusal;
    }
    return refusal;
}

// A vector load's elements per iteration (#2808): 1 where it is consecutive, else its stride's.
static uint32_t ir_vectorize_stride(const IrVectorizeWork* work, uint32_t i)
{
    const IrFunction* f = work->function;
    const bool load = f->op[i] == IR_LOAD && work->classes[i] == IR_VECTORIZE_CLASS_VECTOR;
    const uint32_t result = load ? (uint32_t)(work->coefficients[f->a[i]] >> work->sizes[i]) : 1;
    return result;
}

// A strided load's de-interleave at some lanes (#2808): its loads' width, how many, and the bytes from lane 0's element's
// first to the last lane's element's last, which they cover and never pass; and whether its one load is masked.
typedef struct IrVectorizeDeinterleave IrVectorizeDeinterleave;
struct IrVectorizeDeinterleave
{
    uint32_t width;
    uint32_t loads;
    uint32_t needed;
    bool masked;
};

// Row i's de-interleave at `lanes` (#2808): of 64-byte loads where the target permutes and moves them and the lanes are
// narrower (one load masked to the needed bytes where they are fewer), else of the lanes' own width; as many loads as
// the needed bytes take, the last ending at their end. False where the target has neither.
static bool ir_vectorize_deinterleave_shape(const IrVectorizeWork* work, uint32_t lanes, uint32_t i, IrVectorizeDeinterleave* shape)
{
    const IrVectorizeTarget* t = work->target;
    const uint32_t size = work->sizes[i] & 3;
    const uint32_t k = ir_vectorize_stride(work, i);
    const uint32_t width = lanes << size;
    const uint32_t needed = ((lanes - 1) * k + 1) << size;
    const bool partial = needed < 64;
    const bool mask = partial && ir_vectorize_has(t->masked[size], 64) && ((t->mask >> __builtin_ctz(64u >> size)) & 1) != 0;
    const bool wide = width < 64 && ir_vectorize_has(t->moves, 64) && ir_vectorize_has(t->permute2[size], 64) && (!partial || mask);
    const bool narrow = ir_vectorize_has(t->permute2[size], width);
    shape->width = wide ? 64 : width;
    shape->loads = (needed + shape->width - 1) / shape->width;
    shape->needed = needed;
    shape->masked = wide && partial;
    return wide || narrow;
}

// Whether every vector row's operation is the target's at `lanes` lanes per iteration.
static bool ir_vectorize_legal(const IrVectorizeWork* work, const IrVectorizeLoop* plan, uint32_t lanes)
{
    const IrFunction* f = work->function;
    const IrVectorizeTarget* t = work->target;
    bool legal = true;
    for (uint32_t k = 0; legal && k < plan->row_count; k += 1)
    {
        const uint32_t i = work->rows[k];
        const uint32_t class = work->classes[i];
        const uint32_t op = f->op[i];
        const uint32_t size = work->sizes[i];
        const uint32_t bytes = lanes << size;
        const uint32_t lane = ir_vectorize_lane(size, work->floating[i] != 0);
        if (class == IR_VECTORIZE_CLASS_UPDATE && work->reduction[work->reduction_index[i]].operation == BUSTER_IR_VECTORIZE_COUNT)
        {
            // A count's accumulator is a scalar: the mask's bits in a general register, counted.
            legal = ((t->count >> __builtin_ctz(lanes)) & 1) != 0;
        }
        else if (class == IR_VECTORIZE_CLASS_UPDATE)
        {
            const uint32_t operation = work->reduction[work->reduction_index[i]].operation;
            legal = bytes >= 16 && bytes <= 64 && ir_vectorize_has(t->moves, bytes) && ir_vectorize_has(t->splat[lane], bytes) && ir_vectorize_has(t->reduce[size][operation], bytes);
            legal = legal && (operation < BUSTER_IR_REDUCE_ADD || ir_vectorize_has(t->binary[op][lane], bytes));
        }
        else if (class == IR_VECTORIZE_CLASS_VECTOR)
        {
            legal = bytes >= 16 && bytes <= 64 && ir_vectorize_has(t->moves, bytes);
            if (op == IR_LOAD && ir_vectorize_stride(work, i) != 1)
            {
                // A strided load's de-interleave: its loads, and permutes of two tables of its lanes (#2808).
                IrVectorizeDeinterleave shape = { 0 };
                legal = legal && ir_vectorize_deinterleave_shape(work, lanes, i, &shape);
            }
            else if (op == IR_LOAD || op == IR_STORE || op == IR_COPY || op == IR_PHI)
            {
            }
            else if (op == IR_SELECT)
            {
                legal = legal && ir_vectorize_has(t->select[lane], bytes);
            }
            else if ((op == IR_ZEXT || op == IR_SEXT) && work->classes[f->a[i]] == IR_VECTORIZE_CLASS_MASK)
            {
                legal = legal && ir_vectorize_has(t->select[lane], bytes) && ir_vectorize_has(t->splat[lane], bytes);
            }
            else if (op == IR_ZEXT || op == IR_SEXT)
            {
                const uint32_t from = ir_vectorize_size(f->type[f->a[i]]);
                legal = legal && ir_vectorize_has(t->extend[from][size][op == IR_SEXT ? 1 : 0], bytes);
            }
            else if (op == IR_TRUNC)
            {
                const uint32_t from = ir_vectorize_size(f->type[f->a[i]]);
                legal = legal && ir_vectorize_has(t->truncate[from][size], lanes << from);
            }
            else if (op == IR_INTRINSIC)
            {
                legal = legal && ir_vectorize_has(t->popcount[size], bytes);
            }
            else if (op == IR_FMA || op == IR_SITOFP || op == IR_UITOFP || op == IR_FPTOSI || op == IR_FPTOUI)
            {
                const uint32_t float_lane = ir_vectorize_lane(size, true);
                legal = legal && ir_vectorize_has(t->binary[op][float_lane], bytes);
            }
            else
            {
                legal = legal && ir_vectorize_has(t->binary[op][lane], bytes);
            }
            // Each uniform operand is splatted at the row's lanes.
            const uint32_t mask = ir_value_mask(f, i);
            const uint32_t operands[3] = { f->a[i], f->b[i], f->c[i] };
            for (uint32_t k2 = 0; legal && k2 < 3; k2 += 1)
            {
                const uint32_t v = operands[k2];
                const bool pointer = op == IR_LOAD || (op == IR_STORE && k2 == 0);
                const bool uniform = ((mask >> k2) & 1) != 0 && !pointer && work->classes[v] != IR_VECTORIZE_CLASS_MASK && work->classes[v] != IR_VECTORIZE_CLASS_VECTOR &&
                                     work->classes[v] != IR_VECTORIZE_CLASS_REDUCTION && !(op == IR_SELECT && k2 == 0);
                if (uniform)
                {
                    const uint32_t vsize = ir_vectorize_size(f->type[v]);
                    const uint32_t vlane = ir_vectorize_lane(vsize, f->type[v] == IR_F32 || f->type[v] == IR_F64);
                    legal = vsize < 4 && ir_vectorize_has(t->splat[vlane], lanes << vsize);
                }
            }
        }
        else if (class == IR_VECTORIZE_CLASS_MASK)
        {
            legal = bytes >= 16 && bytes <= 64 && ir_vectorize_has(t->compare[lane], bytes);
            uint32_t x = 0;
            uint32_t y = 0;
            uint32_t predicate = 0;
            ir_vectorize_compared(f, i, &x, &y, &predicate);
            for (uint32_t k2 = 0; legal && k2 < 2; k2 += 1)
            {
                const uint32_t v = k2 == 0 ? x : y;
                legal = work->classes[v] == IR_VECTORIZE_CLASS_VECTOR || ir_vectorize_has(t->splat[lane], bytes);
            }
        }
    }
    return legal;
}

// Whether row i is a count's update (#2808), whose accumulator is a scalar: no vector of its own.
static bool ir_vectorize_scalar_update(const IrVectorizeWork* work, uint32_t i)
{
    const bool result = work->classes[i] == IR_VECTORIZE_CLASS_UPDATE && work->reduction[work->reduction_index[i]].operation == BUSTER_IR_VECTORIZE_COUNT;
    return result;
}

// The lanes per iteration: the widest width whose lanes give every vector row at least 16 bytes and at most the width,
// and at which the target has them all. 0 where there is none.
static uint32_t ir_vectorize_width(const IrVectorizeWork* work, const IrVectorizeLoop* plan)
{
    const IrFunction* f = work->function;
    uint32_t smallest = 4;
    uint32_t largest = 0;
    for (uint32_t k = 0; k < plan->row_count; k += 1)
    {
        const uint32_t i = work->rows[k];
        const uint32_t class = work->classes[i];
        if ((class == IR_VECTORIZE_CLASS_VECTOR || class == IR_VECTORIZE_CLASS_UPDATE || class == IR_VECTORIZE_CLASS_MASK) && !ir_vectorize_scalar_update(work, i))
        {
            smallest = work->sizes[i] < smallest ? work->sizes[i] : smallest;
            largest = work->sizes[i] > largest ? work->sizes[i] : largest;
            // An extension's or a truncation's source too.
            const bool convert = (f->op[i] == IR_ZEXT || f->op[i] == IR_SEXT || f->op[i] == IR_TRUNC) && work->classes[f->a[i]] == IR_VECTORIZE_CLASS_VECTOR;
            const uint32_t from = convert ? work->sizes[f->a[i]] : work->sizes[i];
            smallest = from < smallest ? from : smallest;
            largest = from > largest ? from : largest;
        }
    }
    uint32_t lanes = 0;
    for (uint32_t w = BUSTER_IR_VECTORIZE_WIDTHS; lanes == 0 && w > 0 && smallest <= largest; w -= 1)
    {
        const uint32_t candidate = (16u << (w - 1)) >> largest;
        const bool fits = (candidate << smallest) >= 16 && candidate >= 2;
        lanes = fits && ir_vectorize_legal(work, plan, candidate) ? candidate : 0;
    }
    return lanes;
}

// Whether the loop's tail can be one masked vector iteration at `lanes` (#2807): the mask of the lanes from a general
// register (IrVectorizeTarget.mask), the vector rows' integer arithmetic alone (a floating row would compute the inactive
// lanes too, raising flags the scalar loop never raises), each vector load and store with its lanes' masked move at its
// width (IrVectorizeTarget.masked), and each reduction's accumulator kept in the inactive lanes by a select.
static bool ir_vectorize_masked_legal(const IrVectorizeWork* work, const IrVectorizeLoop* plan, uint32_t lanes)
{
    const IrFunction* f = work->function;
    const IrVectorizeTarget* t = work->target;
    bool legal = ((t->mask >> __builtin_ctz(lanes)) & 1) != 0;
    for (uint32_t k = 0; legal && k < plan->row_count; k += 1)
    {
        const uint32_t i = work->rows[k];
        const uint32_t class = work->classes[i];
        const uint32_t op = f->op[i];
        const bool vector = class == IR_VECTORIZE_CLASS_VECTOR || class == IR_VECTORIZE_CLASS_UPDATE;
        legal = !vector || (work->kinds[i] & 7) != BUSTER_IR_VECTORIZE_KIND_FLOAT;
        legal = legal && (!vector || (op != IR_LOAD && op != IR_STORE) || ir_vectorize_has(t->masked[work->sizes[i]], lanes << work->sizes[i]));
    }
    for (uint32_t i = f->block_offsets[plan->header]; legal && f->op[i] == IR_PHI; i += 1)
    {
        const uint32_t size = work->sizes[i];
        const bool vector = work->classes[i] == IR_VECTORIZE_CLASS_REDUCTION && !ir_vectorize_scalar_update(work, ir_vectorize_incoming(f, i, plan->latch));
        legal = !vector || ir_vectorize_has(t->select[ir_vectorize_lane(size, false)], lanes << size);
    }
    // A strided load's de-interleave has no masked form here (#2808).
    legal = legal && !plan->strided;
    return legal;
}

// A pointer's base: past its conversions, the pointer an address row of the loop's chain offsets (by the induction's
// index or a uniform), and a uniform address row's own base where it has no index; the uniform value the chain
// starts from.
static uint32_t ir_vectorize_base(const IrVectorizeWork* work, uint32_t loop, uint32_t value)
{
    const IrFunction* f = work->function;
    uint32_t at = ir_vectorize_root(f, value);
    uint32_t depth = 0;
    bool more = true;
    while (more && depth < BUSTER_IR_VECTORIZE_DEPTH)
    {
        const uint32_t class = ir_vectorize_class(work, loop, at);
        const bool chain = class == IR_VECTORIZE_CLASS_POINTER || class == IR_VECTORIZE_CLASS_INDEX;
        const bool address = f->op[at] == IR_ADDRESS && (chain || f->b[at] == BUSTER_IR_NONE);
        const bool offset = chain && (f->op[at] == IR_ADD || f->op[at] == IR_SUB) && ir_vectorize_class(work, loop, f->a[at]) == IR_VECTORIZE_CLASS_POINTER;
        more = address || offset;
        at = more ? ir_vectorize_root(f, f->a[at]) : at;
        depth += 1;
    }
    return at;
}

// The dependences: for each store and each other access whose address isn't the store's own, a run-time test of their
// ranges (at most BUSTER_IR_VECTORIZE_GUARD_LIMIT), unless both are of one base, which refuses: two addresses of one
// array that aren't one address are a dependence carried across iterations (a prefix sum's a[i] and a[i - 1]), which
// the test would find at run time on every call. A uniform load in a loop that stores refuses.
static uint32_t ir_vectorize_dependences(IrVectorizeWork* work, uint32_t loop, IrVectorizeLoop* plan)
{
    const IrFunction* f = work->function;
    uint32_t refusal = IR_VECTORIZE_REFUSALS;
    plan->guards = 0;
    plan->stores = false;
    bool uniform_loads = false;
    for (uint32_t k = 0; k < plan->row_count; k += 1)
    {
        const uint32_t i = work->rows[k];
        plan->stores = plan->stores || (f->op[i] == IR_STORE && work->classes[i] == IR_VECTORIZE_CLASS_VECTOR);
        uniform_loads = uniform_loads || (f->op[i] == IR_LOAD && work->classes[i] == IR_VECTORIZE_CLASS_UNIFORM);
    }
    refusal = plan->stores && uniform_loads ? IR_VECTORIZE_REFUSE_DEPENDENCE : refusal;
    for (uint32_t k = 0; refusal == IR_VECTORIZE_REFUSALS && k < plan->row_count; k += 1)
    {
        const uint32_t s = work->rows[k];
        if (f->op[s] == IR_STORE && work->classes[s] == IR_VECTORIZE_CLASS_VECTOR)
        {
            for (uint32_t k2 = 0; refusal == IR_VECTORIZE_REFUSALS && k2 < plan->row_count; k2 += 1)
            {
                const uint32_t m = work->rows[k2];
                const bool access = (f->op[m] == IR_LOAD || f->op[m] == IR_STORE) && work->classes[m] == IR_VECTORIZE_CLASS_VECTOR && m != s;
                // A pair of stores is tested once, from the first.
                const bool counted = access && !(f->op[m] == IR_STORE && k2 < k);
                if (counted)
                {
                    const uint32_t ssize = work->sizes[s];
                    const uint32_t msize = work->sizes[m];
                    const bool same = ir_vectorize_same(f, f->a[s], f->a[m], BUSTER_IR_VECTORIZE_DEPTH);
                    const bool shifted = !same && ir_vectorize_base(work, loop, f->a[s]) == ir_vectorize_base(work, loop, f->a[m]);
                    if (same)
                    {
                        refusal = ssize != msize ? IR_VECTORIZE_REFUSE_DEPENDENCE : refusal;
                    }
                    else if (shifted || plan->guards == BUSTER_IR_VECTORIZE_GUARD_LIMIT)
                    {
                        refusal = IR_VECTORIZE_REFUSE_DEPENDENCE;
                    }
                    else
                    {
                        plan->guard_pairs[plan->guards * 2] = s;
                        plan->guard_pairs[plan->guards * 2 + 1] = m;
                        plan->guards += 1;
                    }
                }
            }
        }
    }
    // A guard's pointers are computed in G at the start, and a 32-bit index's chain (#2808) at the start and at the last
    // vector iteration: each chain's uniforms must be there.
    for (uint32_t g = 0; refusal == IR_VECTORIZE_REFUSALS && g < plan->guards * 2 + plan->narrows; g += 1)
    {
        const uint32_t access = g < plan->guards * 2 ? plan->guard_pairs[g] : plan->narrow_rows[g - plan->guards * 2];
        uint32_t stack[BUSTER_IR_VECTORIZE_DEPTH * 3] = { 0 };
        uint32_t depth = 0;
        stack[depth] = f->a[access];
        depth += 1;
        while (refusal == IR_VECTORIZE_REFUSALS && depth != 0)
        {
            depth -= 1;
            const uint32_t v = stack[depth];
            const uint32_t class = ir_vectorize_class(work, loop, v);
            if (class == IR_VECTORIZE_CLASS_UNIFORM)
            {
                refusal = ir_vectorize_in_g(work, loop, v) ? refusal : IR_VECTORIZE_REFUSE_DEPENDENCE;
            }
            else if (class == IR_VECTORIZE_CLASS_INDEX || class == IR_VECTORIZE_CLASS_POINTER)
            {
                // The induction's start is the plan's; a chain block's phi has one input.
                const bool induction = v == plan->induction;
                const uint32_t mask = induction ? 0u : f->op[v] == IR_PHI ? 1u : ir_value_mask(f, v);
                const uint32_t operands[3] = { f->op[v] == IR_PHI ? f->extra[f->a[v] + 1] : f->a[v], f->b[v], f->c[v] };
                for (uint32_t k2 = 0; k2 < 3; k2 += 1)
                {
                    if (((mask >> k2) & 1) != 0)
                    {
                        refusal = depth == BUSTER_ARRAY_LENGTH(stack) ? IR_VECTORIZE_REFUSE_DEPENDENCE : refusal;
                        stack[depth < BUSTER_ARRAY_LENGTH(stack) ? depth : 0] = operands[k2];
                        depth += depth < BUSTER_ARRAY_LENGTH(stack) ? 1 : 0;
                    }
                }
            }
            else
            {
                refusal = IR_VECTORIZE_REFUSE_DEPENDENCE;
            }
        }
    }
    return refusal;
}

// The plan for one innermost loop: its refusal, or IR_VECTORIZE_REFUSALS and a complete plan.
static uint32_t ir_vectorize_plan(IrVectorizeWork* work, uint32_t loop, IrVectorizeLoop* plan)
{
    const IrFunction* f = work->function;
    uint32_t refusal = ir_vectorize_shape(work, loop, plan) ? IR_VECTORIZE_REFUSALS : IR_VECTORIZE_REFUSE_SHAPE;
    // The data-parallel admission scan of the loop's blocks: a refusing kind anywhere refuses.
    for (uint32_t block = plan->header; refusal == IR_VECTORIZE_REFUSALS && block != BUSTER_IR_NONE;)
    {
        const uint32_t first = f->block_offsets[block];
        const uint32_t end = f->block_offsets[(uint64_t)block + 1];
        refusal = ir_vectorize_forbidden(work->kinds, first, end) ? IR_VECTORIZE_REFUSE_OPERATION : refusal;
        const uint32_t next = f->op[end - 1] == IR_BR ? f->a[end - 1] : ir_vectorize_block_inside(work, loop, f->b[end - 1]) ? f->b[end - 1] : f->c[end - 1];
        block = next == plan->header ? BUSTER_IR_NONE : next;
    }
    refusal = refusal == IR_VECTORIZE_REFUSALS && !ir_vectorize_induction(work, loop, plan) ? IR_VECTORIZE_REFUSE_INDUCTION : refusal;
    if (refusal == IR_VECTORIZE_REFUSALS)
    {
        refusal = ir_vectorize_phis(work, loop, plan);
    }
    if (refusal == IR_VECTORIZE_REFUSALS)
    {
        ir_vectorize_needed(work, loop, plan);
    }
    refusal = refusal == IR_VECTORIZE_REFUSALS ? ir_vectorize_rows(work, loop, plan) : refusal;
    refusal = refusal == IR_VECTORIZE_REFUSALS ? ir_vectorize_dependences(work, loop, plan) : refusal;
    if (refusal == IR_VECTORIZE_REFUSALS)
    {
        plan->lanes = ir_vectorize_width(work, plan);
        refusal = plan->lanes == 0 ? IR_VECTORIZE_REFUSE_WIDTH : refusal;
    }
    if (refusal == IR_VECTORIZE_REFUSALS)
    {
        uint32_t largest = 0;
        for (uint32_t k = 0; k < plan->row_count; k += 1)
        {
            const uint32_t i = work->rows[k];
            const uint32_t class = work->classes[i];
            largest = (class == IR_VECTORIZE_CLASS_VECTOR || class == IR_VECTORIZE_CLASS_UPDATE) && !ir_vectorize_scalar_update(work, i) && work->sizes[i] > largest ? work->sizes[i] : largest;
        }
        const uint32_t bytes = plan->lanes << largest;
        plan->width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
        plan->masked = ir_vectorize_masked_legal(work, plan, plan->lanes);
    }
    return refusal;
}

// ---- Emission (IrVectorizeBuild): rows and blocks appended past the original ones, counted, then written. ----

static uint32_t ir_vectorize_row(IrVectorizeBuild* build, uint32_t op, uint32_t type, uint32_t flags, uint32_t alignment, uint32_t a, uint32_t b, uint32_t c, uint32_t origin)
{
    IrFunction* v = build->view;
    const uint32_t id = build->rows;
    if (!build->counting)
    {
        v->op[id] = (uint8_t)op;
        v->type[id] = (uint8_t)type;
        v->flags[id] = (uint8_t)flags;
        v->alignment_log2[id] = (uint8_t)alignment;
        v->a[id] = a;
        v->b[id] = b;
        v->c[id] = c;
        if (v->debug != NULL)
        {
            v->debug->origins[id] = origin;
        }
    }
    build->rows += 1;
    return id;
}

static uint32_t ir_vectorize_block(IrVectorizeBuild* build)
{
    const uint32_t id = build->blocks;
    if (!build->counting)
    {
        build->view->block_offsets[id] = build->rows;
    }
    build->blocks += 1;
    return id;
}

// A new constant's row: `bits` at `type`, appended to the constant table.
static uint32_t ir_vectorize_const(IrVectorizeBuild* build, uint32_t type, uint64_t bits)
{
    const uint32_t index = build->constants;
    if (!build->counting)
    {
        build->view->constants[index] = (IrConstant){ .low = bits & ir_vectorize_type_mask(type), .high = 0 };
    }
    build->constants += 1;
    return ir_vectorize_row(build, IR_CONST, type, 0, 0, index, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
}

// A phi's incoming pairs, `count` of them, appended to the extra array: returns their offset for the phi's `a`.
static uint32_t ir_vectorize_pairs(IrVectorizeBuild* build, const uint32_t* pairs, uint32_t count)
{
    const uint32_t offset = build->extras;
    if (!build->counting)
    {
        memcpy(build->view->extra + offset, pairs, (uint64_t)count * 2 * sizeof(uint32_t));
    }
    build->extras += count * 2;
    return offset;
}

// A vector constant's row (#2808): `count` lanes of 1 << size bytes, lane l `lanes[l]`, at `type`, appended to the vector
// constant table.
static uint32_t ir_vectorize_vector_constant(IrVectorizeBuild* build, uint32_t type, uint32_t size, const uint32_t* lanes, uint32_t count)
{
    const uint32_t index = build->vector_constants;
    if (!build->counting)
    {
        IrSimdValue value = { 0 };
        uint8_t bytes[64] = { 0 };
        for (uint32_t l = 0; l < count; l += 1)
        {
            const uint64_t bits = lanes[l];
            memcpy(bytes + ((uint64_t)l << size), &bits, (uint64_t)1 << size);
        }
        memcpy(value.words, bytes, sizeof(bytes));
        build->view->vector_constants[index] = value;
    }
    build->vector_constants += 1;
    return ir_vectorize_row(build, IR_VCONST, type, 0, 0, index, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
}

// `base` plus `offset` bytes, an address row of its own entry in the address table (#2808).
static uint32_t ir_vectorize_offset(IrVectorizeBuild* build, uint32_t base, int64_t offset)
{
    const uint32_t index = build->addresses;
    if (!build->counting)
    {
        build->view->addresses[index] = (IrAddress){ .offset = offset, .scale = 1 };
    }
    build->addresses += 1;
    return ir_vectorize_row(build, IR_ADDRESS, IR_PTR, 0, 0, base, BUSTER_IR_NONE, index, BUSTER_IR_NONE);
}

static uint32_t ir_vectorize_origin(const IrFunction* f, uint32_t i)
{
    const uint32_t result = f->debug != NULL ? f->debug->origins[i] : BUSTER_IR_NONE;
    return result;
}

// A value as a clone reads it: outside the loop itself; in the loop, its clone in VB (or G), or, where `chain` is a G
// chain's clones (IrVectorizeWork.starts, .lasts), an index's or a pointer's clone there, at that chain's induction.
static uint32_t ir_vectorize_operand(const IrVectorizeWork* work, uint32_t loop, uint32_t value, const uint32_t* chain)
{
    uint32_t result = value;
    if (ir_vectorize_inside(work, loop, value))
    {
        const uint32_t class = work->classes[value];
        const bool affine = class == IR_VECTORIZE_CLASS_INDEX || class == IR_VECTORIZE_CLASS_POINTER;
        result = chain != NULL && affine ? chain[value] : work->values[value];
    }
    return result;
}

// A scalar clone of row i, its operands read as ir_vectorize_operand gives them.
static uint32_t ir_vectorize_clone(IrVectorizeWork* work, IrVectorizeBuild* build, uint32_t loop, uint32_t i, const uint32_t* chain)
{
    const IrFunction* f = work->function;
    const uint32_t mask = ir_value_mask(f, i);
    const uint32_t a = (mask & 1) != 0 ? ir_vectorize_operand(work, loop, f->a[i], chain) : f->a[i];
    const uint32_t b = (mask & 2) != 0 ? ir_vectorize_operand(work, loop, f->b[i], chain) : f->b[i];
    const uint32_t c = (mask & 4) != 0 ? ir_vectorize_operand(work, loop, f->c[i], chain) : f->c[i];
    return ir_vectorize_row(build, f->op[i], f->type[i], f->flags[i], f->alignment_log2[i], a, b, c, ir_vectorize_origin(f, i));
}

// The G clones of an index's or a pointer's chain into `memo` (IrVectorizeWork.starts or .lasts), the induction at
// `induction`, each once and after its operands' (an operand's before the next one's, in operand order), by an explicit
// stack (the call graph's no-recursion rule) of values and whether their operands were pushed; a value is pushed at most
// once per operand that reads it.
static void ir_vectorize_start_chain(IrVectorizeWork* work, IrVectorizeBuild* build, uint32_t loop, const IrVectorizeLoop* plan, uint32_t value, uint32_t* memo, uint32_t induction)
{
    const IrFunction* f = work->function;
    uint32_t count = 1;
    work->chain[0] = value;
    work->chain_expanded[0] = 0;
    while (count != 0)
    {
        const uint32_t v = work->chain[count - 1];
        const bool expanded = work->chain_expanded[count - 1] != 0;
        const uint32_t class = ir_vectorize_class(work, loop, v);
        const bool pending = (class == IR_VECTORIZE_CLASS_INDEX || class == IR_VECTORIZE_CLASS_POINTER) && ir_vectorize_inside(work, loop, v) && memo[v] == BUSTER_IR_NONE;
        const bool copy = f->op[v] == IR_COPY || f->op[v] == IR_PHI;
        if (!pending)
        {
            count -= 1;
        }
        else if (v == plan->induction)
        {
            memo[v] = induction;
            count -= 1;
        }
        else if (expanded)
        {
            // Every operand's start is made: this one's, a copy's its source's.
            const uint32_t source = f->op[v] == IR_PHI ? f->extra[f->a[v] + 1] : f->a[v];
            memo[v] = copy ? ir_vectorize_operand(work, loop, source, memo) : ir_vectorize_clone(work, build, loop, v, memo);
            count -= 1;
        }
        else
        {
            work->chain_expanded[count - 1] = 1;
            const uint32_t mask = copy ? 1u : ir_value_mask(f, v);
            const uint32_t operands[3] = { f->op[v] == IR_PHI ? f->extra[f->a[v] + 1] : f->a[v], f->b[v], f->c[v] };
            for (uint32_t k = 3; k > 0; k -= 1)
            {
                if (((mask >> (k - 1)) & 1) != 0 && count < work->chain_capacity)
                {
                    work->chain[count] = operands[k - 1];
                    work->chain_expanded[count] = 0;
                    count += 1;
                }
            }
        }
    }
}

// A uniform value's splat at `lanes` lanes, made once (in G where the value is there, else where it is cloned).
static uint32_t ir_vectorize_splat(IrVectorizeWork* work, IrVectorizeBuild* build, uint32_t loop, uint32_t value, uint32_t lanes)
{
    const IrFunction* f = work->function;
    if (work->splats[value] == BUSTER_IR_NONE)
    {
        const uint32_t size = ir_vectorize_size(f->type[value]);
        const uint32_t scalar = ir_vectorize_operand(work, loop, value, NULL);
        work->splats[value] = ir_vectorize_row(build, IR_INTRINSIC, ir_vectorize_vector_type(lanes << size), 0, 0, scalar, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_SPLAT, BUSTER_IR_NONE);
        ir_vectorize_touch(work, value);
    }
    return work->splats[value];
}

// A vector row's operand: its vector, or its uniform's splat.
static uint32_t ir_vectorize_lanes_of(IrVectorizeWork* work, IrVectorizeBuild* build, uint32_t loop, uint32_t value, uint32_t lanes)
{
    const uint32_t class = ir_vectorize_class(work, loop, value);
    const bool vector = class == IR_VECTORIZE_CLASS_VECTOR || class == IR_VECTORIZE_CLASS_MASK || class == IR_VECTORIZE_CLASS_REDUCTION || class == IR_VECTORIZE_CLASS_UPDATE;
    const uint32_t result = vector ? work->values[value] : ir_vectorize_splat(work, build, loop, value, lanes);
    return result;
}

// A strided load's de-interleave (#2808), of k elements per iteration and L lanes of E bytes (lane l's element l * k from
// lane 0's), by loads of M elements (ir_vectorize_deinterleave_shape): the s-th s * M elements past lane 0's but the
// last, which ends at the last lane's element, so that no byte past the scalar loop's last one is read (one load alone
// is masked to them instead). Lane l's element is in the last load from (loads - 1) * M on, else in the (l * k / M)-th.
// The permutes of two tables (vpermt2b to vpermt2q, an index past M naming the second's lane) fold them in order: the
// first takes the first two loads' elements, and the s-th the s-th load's into the lanes the ones before it placed; one
// load is permuted with itself. G makes their index vectors in order, and the mask of a masked load after them: returns
// the first one's row, the next ones' following it, and the mask in *mask.
static uint32_t ir_vectorize_deinterleave_indices(IrVectorizeWork* work, IrVectorizeBuild* build, const IrVectorizeLoop* plan, uint32_t i, uint32_t* mask)
{
    const uint32_t lanes = plan->lanes;
    const uint32_t size = work->sizes[i] & 3;
    const uint32_t k = ir_vectorize_stride(work, i);
    IrVectorizeDeinterleave shape = { 0 };
    ir_vectorize_deinterleave_shape(work, lanes, i, &shape);
    const uint32_t elements = shape.width >> size;
    const uint32_t vtype = ir_vectorize_vector_type(shape.width);
    const uint32_t loads = shape.loads;
    const uint32_t last_start = loads == 1 ? 0 : (shape.needed >> size) - elements;
    const uint32_t vectors = loads == 1 ? 1 : loads - 1;
    uint32_t first = BUSTER_IR_NONE;
    for (uint32_t s = 1; s <= vectors; s += 1)
    {
        uint32_t indices[64] = { 0 };
        for (uint32_t l = 0; l < lanes; l += 1)
        {
            // The mutant takes lane 1's element in lane 0.
            const uint32_t element = (l == 0 && work->mutation == BUSTER_IR_VECTORIZE_MUTATE_STRIDE ? 1 : l) * k;
            const bool last = element >= (loads - 1) * elements;
            const uint32_t source = last ? loads - 1 : element / elements;
            const uint32_t place = last ? element - last_start : element % elements;
            const uint32_t earlier = s == 1 ? place : l;
            indices[l] = loads == 1 ? place : source == s ? elements + place : source < s ? earlier : 0;
        }
        const uint32_t row = ir_vectorize_vector_constant(build, vtype, size, indices, elements);
        first = s == 1 ? row : first;
    }
    *mask = BUSTER_IR_NONE;
    if (shape.masked)
    {
        const uint32_t mtype = ir_vectorize_mask_type(elements);
        const uint32_t bits_type = mtype == IR_M8 ? IR_I8 : mtype == IR_M16 ? IR_I16 : mtype == IR_M32 ? IR_I32 : IR_I64;
        const uint32_t count = shape.needed >> size;
        const uint32_t bits = ir_vectorize_const(build, bits_type, count >= 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1);
        *mask = ir_vectorize_row(build, IR_BITCAST, mtype, 0, 0, bits, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return first;
}

// The splats G makes before the vector loop: each uniform operand of a vector row that G has, and each mask
// extension's two constants.
static void ir_vectorize_g_splats(IrVectorizeWork* work, IrVectorizeBuild* build, uint32_t loop, const IrVectorizeLoop* plan)
{
    const IrFunction* f = work->function;
    const uint32_t lanes = plan->lanes;
    for (uint32_t k = 0; k < plan->row_count; k += 1)
    {
        const uint32_t i = work->rows[k];
        const uint32_t class = work->classes[i];
        const uint32_t op = f->op[i];
        if (class == IR_VECTORIZE_CLASS_UPDATE)
        {
            const uint32_t lane = work->reduction[work->reduction_index[i]].lane;
            if (ir_vectorize_class(work, loop, lane) == IR_VECTORIZE_CLASS_UNIFORM && ir_vectorize_in_g(work, loop, lane))
            {
                ir_vectorize_splat(work, build, loop, lane, lanes);
            }
        }
        else if (class == IR_VECTORIZE_CLASS_VECTOR || class == IR_VECTORIZE_CLASS_MASK)
        {
            const uint32_t mask = ir_value_mask(f, i);
            uint32_t operands[3] = { f->a[i], f->b[i], f->c[i] };
            uint32_t predicate = 0;
            if (op == IR_ICMP)
            {
                ir_vectorize_compared(f, i, &operands[0], &operands[1], &predicate);
            }
            for (uint32_t k2 = 0; k2 < 3; k2 += 1)
            {
                const uint32_t v = operands[k2];
                const bool pointer = op == IR_LOAD || (op == IR_STORE && k2 == 0);
                const bool used = ((mask >> k2) & 1) != 0 && !pointer && op != IR_PHI && op != IR_COPY;
                if (used && ir_vectorize_class(work, loop, v) == IR_VECTORIZE_CLASS_UNIFORM && ir_vectorize_in_g(work, loop, v))
                {
                    ir_vectorize_splat(work, build, loop, v, lanes);
                }
            }
            if (op == IR_LOAD && ir_vectorize_stride(work, i) != 1)
            {
                work->ones[i] = ir_vectorize_deinterleave_indices(work, build, plan, i, &work->zeros[i]);
            }
            if ((op == IR_ZEXT || op == IR_SEXT) && work->classes[f->a[i]] == IR_VECTORIZE_CLASS_MASK)
            {
                const uint32_t type = f->type[i];
                const uint32_t vtype = ir_vectorize_vector_type(lanes << ir_vectorize_size(type));
                const uint32_t one = ir_vectorize_const(build, type, op == IR_ZEXT ? 1 : UINT64_MAX);
                const uint32_t zero = ir_vectorize_const(build, type, 0);
                work->ones[i] = ir_vectorize_row(build, IR_INTRINSIC, vtype, 0, 0, one, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_SPLAT, BUSTER_IR_NONE);
                work->zeros[i] = ir_vectorize_row(build, IR_INTRINSIC, vtype, 0, 0, zero, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_SPLAT, BUSTER_IR_NONE);
            }
        }
    }
}

// A reduction phi's accumulator's type in the vector loop: a vector of its lanes, or a count's scalar (#2808).
static uint32_t ir_vectorize_accumulator(const IrVectorizeWork* work, const IrVectorizeLoop* plan, uint32_t phi)
{
    const IrFunction* f = work->function;
    const bool scalar = ir_vectorize_scalar_update(work, ir_vectorize_incoming(f, phi, plan->latch));
    const uint32_t result = scalar ? f->type[phi] : ir_vectorize_vector_type(plan->lanes << work->sizes[phi]);
    return result;
}

// A count's update (#2808): its accumulator plus the population count of its comparison's mask's bits, of those that
// `tail` (the masked tail's mask, of the same type) has set where it isn't BUSTER_IR_NONE.
static uint32_t ir_vectorize_count_row(IrVectorizeWork* work, IrVectorizeBuild* build, const IrVectorizeLoop* plan, uint32_t i, uint32_t tail)
{
    const IrFunction* f = work->function;
    const IrVectorizeReduction reduction = work->reduction[work->reduction_index[i]];
    const uint32_t mtype = ir_vectorize_mask_type(plan->lanes);
    const uint32_t bits_type = mtype == IR_M8 ? IR_I8 : mtype == IR_M16 ? IR_I16 : mtype == IR_M32 ? IR_I32 : IR_I64;
    uint32_t bits = ir_vectorize_row(build, IR_BITCAST, bits_type, 0, 0, work->values[reduction.lane], BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
    if (tail != BUSTER_IR_NONE)
    {
        const uint32_t active = ir_vectorize_row(build, IR_BITCAST, bits_type, 0, 0, tail, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        bits = ir_vectorize_row(build, IR_AND, bits_type, 0, 0, bits, active, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    // popcnt counts 32 or 64 bits.
    const uint32_t count_type = bits_type == IR_I64 ? IR_I64 : IR_I32;
    const uint32_t wide = bits_type == count_type ? bits : ir_vectorize_row(build, IR_ZEXT, count_type, 0, 0, bits, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
    const uint32_t counted = ir_vectorize_row(build, IR_INTRINSIC, count_type, 0, 0, wide, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_POPCNT, BUSTER_IR_NONE);
    const uint32_t type = f->type[i];
    const uint32_t resize = type == IR_I64 ? IR_ZEXT : IR_TRUNC;
    const uint32_t addend = type == count_type ? counted : ir_vectorize_row(build, resize, type, 0, 0, counted, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
    const uint32_t result = ir_vectorize_row(build, IR_ADD, type, 0, 0, work->values[reduction.phi], addend, BUSTER_IR_NONE, ir_vectorize_origin(f, i));
    return result;
}

// One vector row in VB.
static void ir_vectorize_vector_row(IrVectorizeWork* work, IrVectorizeBuild* build, uint32_t loop, const IrVectorizeLoop* plan, uint32_t i)
{
    const IrFunction* f = work->function;
    const uint32_t op = f->op[i];
    const uint32_t lanes = plan->lanes;
    const uint32_t size = work->sizes[i];
    const uint32_t vtype = ir_vectorize_vector_type(lanes << size);
    const uint32_t lane = ir_vectorize_lane(size, work->floating[i] != 0);
    const uint32_t origin = ir_vectorize_origin(f, i);
    uint32_t value = BUSTER_IR_NONE;
    if (ir_vectorize_scalar_update(work, i))
    {
        value = ir_vectorize_count_row(work, build, plan, i, BUSTER_IR_NONE);
    }
    else if (work->classes[i] == IR_VECTORIZE_CLASS_UPDATE && work->reduction[work->reduction_index[i]].operation < BUSTER_IR_REDUCE_ADD)
    {
        const IrVectorizeReduction reduction = work->reduction[work->reduction_index[i]];
        const uint32_t other = ir_vectorize_lanes_of(work, build, loop, reduction.lane, lanes);
        value = ir_vectorize_row(build, IR_INTRINSIC, vtype, size | reduction.operation << 2, 0, work->values[reduction.phi], other, BUSTER_IR_INTRINSIC_LANE_OP, origin);
    }
    else if (op == IR_PHI || op == IR_COPY)
    {
        value = work->values[op == IR_PHI ? f->extra[f->a[i] + 1] : f->a[i]];
    }
    else if (op == IR_LOAD && ir_vectorize_stride(work, i) != 1)
    {
        // A strided load (#2808): the loads and permutes of ir_vectorize_deinterleave_indices, then, of wider loads, the
        // lanes' part.
        IrVectorizeDeinterleave shape = { 0 };
        ir_vectorize_deinterleave_shape(work, lanes, i, &shape);
        const uint32_t wide = ir_vectorize_vector_type(shape.width);
        const uint32_t base = ir_vectorize_operand(work, loop, f->a[i], NULL);
        for (uint32_t s = 0; s < shape.loads; s += 1)
        {
            const int64_t offset = s + 1 < shape.loads ? (int64_t)s * shape.width : (int64_t)shape.needed - (int64_t)shape.width;
            const uint32_t pointer = s == 0 ? base : ir_vectorize_offset(build, base, offset);
            // A load past lane 0's address is as aligned as an element alone: its offset is a multiple of the element's.
            const uint32_t alignment = s == 0 || f->alignment_log2[i] < size ? f->alignment_log2[i] : size;
            // A masked load's inactive lanes, which no permute reads, take the index vector's.
            const uint32_t loaded = shape.masked ? ir_vectorize_row(build, IR_MASKED_LOAD, wide, ir_vectorize_lane(size, false), alignment, pointer, work->zeros[i], work->ones[i], origin) :
                                                   ir_vectorize_row(build, IR_LOAD, wide, 0, alignment, pointer, BUSTER_IR_NONE, BUSTER_IR_NONE, origin);
            const bool permute = s != 0 || shape.loads == 1;
            const uint32_t extra = build->extras;
            if (permute && !build->counting)
            {
                build->view->extra[extra] = loaded;
            }
            build->extras += permute ? 1 : 0;
            const uint32_t kind = ir_intrinsic_ternary_c(BUSTER_IR_INTRINSIC_PERMUTE2, extra);
            const uint32_t table = s == 0 ? loaded : value;
            const uint32_t indices = work->ones[i] + (s == 0 ? 0 : s - 1);
            value = permute ? ir_vectorize_row(build, IR_INTRINSIC, wide, size, 0, table, indices, kind, origin) : loaded;
        }
        if (shape.width != (lanes << size))
        {
            value = ir_vectorize_row(build, IR_INTRINSIC, vtype, 0, 0, value, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_EXTRACT, origin);
        }
    }
    else if (op == IR_LOAD)
    {
        value = ir_vectorize_row(build, IR_LOAD, vtype, 0, f->alignment_log2[i], ir_vectorize_operand(work, loop, f->a[i], NULL), BUSTER_IR_NONE, BUSTER_IR_NONE, origin);
    }
    else if (op == IR_STORE)
    {
        const uint32_t stored = ir_vectorize_lanes_of(work, build, loop, f->b[i], lanes);
        value = ir_vectorize_row(build, IR_STORE, IR_VOID, 0, f->alignment_log2[i], ir_vectorize_operand(work, loop, f->a[i], NULL), stored, BUSTER_IR_NONE, origin);
    }
    else if (op == IR_ICMP)
    {
        const uint32_t olane = ir_vectorize_lane(size, false);
        uint32_t left = 0;
        uint32_t right = 0;
        uint32_t predicate = 0;
        ir_vectorize_compared(f, i, &left, &right, &predicate);
        const uint32_t x = ir_vectorize_lanes_of(work, build, loop, left, lanes);
        const uint32_t y = ir_vectorize_lanes_of(work, build, loop, right, lanes);
        value = ir_vectorize_row(build, IR_VCMP, ir_vectorize_mask_type(lanes), olane, 0, x, y, predicate, origin);
    }
    else if (op == IR_SELECT)
    {
        const uint32_t x = ir_vectorize_lanes_of(work, build, loop, f->b[i], lanes);
        const uint32_t y = ir_vectorize_lanes_of(work, build, loop, f->c[i], lanes);
        value = ir_vectorize_row(build, IR_VSELECT, vtype, lane, 0, work->values[f->a[i]], x, y, origin);
    }
    else if ((op == IR_ZEXT || op == IR_SEXT) && work->classes[f->a[i]] == IR_VECTORIZE_CLASS_MASK)
    {
        value = ir_vectorize_row(build, IR_VSELECT, vtype, lane, 0, work->values[f->a[i]], work->ones[i], work->zeros[i], origin);
    }
    else if (op == IR_ZEXT || op == IR_SEXT || op == IR_TRUNC)
    {
        const uint32_t from = work->sizes[f->a[i]];
        const uint32_t kind = op == IR_TRUNC ? BUSTER_IR_INTRINSIC_TRUNCATE : BUSTER_IR_INTRINSIC_EXTEND;
        const uint32_t flags = from | size << 2 | (op == IR_SEXT ? 1u : 0u) << 4;
        value = ir_vectorize_row(build, IR_INTRINSIC, vtype, flags, 0, work->values[f->a[i]], BUSTER_IR_NONE, kind, origin);
    }
    else if (op == IR_INTRINSIC)
    {
        const uint32_t flags = size | BUSTER_IR_LANE_UNARY_POPCOUNT << 2;
        value = ir_vectorize_row(build, IR_INTRINSIC, vtype, flags, 0, work->values[f->a[i]], BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_LANE_UNARY, origin);
    }
    else if (op == IR_FMA)
    {
        const uint32_t x = ir_vectorize_lanes_of(work, build, loop, f->a[i], lanes);
        const uint32_t y = ir_vectorize_lanes_of(work, build, loop, f->b[i], lanes);
        const uint32_t z = ir_vectorize_lanes_of(work, build, loop, f->c[i], lanes);
        value = ir_vectorize_row(build, IR_FMA, vtype, lane, 0, x, y, z, origin);
    }
    else if (op == IR_SITOFP || op == IR_UITOFP || op == IR_FPTOSI || op == IR_FPTOUI)
    {
        const uint32_t float_lane = ir_vectorize_lane(size, true);
        const uint32_t x = ir_vectorize_lanes_of(work, build, loop, f->a[i], lanes);
        value = ir_vectorize_row(build, op, vtype, float_lane, 0, x, BUSTER_IR_NONE, BUSTER_IR_NONE, origin);
    }
    else
    {
        // An element-wise operation, or a reduction's update of its accumulator. The bitwise operations read no lanes.
        const bool bitwise = op == IR_AND || op == IR_OR || op == IR_XOR;
        const bool mutated = work->mutation == BUSTER_IR_VECTORIZE_MUTATE_LANES && work->floating[i] == 0;
        const uint32_t x = ir_vectorize_lanes_of(work, build, loop, f->a[i], lanes);
        const uint32_t y = ir_vectorize_lanes_of(work, build, loop, f->b[i], lanes);
        const uint32_t flags = bitwise ? 0 : mutated ? ir_vectorize_lane(size < 3 ? size + 1 : 2, false) : lane;
        value = ir_vectorize_row(build, op, vtype, flags, 0, x, y, BUSTER_IR_NONE, origin);
    }
    work->values[i] = value;
}

// The masked tail's rows of the loop (#2807): each needed row as VB has it, at the tail's induction and accumulators
// (IrVectorizeWork.values), but a vector load and store masked by `mask` (a load's inactive lanes zero), and a
// reduction's update selected by it, so that its inactive lanes keep the accumulator.
static void ir_vectorize_tail_rows(IrVectorizeWork* work, IrVectorizeBuild* build, uint32_t loop, const IrVectorizeLoop* plan, uint32_t mask)
{
    const IrFunction* f = work->function;
    const uint32_t lanes = plan->lanes;
    // The uniforms VB clones, and their splats, are VB's: the tail makes its own.
    for (uint32_t k = 0; k < plan->row_count; k += 1)
    {
        const uint32_t i = work->rows[k];
        work->splats[i] = work->classes[i] == IR_VECTORIZE_CLASS_UNIFORM && work->placed_g[i] == 0 ? BUSTER_IR_NONE : work->splats[i];
    }
    uint32_t zeros[4] = { BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE };
    for (uint32_t k = 0; k < plan->row_count; k += 1)
    {
        const uint32_t i = work->rows[k];
        const uint32_t class = work->classes[i];
        const uint32_t op = f->op[i];
        const bool copy = op == IR_COPY || op == IR_PHI;
        const bool vector = class == IR_VECTORIZE_CLASS_VECTOR || class == IR_VECTORIZE_CLASS_MASK || class == IR_VECTORIZE_CLASS_UPDATE;
        const uint32_t size = work->sizes[i];
        const uint32_t vtype = ir_vectorize_vector_type(lanes << size);
        const uint32_t lane = ir_vectorize_lane(size, false);
        if ((class == IR_VECTORIZE_CLASS_UNIFORM && work->placed_g[i] == 0) || class == IR_VECTORIZE_CLASS_INDEX || class == IR_VECTORIZE_CLASS_POINTER)
        {
            work->values[i] = copy ? ir_vectorize_operand(work, loop, op == IR_PHI ? f->extra[f->a[i] + 1] : f->a[i], NULL) : ir_vectorize_clone(work, build, loop, i, NULL);
        }
        else if (vector && op == IR_LOAD)
        {
            if (zeros[size & 3] == BUSTER_IR_NONE)
            {
                static const uint8_t integers[4] = { IR_I8, IR_I16, IR_I32, IR_I64 };
                const uint32_t zero = ir_vectorize_const(build, integers[size & 3], 0);
                zeros[size & 3] = ir_vectorize_row(build, IR_INTRINSIC, vtype, 0, 0, zero, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_SPLAT, BUSTER_IR_NONE);
            }
            const uint32_t pointer = ir_vectorize_operand(work, loop, f->a[i], NULL);
            work->values[i] = ir_vectorize_row(build, IR_MASKED_LOAD, vtype, lane, f->alignment_log2[i], pointer, mask, zeros[size & 3], ir_vectorize_origin(f, i));
        }
        else if (vector && op == IR_STORE)
        {
            const uint32_t stored = ir_vectorize_lanes_of(work, build, loop, f->b[i], lanes);
            const uint32_t pointer = ir_vectorize_operand(work, loop, f->a[i], NULL);
            work->values[i] = ir_vectorize_row(build, IR_MASKED_STORE, IR_VOID, lane, f->alignment_log2[i], pointer, stored, mask, ir_vectorize_origin(f, i));
        }
        else if (ir_vectorize_scalar_update(work, i))
        {
            work->values[i] = ir_vectorize_count_row(work, build, plan, i, mask);
        }
        else if (vector)
        {
            ir_vectorize_vector_row(work, build, loop, plan, i);
            if (class == IR_VECTORIZE_CLASS_UPDATE)
            {
                // The update's own phi: a min/max's update is a select or a join, whose operands aren't it.
                const uint32_t accumulator = work->reduction[work->reduction_index[i]].phi;
                work->values[i] = ir_vectorize_row(build, IR_VSELECT, vtype, lane, 0, mask, work->values[i], work->values[accumulator], BUSTER_IR_NONE);
            }
        }
    }
}

// The 32-bit indices' tests (#2808), in G: each one's 64-bit extension at the vector iterations' first and last
// inductions (its chain cloned at each) must be its coefficient times their distance apart. The 32-bit value is the
// exact affine one modulo 2^32, so its extension is that value less a multiple of 2^32 that can only grow (or only
// shrink) with the induction; equal at both ends, it is the same for every iteration between them, and each lane's
// index is lane 0's plus the coefficient per lane. Returns `enter` with the tests.
static uint32_t ir_vectorize_narrow_guards(IrVectorizeWork* work, IrVectorizeBuild* build, uint32_t loop, const IrVectorizeLoop* plan, uint32_t covered, uint32_t enter)
{
    const IrFunction* f = work->function;
    uint32_t result = enter;
    const uint32_t narrows = work->mutation == BUSTER_IR_VECTORIZE_MUTATE_NARROW ? 0 : plan->narrows;
    if (narrows != 0)
    {
        // covered - 1 iterations from the start to the last: covered is nonzero wherever the tests matter.
        const uint32_t one = ir_vectorize_const(build, IR_I64, 1);
        const uint32_t distance = ir_vectorize_row(build, IR_SUB, IR_I64, 0, 0, covered, one, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t narrow = ir_vectorize_row(build, IR_TRUNC, IR_I32, 0, 0, distance, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t last = ir_vectorize_row(build, IR_ADD, IR_I32, 0, 0, plan->start, narrow, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t k = 0; k < narrows; k += 1)
        {
            const uint32_t row = plan->narrow_rows[k];
            const uint32_t index = f->a[row];
            ir_vectorize_start_chain(work, build, loop, plan, index, work->starts, plan->start);
            ir_vectorize_start_chain(work, build, loop, plan, index, work->lasts, last);
            const uint32_t first_value = ir_vectorize_row(build, f->op[row], IR_I64, 0, 0, ir_vectorize_operand(work, loop, index, work->starts), BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t last_value = ir_vectorize_row(build, f->op[row], IR_I64, 0, 0, ir_vectorize_operand(work, loop, index, work->lasts), BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t difference = ir_vectorize_row(build, IR_SUB, IR_I64, 0, 0, last_value, first_value, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t coefficient = ir_vectorize_const(build, IR_I64, (uint64_t)work->coefficients[row]);
            const uint32_t expected = ir_vectorize_row(build, IR_MUL, IR_I64, 0, 0, distance, coefficient, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t affine = ir_vectorize_row(build, IR_ICMP, IR_I1, 0, 0, difference, expected, IR_EQ, BUSTER_IR_NONE);
            result = ir_vectorize_row(build, IR_SELECT, IR_I1, 0, 0, result, affine, result, BUSTER_IR_NONE);
        }
    }
    return result;
}

// One accepted loop's G, VB and VX, and its preheader's branch and header's phis rewritten. A masked tail's (#2807)
// are G, P, VB, T, TM and TX: G enters P where the trip count is nonzero (its overlap tests cover every element); P
// enters VB where a whole vector is, else T; VB exits to T, which enters TM where the trip count has a remainder of the
// lanes, else TX; TM runs the remaining lanes' iteration under a mask of them; and TX reduces as VX does and enters the
// original header at the trip's end, whose exit test fails at once.
static void ir_vectorize_emit(IrVectorizeWork* work, IrVectorizeBuild* build, uint32_t loop, const IrVectorizeLoop* plan)
{
    const IrFunction* f = work->function;
    IrFunction* v = build->view;
    const uint32_t lanes = plan->lanes;
    const uint32_t itype = f->type[plan->induction];
    const bool masked = plan->masked;
    const uint32_t g = ir_vectorize_block(build);
    const uint32_t pick = g + 1;
    const uint32_t body = masked ? g + 2 : g + 1;
    const uint32_t exit = masked ? g + 3 : g + 2;
    // TM and TX, the masked iteration and the join the header is entered from (VX itself without a masked tail).
    const uint32_t tail = g + 4;
    const uint32_t join = masked ? g + 5 : exit;
    // G: the speculatable uniforms, in order.
    for (uint32_t k = 0; k < plan->row_count; k += 1)
    {
        const uint32_t i = work->rows[k];
        if (work->classes[i] == IR_VECTORIZE_CLASS_UNIFORM && work->placed_g[i] != 0)
        {
            const bool copy = f->op[i] == IR_COPY || f->op[i] == IR_PHI;
            work->values[i] = copy ? ir_vectorize_operand(work, loop, f->op[i] == IR_PHI ? f->extra[f->a[i] + 1] : f->a[i], NULL) : ir_vectorize_clone(work, build, loop, i, NULL);
        }
    }
    // The trip count from the start (plus the exit test's offset, which must not wrap) to the bound, 0 where the bound
    // isn't past it, as 64 bits.
    const uint32_t start = plan->start;
    const uint32_t bound = ir_vectorize_operand(work, loop, plan->bound, NULL);
    const uint32_t zero64 = ir_vectorize_const(build, IR_I64, 0);
    uint32_t first = start;
    uint32_t unwrapped = BUSTER_IR_NONE;
    if (plan->offset_row != BUSTER_IR_NONE)
    {
        const uint32_t offset = ir_vectorize_const(build, itype, plan->offset);
        first = ir_vectorize_row(build, IR_ADD, itype, 0, 0, start, offset, BUSTER_IR_NONE, BUSTER_IR_NONE);
        unwrapped = ir_vectorize_row(build, IR_ICMP, IR_I1, 0, 0, first, start, plan->predicate == IR_ULT ? IR_UGE : IR_GE, BUSTER_IR_NONE);
    }
    const uint32_t past = ir_vectorize_row(build, IR_ICMP, IR_I1, 0, 0, bound, first, plan->predicate == IR_ULT ? IR_UGT : IR_GT, BUSTER_IR_NONE);
    uint32_t difference = BUSTER_IR_NONE;
    if (itype == IR_I64)
    {
        difference = ir_vectorize_row(build, IR_SUB, IR_I64, 0, 0, bound, first, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    else
    {
        const uint32_t extend = plan->predicate == IR_ULT ? IR_ZEXT : IR_SEXT;
        const uint32_t wide_bound = ir_vectorize_row(build, extend, IR_I64, 0, 0, bound, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t wide_start = ir_vectorize_row(build, extend, IR_I64, 0, 0, first, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        difference = ir_vectorize_row(build, IR_SUB, IR_I64, 0, 0, wide_bound, wide_start, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    const uint32_t trip = ir_vectorize_row(build, IR_SELECT, IR_I64, 0, 0, past, difference, zero64, BUSTER_IR_NONE);
    const uint32_t round = ir_vectorize_const(build, IR_I64, work->mutation == BUSTER_IR_VECTORIZE_MUTATE_TAIL ? UINT64_MAX : 0 - (uint64_t)lanes);
    const uint32_t vector_n = ir_vectorize_row(build, IR_AND, IR_I64, 0, 0, trip, round, BUSTER_IR_NONE, BUSTER_IR_NONE);
    const uint32_t narrow = itype == IR_I64 ? vector_n : ir_vectorize_row(build, IR_TRUNC, itype, 0, 0, vector_n, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
    const uint32_t end = ir_vectorize_row(build, IR_ADD, itype, 0, 0, start, narrow, BUSTER_IR_NONE, BUSTER_IR_NONE);
    const uint32_t step = ir_vectorize_const(build, itype, lanes);
    // A masked tail covers every element: its loop runs where the trip count is nonzero, and ends at the trip's end.
    const uint32_t covered = masked ? trip : vector_n;
    uint32_t stop = end;
    if (masked)
    {
        const uint32_t narrow_trip = itype == IR_I64 ? trip : ir_vectorize_row(build, IR_TRUNC, itype, 0, 0, trip, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        stop = ir_vectorize_row(build, IR_ADD, itype, 0, 0, start, narrow_trip, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    uint32_t enter = ir_vectorize_row(build, IR_ICMP, IR_I1, 0, 0, covered, zero64, IR_NE, BUSTER_IR_NONE);
    if (unwrapped != BUSTER_IR_NONE)
    {
        enter = ir_vectorize_row(build, IR_SELECT, IR_I1, 0, 0, enter, unwrapped, enter, BUSTER_IR_NONE);
    }
    // The overlap tests: lane 0's pointers at the start, and each pair's ranges of vector_n elements (a masked tail's
    // trip count's) apart modulo 2^64.
    const uint32_t guards = work->mutation == BUSTER_IR_VECTORIZE_MUTATE_GUARDS ? 0 : plan->guards;
    if (guards != 0)
    {
        const uint32_t limit = ir_vectorize_const(build, IR_I64, BUSTER_IR_VECTORIZE_RANGE_LIMIT);
        const uint32_t small = ir_vectorize_row(build, IR_ICMP, IR_I1, 0, 0, covered, limit, IR_ULT, BUSTER_IR_NONE);
        enter = ir_vectorize_row(build, IR_SELECT, IR_I1, 0, 0, enter, small, enter, BUSTER_IR_NONE);
    }
    for (uint32_t pair = 0; pair < guards; pair += 1)
    {
        uint32_t base[2] = { 0, 0 };
        uint32_t length[2] = { 0, 0 };
        for (uint32_t side = 0; side < 2; side += 1)
        {
            const uint32_t access = plan->guard_pairs[pair * 2 + side];
            ir_vectorize_start_chain(work, build, loop, plan, f->a[access], work->starts, plan->start);
            const uint32_t pointer = ir_vectorize_operand(work, loop, f->a[access], work->starts);
            base[side] = f->type[f->a[access]] == IR_I64 ? pointer : ir_vectorize_row(build, IR_PTRTOINT, IR_I64, 0, 0, pointer, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            // A strided load's range is its stride's bytes per iteration (#2808), past its last element's end.
            const uint32_t stride = ir_vectorize_stride(work, access);
            const uint32_t factor = ir_vectorize_const(build, IR_I64, stride == 1 ? work->sizes[access] : (uint64_t)work->coefficients[f->a[access]]);
            length[side] = ir_vectorize_row(build, stride == 1 ? IR_SHL : IR_MUL, IR_I64, 0, 0, covered, factor, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t forward = ir_vectorize_row(build, IR_SUB, IR_I64, 0, 0, base[1], base[0], BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t backward = ir_vectorize_row(build, IR_SUB, IR_I64, 0, 0, base[0], base[1], BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t after = ir_vectorize_row(build, IR_ICMP, IR_I1, 0, 0, forward, length[0], IR_UGE, BUSTER_IR_NONE);
        const uint32_t before = ir_vectorize_row(build, IR_ICMP, IR_I1, 0, 0, backward, length[1], IR_UGE, BUSTER_IR_NONE);
        const uint32_t apart = ir_vectorize_row(build, IR_SELECT, IR_I1, 0, 0, after, before, after, BUSTER_IR_NONE);
        enter = ir_vectorize_row(build, IR_SELECT, IR_I1, 0, 0, enter, apart, enter, BUSTER_IR_NONE);
    }
    enter = ir_vectorize_narrow_guards(work, build, loop, plan, covered, enter);
    ir_vectorize_g_splats(work, build, loop, plan);
    // Arithmetic identities and the min/max accumulators' incoming values.
    uint32_t seeds[BUSTER_IR_VECTORIZE_REDUCTION_LIMIT] = { 0 };
    uint32_t count = 0;
    for (uint32_t i = f->block_offsets[plan->header]; f->op[i] == IR_PHI; i += 1)
    {
        if (work->classes[i] == IR_VECTORIZE_CLASS_REDUCTION)
        {
            const uint32_t update = ir_vectorize_incoming(f, i, plan->latch);
            const uint32_t operation = work->reduction[work->reduction_index[update]].operation;
            // Min/max is idempotent: seeding every lane includes the incoming accumulator in the horizontal result.
            uint32_t seed = ir_vectorize_incoming(f, i, plan->preheader);
            if (operation == BUSTER_IR_VECTORIZE_COUNT)
            {
                // A count's scalar accumulator starts at the incoming value (#2808).
                const bool mutated = work->mutation == BUSTER_IR_VECTORIZE_MUTATE_IDENTITY;
                seeds[count] = mutated ? ir_vectorize_row(build, IR_ADD, f->type[i], 0, 0, seed, ir_vectorize_const(build, f->type[i], 1), BUSTER_IR_NONE, BUSTER_IR_NONE) : seed;
            }
            else
            {
                if (operation >= BUSTER_IR_REDUCE_ADD)
                {
                    const uint64_t bits = (operation == BUSTER_IR_REDUCE_MUL ? 1 : operation == BUSTER_IR_REDUCE_AND ? UINT64_MAX : 0) + (work->mutation == BUSTER_IR_VECTORIZE_MUTATE_IDENTITY ? 1 : 0);
                    seed = ir_vectorize_const(build, f->type[i], bits);
                }
                seeds[count] = ir_vectorize_row(build, IR_INTRINSIC, ir_vectorize_vector_type(lanes << work->sizes[i]), 0, 0, seed, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_SPLAT, BUSTER_IR_NONE);
            }
            count += 1;
        }
    }
    ir_vectorize_row(build, IR_CBR, IR_VOID, 0, 0, enter, masked ? pick : body, plan->header, BUSTER_IR_NONE);
    // P: the vector loop where a whole vector is, else the masked tail alone.
    if (masked)
    {
        ir_vectorize_block(build);
        const uint32_t whole = ir_vectorize_row(build, IR_ICMP, IR_I1, 0, 0, vector_n, zero64, IR_NE, BUSTER_IR_NONE);
        ir_vectorize_row(build, IR_CBR, IR_VOID, 0, 0, whole, body, exit, BUSTER_IR_NONE);
    }
    const uint32_t before = masked ? pick : g;
    // VB: lane 0's induction and the accumulators, then the loop's rows in order.
    ir_vectorize_block(build);
    const uint32_t induction = ir_vectorize_row(build, IR_PHI, itype, 0, 0, 0, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
    uint32_t phis[BUSTER_IR_VECTORIZE_REDUCTION_LIMIT] = { 0 };
    count = 0;
    for (uint32_t i = f->block_offsets[plan->header]; f->op[i] == IR_PHI; i += 1)
    {
        if (work->classes[i] == IR_VECTORIZE_CLASS_REDUCTION)
        {
            phis[count] = ir_vectorize_row(build, IR_PHI, ir_vectorize_accumulator(work, plan, i), 0, 0, 0, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            work->values[i] = phis[count];
            count += 1;
        }
    }
    work->values[plan->induction] = induction;
    for (uint32_t k = 0; k < plan->row_count; k += 1)
    {
        const uint32_t i = work->rows[k];
        const uint32_t class = work->classes[i];
        if (class == IR_VECTORIZE_CLASS_UNIFORM && work->placed_g[i] == 0)
        {
            const bool copy = f->op[i] == IR_COPY || f->op[i] == IR_PHI;
            work->values[i] = copy ? ir_vectorize_operand(work, loop, f->op[i] == IR_PHI ? f->extra[f->a[i] + 1] : f->a[i], NULL) : ir_vectorize_clone(work, build, loop, i, NULL);
        }
        else if (class == IR_VECTORIZE_CLASS_INDEX || class == IR_VECTORIZE_CLASS_POINTER)
        {
            const bool copy = f->op[i] == IR_COPY || f->op[i] == IR_PHI;
            work->values[i] = copy ? ir_vectorize_operand(work, loop, f->op[i] == IR_PHI ? f->extra[f->a[i] + 1] : f->a[i], NULL) : ir_vectorize_clone(work, build, loop, i, NULL);
        }
        else if (class == IR_VECTORIZE_CLASS_VECTOR || class == IR_VECTORIZE_CLASS_MASK || class == IR_VECTORIZE_CLASS_UPDATE)
        {
            ir_vectorize_vector_row(work, build, loop, plan, i);
        }
    }
    const uint32_t next = ir_vectorize_row(build, IR_ADD, itype, 0, 0, induction, step, BUSTER_IR_NONE, BUSTER_IR_NONE);
    const uint32_t more = ir_vectorize_row(build, IR_ICMP, IR_I1, 0, 0, next, end, plan->predicate, BUSTER_IR_NONE);
    ir_vectorize_row(build, IR_CBR, IR_VOID, 0, 0, more, body, exit, BUSTER_IR_NONE);
    // The accumulators' values at VB's back edge, before a masked tail clones the rows again.
    uint32_t updates[BUSTER_IR_VECTORIZE_REDUCTION_LIMIT] = { 0 };
    count = 0;
    for (uint32_t i = f->block_offsets[plan->header]; f->op[i] == IR_PHI; i += 1)
    {
        if (work->classes[i] == IR_VECTORIZE_CLASS_REDUCTION)
        {
            updates[count] = work->values[ir_vectorize_incoming(f, i, plan->latch)];
            count += 1;
        }
    }
    // VX: each accumulator reduced; an arithmetic one combined with its initial value (a min/max's lanes started from it).
    // With a masked tail, T first: the accumulators P or VB leave, and whether the trip count has a remainder; then TM,
    // the masked iteration at VB's end (G's `end`); then TX, which joins the accumulators and reduces as VX does.
    ir_vectorize_block(build);
    uint32_t tails[BUSTER_IR_VECTORIZE_REDUCTION_LIMIT] = { 0 };
    uint32_t masked_updates[BUSTER_IR_VECTORIZE_REDUCTION_LIMIT] = { 0 };
    uint32_t joins[BUSTER_IR_VECTORIZE_REDUCTION_LIMIT] = { 0 };
    if (masked)
    {
        count = 0;
        for (uint32_t i = f->block_offsets[plan->header]; f->op[i] == IR_PHI; i += 1)
        {
            if (work->classes[i] == IR_VECTORIZE_CLASS_REDUCTION)
            {
                tails[count] = ir_vectorize_row(build, IR_PHI, ir_vectorize_accumulator(work, plan, i), 0, 0, 0, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                work->values[i] = tails[count];
                count += 1;
            }
        }
        const uint32_t below = ir_vectorize_const(build, IR_I64, lanes - 1);
        const uint32_t remaining = ir_vectorize_row(build, IR_AND, IR_I64, 0, 0, trip, below, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t partial = ir_vectorize_row(build, IR_ICMP, IR_I1, 0, 0, remaining, zero64, IR_NE, BUSTER_IR_NONE);
        ir_vectorize_row(build, IR_CBR, IR_VOID, 0, 0, partial, tail, join, BUSTER_IR_NONE);
        // TM: the mask of the remainder, (1 << remainder) - 1 at the mask's type, and the loop's rows under it.
        ir_vectorize_block(build);
        work->values[plan->induction] = end;
        const uint32_t mtype = ir_vectorize_mask_type(lanes);
        const uint32_t bits_type = mtype == IR_M8 ? IR_I8 : mtype == IR_M16 ? IR_I16 : mtype == IR_M32 ? IR_I32 : IR_I64;
        const uint32_t one = ir_vectorize_const(build, IR_I64, 1);
        const uint32_t bit = work->mutation == BUSTER_IR_VECTORIZE_MUTATE_MASK ? ir_vectorize_const(build, IR_I64, 2) : one;
        const uint32_t shifted = ir_vectorize_row(build, IR_SHL, IR_I64, 0, 0, bit, remaining, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t low = ir_vectorize_row(build, IR_SUB, IR_I64, 0, 0, shifted, one, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t narrow_low = bits_type == IR_I64 ? low : ir_vectorize_row(build, IR_TRUNC, bits_type, 0, 0, low, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t mask = ir_vectorize_row(build, IR_BITCAST, mtype, 0, 0, narrow_low, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_vectorize_tail_rows(work, build, loop, plan, mask);
        ir_vectorize_row(build, IR_BR, IR_VOID, 0, 0, join, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        // TX: each accumulator T's or TM's, which the reduction below reads.
        ir_vectorize_block(build);
        count = 0;
        for (uint32_t i = f->block_offsets[plan->header]; f->op[i] == IR_PHI; i += 1)
        {
            if (work->classes[i] == IR_VECTORIZE_CLASS_REDUCTION)
            {
                const uint32_t update = ir_vectorize_incoming(f, i, plan->latch);
                masked_updates[count] = work->values[update];
                joins[count] = ir_vectorize_row(build, IR_PHI, ir_vectorize_accumulator(work, plan, i), 0, 0, 0, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                work->values[update] = joins[count];
                count += 1;
            }
        }
    }
    uint32_t finals[BUSTER_IR_VECTORIZE_REDUCTION_LIMIT] = { 0 };
    count = 0;
    for (uint32_t i = f->block_offsets[plan->header]; f->op[i] == IR_PHI; i += 1)
    {
        if (work->classes[i] == IR_VECTORIZE_CLASS_REDUCTION)
        {
            const uint32_t update = ir_vectorize_incoming(f, i, plan->latch);
            const uint32_t operation = work->reduction[work->reduction_index[update]].operation;
            const uint32_t size = work->sizes[i];
            if (operation == BUSTER_IR_VECTORIZE_COUNT)
            {
                // A count's accumulator is the scalar result, the incoming value included (#2808).
                finals[count] = work->values[update];
            }
            else
            {
                const uint32_t reduced = ir_vectorize_row(build, IR_INTRINSIC, IR_V4I32, size | operation << 2, 0, work->values[update], BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_REDUCE, BUSTER_IR_NONE);
                const uint32_t lane = ir_vectorize_row(build, IR_INTRINSIC, f->type[i], size, 0, reduced, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_LANE_EXTRACT, BUSTER_IR_NONE);
                const uint32_t initial = ir_vectorize_incoming(f, i, plan->preheader);
                finals[count] = operation < BUSTER_IR_REDUCE_ADD ? lane : ir_vectorize_row(build, f->op[update], f->type[i], 0, 0, initial, lane, BUSTER_IR_NONE, ir_vectorize_origin(f, update));
            }
            count += 1;
        }
    }
    ir_vectorize_row(build, IR_BR, IR_VOID, 0, 0, plan->header, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
    // The phis' incoming pairs, the preheader's branch to G and the header's phis.
    const uint32_t induction_pairs[4] = { before, start, body, next };
    const uint32_t induction_extra = ir_vectorize_pairs(build, induction_pairs, 2);
    if (!build->counting)
    {
        v->a[induction] = induction_extra;
        v->a[f->block_offsets[(uint64_t)plan->preheader + 1] - 1] = g;
    }
    count = 0;
    for (uint32_t i = f->block_offsets[plan->header]; f->op[i] == IR_PHI; i += 1)
    {
        const bool reduction = work->classes[i] == IR_VECTORIZE_CLASS_REDUCTION;
        const uint32_t initial = ir_vectorize_incoming(f, i, plan->preheader);
        const uint32_t back = ir_vectorize_incoming(f, i, plan->latch);
        if (reduction)
        {
            const uint32_t pairs[4] = { before, seeds[count], body, updates[count] };
            const uint32_t offset = ir_vectorize_pairs(build, pairs, 2);
            const uint32_t tail_offset = masked ? ir_vectorize_pairs(build, pairs, 2) : 0;
            const uint32_t join_pairs[4] = { exit, tails[count], tail, masked_updates[count] };
            const uint32_t join_offset = masked ? ir_vectorize_pairs(build, join_pairs, 2) : 0;
            if (!build->counting)
            {
                v->a[phis[count]] = offset;
            }
            if (!build->counting && masked)
            {
                v->a[tails[count]] = tail_offset;
                v->a[joins[count]] = join_offset;
            }
        }
        const uint32_t final = reduction ? finals[count] : stop;
        const uint32_t pairs[6] = { g, initial, plan->latch, back, join, final };
        const uint32_t offset = ir_vectorize_pairs(build, pairs, 3);
        if (!build->counting)
        {
            v->a[i] = offset;
            v->b[i] = 3;
        }
        count += reduction ? 1 : 0;
    }
}

// Plans every loop header the walk found and emits the accepted loops: once counting, once writing. Both replays
// decide alike.
static void ir_vectorize_replay(IrVectorizeWork* work, IrVectorizeBuild* build)
{
    for (uint32_t loop = 0; loop < work->header_count; loop += 1)
    {
        {
            IrVectorizeLoop plan = { 0 };
            const uint32_t refusal = ir_vectorize_plan(work, loop, &plan);
            if (build->counting)
            {
                work->statistics.loops += 1;
                if (refusal == IR_VECTORIZE_REFUSALS)
                {
                    work->statistics.vectorized += 1;
                    work->statistics.widths[plan.width] += 1;
                    work->statistics.reductions += plan.reductions;
                    work->statistics.guards += plan.guards;
                    work->statistics.masked += plan.masked ? 1u : 0u;
                }
                else
                {
                    work->statistics.refused[refusal] += 1;
                }
            }
            if (refusal == IR_VECTORIZE_REFUSALS)
            {
                ir_vectorize_emit(work, build, loop, &plan);
            }
            ir_vectorize_reset(work);
        }
    }
}

// Whether a phi takes a value along an edge from a block numbered at or past its own: a candidate back edge. Phis lead
// their blocks, so each block's leading rows alone are read. A function without one has no loop the lowering numbered
// in order, and walks no blocks.
static bool ir_vectorize_candidate(const IrFunction* f)
{
    bool found = false;
    for (uint32_t block = 1; !found && block < f->block_count; block += 1)
    {
        const uint32_t end = f->block_offsets[(uint64_t)block + 1];
        for (uint32_t i = f->block_offsets[block]; !found && i < end && f->op[i] == IR_PHI; i += 1)
        {
            for (uint32_t k = 0; k < f->b[i]; k += 1)
            {
                found = found || f->extra[f->a[i] + k * 2] >= block;
            }
        }
    }
    return found;
}

// Every operation the IR defines, at every width: the interpreter's target, for the tests.
static __attribute__((unused)) void ir_vectorize_target_ir(IrVectorizeTarget* target)
{
    *target = (IrVectorizeTarget){ .moves = 7 };
    static const uint8_t binaries[] = { IR_ADD, IR_SUB, IR_MUL, IR_AND, IR_OR, IR_XOR, IR_SHL, IR_LSHR, IR_ASHR, IR_FADD, IR_FSUB, IR_FMUL, IR_FDIV, IR_FMA };
    for (uint32_t lane = 0; lane < IR_SIMD_LANE_COUNT; lane += 1)
    {
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(binaries); k += 1)
        {
            const uint32_t op = binaries[k];
            const bool bitwise = op == IR_AND || op == IR_OR || op == IR_XOR;
            // The bitwise operations read no lanes: any lanes' bits are theirs.
            target->binary[op][lane] = bitwise || ir_lane_valid(op, lane) ? 7 : 0;
        }
        target->compare[lane] = 7;
        target->select[lane] = 7;
        target->splat[lane] = 7;
    }
    for (uint32_t from = 0; from < 4; from += 1)
    {
        for (uint32_t to = 0; to < 4; to += 1)
        {
            target->extend[from][to][0] = from < to ? 7 : 0;
            target->extend[from][to][1] = from < to ? 7 : 0;
            target->truncate[from][to] = from > to ? 7 : 0;
        }
        target->popcount[from] = 7;
        target->masked[from] = 7;
        target->permute2[from] = 7;
    }
    // Same-width integer/float conversions (#2806), keyed by the float lane.
    target->binary[IR_SITOFP][IR_SIMD_LANE_F32] = 7;
    target->binary[IR_SITOFP][IR_SIMD_LANE_F64] = 7;
    target->binary[IR_UITOFP][IR_SIMD_LANE_F32] = 7;
    target->binary[IR_UITOFP][IR_SIMD_LANE_F64] = 7;
    target->binary[IR_FPTOSI][IR_SIMD_LANE_F32] = 7;
    target->binary[IR_FPTOSI][IR_SIMD_LANE_F64] = 7;
    target->binary[IR_FPTOUI][IR_SIMD_LANE_F32] = 7;
    target->binary[IR_FPTOUI][IR_SIMD_LANE_F64] = 7;
    // A mask of 2 to 64 lanes, and its count.
    target->mask = 0x7e;
    target->count = 0x7e;
    for (uint32_t operation = 0; operation < BUSTER_IR_REDUCE_OPERATIONS; operation += 1)
    {
        target->reduce[2][operation] = 7;
        target->reduce[3][operation] = operation >= BUSTER_IR_REDUCE_ADD ? 7 : 0;
    }
}

static __attribute__((unused)) IrCheck ir_vectorize_loops(const IrFunction* function, const IrVectorizeOptions* options, Arena* output, Arena* scratch, IrVectorizeResult* result)
{
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    IrVectorizeWork work = { .function = function, .target = options->target, .mutation = options->mutation };
    IrCheck check = ir_check(output != scratch ? IR_OK : IR_ERROR_METADATA, BUSTER_IR_NONE, 0);
    const bool candidate = check.error == IR_OK && function->label_count == 0 && function->block_count != 0 && ir_vectorize_candidate(function);
    if (candidate)
    {
        check.error = ir_vectorize_graph(&work, scratch) ? IR_OK : IR_ERROR_MEMORY;
    }
    const uint64_t n = function->instruction_count;
    // Most loops the walk finds aren't the admitted shape (an outer loop, control flow in the body): a function none of
    // whose loops is skips the rows' arrays and kinds, and counts its loops refused as their shape, as planning would.
    bool shaped = false;
    for (uint32_t loop = 0; candidate && check.error == IR_OK && !shaped && loop < work.header_count; loop += 1)
    {
        IrVectorizeLoop probe = { 0 };
        shaped = ir_vectorize_chain(&work, loop, &probe) != BUSTER_IR_NONE;
    }
    work.statistics.loops = candidate && check.error == IR_OK && !shaped ? work.header_count : 0;
    work.statistics.refused[IR_VECTORIZE_REFUSE_SHAPE] = work.statistics.loops;
    if (shaped)
    {
        bool ok = true;
        work.kinds = ir_allocate(scratch, n, 1, &ok);
        work.classes = ir_allocate(scratch, n, 1, &ok);
        work.reduction_index = ir_allocate(scratch, n, 1, &ok);
        work.needed = ir_allocate(scratch, n, 1, &ok);
        work.sizes = ir_allocate(scratch, n, 1, &ok);
        work.floating = ir_allocate(scratch, n, 1, &ok);
        work.placed_g = ir_allocate(scratch, n, 1, &ok);
        work.coefficients = ir_allocate(scratch, n, sizeof(int64_t), &ok);
        work.values = ir_allocate(scratch, n, 4, &ok);
        work.starts = ir_allocate(scratch, n, 4, &ok);
        work.lasts = ir_allocate(scratch, n, 4, &ok);
        work.splats = ir_allocate(scratch, n, 4, &ok);
        work.ones = ir_allocate(scratch, n, 4, &ok);
        work.zeros = ir_allocate(scratch, n, 4, &ok);
        work.rows = ir_allocate(scratch, n, 4, &ok);
        // Each value of a chain is pushed once and pushes its operands once: at most three per value, and the first.
        work.chain_capacity = n * 3 + 4;
        work.chain = ir_allocate(scratch, work.chain_capacity, 4, &ok);
        work.chain_expanded = ir_allocate(scratch, work.chain_capacity, 1, &ok);
        // Each value is touched at most a few times per loop: its class, its splat and its needed bit.
        work.touched = ir_allocate(scratch, n * 4 + 8, 4, &ok);
        work.row_marks = ir_allocate(scratch, n, 4, &ok);
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (shaped && check.error == IR_OK)
    {
        memset(work.values, 0xff, n * 4);
        memset(work.starts, 0xff, n * 4);
        memset(work.lasts, 0xff, n * 4);
        memset(work.splats, 0xff, n * 4);
        memset(work.ones, 0xff, n * 4);
        memset(work.zeros, 0xff, n * 4);
        ir_vectorize_kinds(function, work.kinds);
        IrVectorizeBuild counting = {
            .counting = true,
            .rows = function->instruction_count,
            .blocks = function->block_count,
            .extras = function->extra_count,
            .constants = function->constant_count,
            .vector_constants = function->vector_constant_count,
            .addresses = function->address_count,
        };
        ir_vectorize_replay(&work, &counting);
        if (work.statistics.vectorized != 0)
        {
            const uint64_t rows = counting.rows;
            const uint64_t blocks = counting.blocks;
            bool ok = rows < UINT32_MAX && blocks < UINT32_MAX;
            IrFunction view = *function;
            view.instruction_count = (uint32_t)rows;
            view.block_count = (uint32_t)blocks;
            view.extra_count = counting.extras;
            view.constant_count = counting.constants;
            view.op = ir_allocate(output, rows, 1, &ok);
            view.type = ir_allocate(output, rows, 1, &ok);
            view.flags = ir_allocate(output, rows, 1, &ok);
            view.alignment_log2 = ir_allocate(output, rows, 1, &ok);
            view.a = ir_allocate(output, rows, 4, &ok);
            view.b = ir_allocate(output, rows, 4, &ok);
            view.c = ir_allocate(output, rows, 4, &ok);
            view.block_offsets = ir_allocate(output, blocks + 1, 4, &ok);
            view.extra = ir_allocate(output, counting.extras, 4, &ok);
            view.constants = ir_allocate(output, counting.constants, sizeof(IrConstant), &ok);
            // The strided loads' indices and offsets (#2808), past the function's own: its tables otherwise stay shared.
            const bool vectors = counting.vector_constants != function->vector_constant_count;
            const bool addresses = counting.addresses != function->address_count;
            view.vector_constant_count = counting.vector_constants;
            view.address_count = counting.addresses;
            view.vector_constants = vectors ? ir_allocate(output, counting.vector_constants, sizeof(IrSimdValue), &ok) : function->vector_constants;
            view.addresses = addresses ? ir_allocate(output, counting.addresses, sizeof(IrAddress), &ok) : function->addresses;
            ok = ok && ir_debug_create(output, &view, function->debug);
            check.error = ok ? IR_OK : rows >= UINT32_MAX || blocks >= UINT32_MAX ? IR_ERROR_LIMIT : IR_ERROR_MEMORY;
            if (check.error == IR_OK)
            {
                memcpy(view.op, function->op, n);
                memcpy(view.type, function->type, n);
                memcpy(view.flags, function->flags, n);
                memcpy(view.alignment_log2, function->alignment_log2, n);
                memcpy(view.a, function->a, n * 4);
                memcpy(view.b, function->b, n * 4);
                memcpy(view.c, function->c, n * 4);
                memcpy(view.block_offsets, function->block_offsets, (uint64_t)function->block_count * 4);
                if (function->extra_count != 0)
                {
                    memcpy(view.extra, function->extra, (uint64_t)function->extra_count * 4);
                }
                if (function->constant_count != 0)
                {
                    memcpy(view.constants, function->constants, (uint64_t)function->constant_count * sizeof(IrConstant));
                }
                if (vectors && function->vector_constant_count != 0)
                {
                    memcpy(view.vector_constants, function->vector_constants, (uint64_t)function->vector_constant_count * sizeof(IrSimdValue));
                }
                if (addresses && function->address_count != 0)
                {
                    memcpy(view.addresses, function->addresses, (uint64_t)function->address_count * sizeof(IrAddress));
                }
                ir_debug_remap(function, &view, NULL, NULL, NULL);
                IrVectorizeBuild build = {
                    .counting = false,
                    .view = &view,
                    .rows = function->instruction_count,
                    .blocks = function->block_count,
                    .extras = function->extra_count,
                    .constants = function->constant_count,
                    .vector_constants = function->vector_constant_count,
                    .addresses = function->address_count,
                };
                const IrVectorizeResult counted = work.statistics;
                ir_vectorize_replay(&work, &build);
                work.statistics = counted;
                view.block_offsets[view.block_count] = view.instruction_count;
                const bool tables = build.vector_constants == counting.vector_constants && build.addresses == counting.addresses;
                check.error = build.rows == counting.rows && build.blocks == counting.blocks && build.extras == counting.extras && build.constants == counting.constants && tables ? IR_OK : IR_ERROR_STRUCTURE;
                view.arena_bytes = output->length - om;
            }
            if (check.error == IR_OK)
            {
                work.statistics.function = view;
                work.statistics.changed = true;
            }
        }
    }
    // Structural verification: the input may hold if-conversion's unreachable stubs, whose uses of reachable values no
    // dominator tree covers until the optimizer that follows removes them (it verifies dominance then).
    if (check.error == IR_OK && work.statistics.changed)
    {
        check = ir_verify(&work.statistics.function, scratch, NULL);
    }
    if (check.error == IR_OK)
    {
        if (!work.statistics.changed)
        {
            work.statistics.function = *function;
        }
        *result = work.statistics;
    }
    if (check.error != IR_OK || !work.statistics.changed)
    {
        arena_pop_to(output, om);
    }
    arena_pop_to(scratch, sm);
    return check;
}

#endif
