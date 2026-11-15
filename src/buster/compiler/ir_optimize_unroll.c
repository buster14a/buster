// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's loop unroller (#2755, docs/tier1-loop-unrolling.md). Include after ir_optimize.c and dominators.c.
// ir_optimize_unroll takes verified SSA and gives each counted innermost loop it admits a main loop of `factor` copies
// of its body, entered and continued only while that many whole iterations remain, before the original loop, which
// stays as it was as the remainder of fewer than `factor` iterations and as the whole loop where the main one can't run:
//
//   P:  ...; cbr (start < n), MP, H                  a loop of no iteration goes straight to the original
//   MP: cbr (n - start >u K'), MH, MX                 n - start is the true difference; then bound = n - K' can't wrap
//   MH: the header's phis' main copies; copy 1 of the body, ..., copy `factor`;
//       next = i + factor * step; cbr (next < bound), MH, MX
//   MX: the main loop's values (a split reduction's accumulators combined); br H
//   H:  the original loop, its phis entered from P as before and from MX
//
// K' is (factor - 1) * step + c, less one for a non-strict test, where the test reads the induction phi plus c >= 0 (0
// for the phi itself, the step for its increment): every one of the `factor` iterations then passes the original test,
// so each copy's own test is known and its exit branch becomes a jump. Copy k's induction is i + (k - 1) * step from the main phi
// rather than a chain of increments. An integer reduction (wrapping add, sub, mul, and, or, xor, alone or under a select
// against the old value, and signed or unsigned minimum or maximum by an icmp and a select) whose phi and update the
// loop uses nowhere else takes up to `accumulators` accumulators, the extra ones starting at the operation's identity,
// which MX combines: exact for wrapping integers in any order. Floating values are never split.
//
// A loop is admitted when it is innermost and reducible, has one outside predecessor ending in `br` to its header (its
// preheader), one latch, and one exit edge, from a conditional branch in its header or its latch on an icmp of an
// integer induction phi (or it plus a constant, as its increment) against a value defined outside the loop or a
// constant; the induction's increment adds a positive constant step; the test continues while ult, ule, slt or sle
// (either operand order, either branch sense);
// and it has no call, switch, indirect branch, labeled block, return, unreachable, alloca or stack save/restore. Each
// refused loop is counted by its first failing rule (IrUnrollRefusal). The factor is the largest power of two up to
// `factor` whose copies' estimated cost (the instructions live in a copy, minus the free ones) fits `budget`, and a
// function grows by at most `growth` instructions.
//
// Data-parallel parts with exact scalar twins: the opcode classes (one byte permute of a 128-entry table per 64
// instructions), each loop block's class union (64-byte masked loads, or-reduced) and the operand remap of every
// copied range (16 instructions at a time: the operand tables' masks and masked gathers from the old-to-new map). The
// per-loop recognition and the CFG and phi construction are the small ordered part: a loop's blocks, its header's phis
// and its liveness walk, bounded by BUSTER_IR_UNROLL_LOOP_LIMIT instructions. Both twins give identical functions.
//
// Contract: as ir_thread's. The input is never mutated. The result is unverified, valid SSA that the optimizer which
// follows verifies (the tests verify it with dominance); it borrows the input's immutable tables (slots, addresses,
// call types, rows, asm bodies, labels, ABI records) and owns new columns, block offsets, extra words, calls and
// constants in `output`. Original blocks keep their identities, new blocks follow them, so labels and debug regions
// stay valid; a copy's instructions keep their origins. Nothing admitted gives the input itself. All scratch is
// returned; on failure, so is `output`'s growth.
//
// Layout: ir_unroll_classify, ir_unroll_block_classes (twins); ir_unroll_constant, ir_unroll_reduction,
// ir_unroll_cost, ir_unroll_loop (recognition); ir_unroll_remap (twins); ir_unroll_rewrite; ir_optimize_unroll.

#ifndef BUSTER_IR_UNROLL_C
#define BUSTER_IR_UNROLL_C

#define BUSTER_IR_UNROLL_FACTOR 8u
#define BUSTER_IR_UNROLL_BUDGET 128u
#define BUSTER_IR_UNROLL_ACCUMULATORS 4u
#define BUSTER_IR_UNROLL_GROWTH 4096u
// The largest step an induction may add, and the most instructions a loop may have for its recognition to run.
#define BUSTER_IR_UNROLL_STEP_LIMIT 65536u
#define BUSTER_IR_UNROLL_LOOP_LIMIT 1024u
// Copies, extensions and truncations followed from a step to its constant.
#define BUSTER_IR_UNROLL_CONSTANT_LINKS 8u
#define BUSTER_IR_UNROLL_BATCH 64u
// The general values a main loop may keep live across its copies: its phis, the outside values it reads and its bound.
// Tier 1 gives a value that crosses no call the caller-saved registers alone (docs/tier1-allocation.md), nine, two of
// which a copy's own temporaries take. Past it, a reduction takes fewer accumulators.
#define BUSTER_IR_UNROLL_PRESSURE 7u

// An address's index the copies fold (IrUnrollWork.folds): the induction phi itself, or its extension (sext under a
// signed test, zext under an unsigned one) in a block that dominates the latch.
#define BUSTER_IR_UNROLL_FOLD_PHI 1u
#define BUSTER_IR_UNROLL_FOLD_EXTENSION 2u

// An opcode's class bits (ir_unroll_classes): the refusals a loop body takes from it, a phi, a root of the liveness
// walk (an effect or a trap a copy keeps), and an operation that costs a copy nothing.
#define BUSTER_IR_UNROLL_CALL 1u
#define BUSTER_IR_UNROLL_CONTROL 2u
#define BUSTER_IR_UNROLL_EXIT 4u
#define BUSTER_IR_UNROLL_STACK 8u
#define BUSTER_IR_UNROLL_PHI 16u
#define BUSTER_IR_UNROLL_EFFECT 32u
#define BUSTER_IR_UNROLL_FREE 64u

// A header phi's role (IrUnrollWork.roles).
#define BUSTER_IR_UNROLL_CARRIED 0u
#define BUSTER_IR_UNROLL_INDUCTION 1u
#define BUSTER_IR_UNROLL_REDUCTION 2u

typedef enum IrUnrollRefusal
{
    IR_UNROLL_UNROLLED,
    IR_UNROLL_OUTER,
    IR_UNROLL_IRREDUCIBLE,
    IR_UNROLL_PREHEADER,
    IR_UNROLL_LATCH,
    IR_UNROLL_EXIT,
    IR_UNROLL_CALL_REFUSED,
    IR_UNROLL_CONTROL_REFUSED,
    IR_UNROLL_STACK_REFUSED,
    IR_UNROLL_TEST,
    IR_UNROLL_PREDICATE,
    IR_UNROLL_BOUND,
    IR_UNROLL_INDUCTION_REFUSED,
    IR_UNROLL_SIZE,
    IR_UNROLL_GROWTH_REFUSED,
    IR_UNROLL_OUTCOME_COUNT,
} IrUnrollRefusal;

// --tier1-report's names of the outcomes, the first the unrolled loops'.
static const char* const ir_unroll_outcome_names[IR_UNROLL_OUTCOME_COUNT] = {
    "unrolled",
    "outer",
    "irreducible",
    "preheader",
    "latch",
    "exit",
    "call",
    "control",
    "stack",
    "test",
    "predicate",
    "bound",
    "induction",
    "size",
    "growth",
};

typedef struct IrUnrollOptions IrUnrollOptions;
struct IrUnrollOptions
{
    uint32_t factor;
    uint32_t budget;
    uint32_t accumulators;
    uint32_t growth;
    bool scalar;
};

typedef struct IrUnrollResult IrUnrollResult;
struct IrUnrollResult
{
    IrFunction function;
    // Each natural loop's outcome: unrolled, or its refusal.
    uint32_t outcomes[IR_UNROLL_OUTCOME_COUNT];
    // The copies the unrolled loops' main loops hold, their reductions split into accumulators, and the instructions added.
    uint32_t copies;
    uint32_t reductions;
    uint32_t added;
};

typedef struct IrUnrollLoop IrUnrollLoop;
struct IrUnrollLoop
{
    uint32_t header;
    uint32_t preheader;
    uint32_t latch;
    uint32_t exiting;
    uint32_t induction;
    uint32_t start;
    uint32_t bound;
    // The header's first instruction past its phis, and the loop's blocks in IrUnrollWork.lists (header first).
    uint32_t phi_end;
    uint32_t list;
    uint32_t block_count;
    uint32_t instructions;
    uint32_t reductions;
    // The main phis, one per header phi and `accumulators` per split reduction, and the instructions combining them.
    uint32_t main_phis;
    uint32_t combines;
    // The addresses whose index the copies fold into their displacements, and the main loop's general values.
    uint32_t addresses;
    uint32_t pressure;
    // Its first new block, copy 1's header (MH); MX follows the copies' factor * block_count blocks.
    uint32_t first_block;
    uint32_t added;
    uint64_t step;
    uint64_t offset;
    // A bound that is a constant (wherever the lowering defined it), which the preheader makes again.
    uint64_t bound_value;
    bool bound_constant;
    uint8_t type;
    uint8_t predicate;
    uint8_t factor;
    uint8_t accumulators;
};

typedef struct IrUnrollWork IrUnrollWork;
struct IrUnrollWork
{
    const IrFunction* function;
    IrCfg cfg;
    Dominators loops;
    uint8_t* classes;
    uint8_t* labeled;
    // Per block: its index in its admitted loop's list. Per instruction: uses inside its own loop, liveness, a header phi's
    // role and a reduction's combining operation, predicate and identity.
    uint32_t* positions;
    uint32_t* uses;
    uint8_t* live;
    uint8_t* roles;
    uint8_t* combine_ops;
    uint8_t* combine_predicates;
    uint64_t* identities;
    uint8_t* folds;
    uint32_t* stack;
    uint32_t* lists;
    uint32_t list_count;
    IrUnrollLoop* accepted;
    uint32_t accepted_count;
    IrUnrollResult statistics;
};

static __attribute__((unused)) IrUnrollOptions ir_unroll_options(void)
{
    const IrUnrollOptions options = {
        .factor = BUSTER_IR_UNROLL_FACTOR,
        .budget = BUSTER_IR_UNROLL_BUDGET,
        .accumulators = BUSTER_IR_UNROLL_ACCUMULATORS,
        .growth = BUSTER_IR_UNROLL_GROWTH,
    };
    return options;
}

static const uint8_t ir_unroll_classes[BUSTER_IR_OP_TABLE] = {
    [IR_CONST] = BUSTER_IR_UNROLL_FREE,
    [IR_UNDEF] = BUSTER_IR_UNROLL_FREE,
    [IR_PARAM] = BUSTER_IR_UNROLL_FREE,
    [IR_COPY] = BUSTER_IR_UNROLL_FREE,
    [IR_INTTOPTR] = BUSTER_IR_UNROLL_FREE,
    [IR_PTRTOINT] = BUSTER_IR_UNROLL_FREE,
    [IR_BITCAST] = BUSTER_IR_UNROLL_FREE,
    [IR_STACK_ADDR] = BUSTER_IR_UNROLL_FREE,
    [IR_SYMBOL_ADDR] = BUSTER_IR_UNROLL_FREE,
    [IR_SDIV] = BUSTER_IR_UNROLL_EFFECT,
    [IR_UDIV] = BUSTER_IR_UNROLL_EFFECT,
    [IR_SREM] = BUSTER_IR_UNROLL_EFFECT,
    [IR_UREM] = BUSTER_IR_UNROLL_EFFECT,
    [IR_STORE] = BUSTER_IR_UNROLL_EFFECT,
    [IR_MASKED_STORE] = BUSTER_IR_UNROLL_EFFECT,
    [IR_COMPRESS_STORE] = BUSTER_IR_UNROLL_EFFECT,
    [IR_SCATTER] = BUSTER_IR_UNROLL_EFFECT,
    [IR_ATOMIC_RMW] = BUSTER_IR_UNROLL_EFFECT,
    [IR_FENCE] = BUSTER_IR_UNROLL_EFFECT,
    [IR_LIFETIME] = BUSTER_IR_UNROLL_EFFECT | BUSTER_IR_UNROLL_FREE,
    [IR_CALL] = BUSTER_IR_UNROLL_CALL | BUSTER_IR_UNROLL_EFFECT,
    [IR_CALL_INDIRECT] = BUSTER_IR_UNROLL_CALL | BUSTER_IR_UNROLL_EFFECT,
    [IR_PHI] = BUSTER_IR_UNROLL_PHI | BUSTER_IR_UNROLL_FREE,
    [IR_BR] = BUSTER_IR_UNROLL_EFFECT | BUSTER_IR_UNROLL_FREE,
    [IR_CBR] = BUSTER_IR_UNROLL_EFFECT,
    [IR_SWITCH] = BUSTER_IR_UNROLL_CONTROL | BUSTER_IR_UNROLL_EFFECT,
    [IR_BR_INDIRECT] = BUSTER_IR_UNROLL_CONTROL | BUSTER_IR_UNROLL_EFFECT,
    [IR_RET] = BUSTER_IR_UNROLL_EXIT | BUSTER_IR_UNROLL_EFFECT,
    [IR_UNREACHABLE] = BUSTER_IR_UNROLL_EXIT | BUSTER_IR_UNROLL_EFFECT,
    [IR_ALLOCA] = BUSTER_IR_UNROLL_STACK | BUSTER_IR_UNROLL_EFFECT,
    [IR_STACK_SAVE] = BUSTER_IR_UNROLL_STACK | BUSTER_IR_UNROLL_EFFECT,
    [IR_STACK_RESTORE] = BUSTER_IR_UNROLL_STACK | BUSTER_IR_UNROLL_EFFECT,
};

// Every instruction's class, and whether the function has a phi: without one no loop has an induction, and nothing
// else is built.
static bool ir_unroll_classify(const IrFunction* f, uint8_t* classes, bool scalar)
{
    const uint32_t count = f->instruction_count;
    bool phis = false;
#if BUSTER_HOST_VECTOR
    if (!scalar)
    {
        const __m512i low = _mm512_loadu_si512(ir_unroll_classes);
        const __m512i high = _mm512_loadu_si512(ir_unroll_classes + 64);
        uint64_t found = 0;
        for (uint64_t first = 0; first < count; first += BUSTER_IR_UNROLL_BATCH)
        {
            const uint64_t lanes = count - first < BUSTER_IR_UNROLL_BATCH ? count - first : BUSTER_IR_UNROLL_BATCH;
            const __mmask64 valid = UINT64_MAX >> (BUSTER_IR_UNROLL_BATCH - lanes);
            const __m512i operations = _mm512_maskz_loadu_epi8(valid, f->op + first);
            const __m512i looked = _mm512_maskz_permutex2var_epi8(valid, low, operations, high);
            _mm512_mask_storeu_epi8(classes + first, valid, looked);
            found |= _mm512_test_epi8_mask(looked, _mm512_set1_epi8(BUSTER_IR_UNROLL_PHI));
        }
        phis = found != 0;
    }
    else
#endif
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            classes[i] = ir_unroll_classes[f->op[i]];
            phis = phis || (classes[i] & BUSTER_IR_UNROLL_PHI) != 0;
        }
    }
    (void)scalar;
    return phis;
}

// The union of the classes of instructions [first, end).
static uint32_t ir_unroll_block_classes(const uint8_t* classes, uint32_t first, uint32_t end, bool scalar)
{
    uint32_t result = 0;
#if BUSTER_HOST_VECTOR
    if (!scalar)
    {
        __m512i any = _mm512_setzero_si512();
        for (uint64_t at = first; at < end; at += BUSTER_IR_UNROLL_BATCH)
        {
            const uint64_t lanes = end - at < BUSTER_IR_UNROLL_BATCH ? end - at : BUSTER_IR_UNROLL_BATCH;
            any = _mm512_or_si512(any, _mm512_maskz_loadu_epi8(UINT64_MAX >> (BUSTER_IR_UNROLL_BATCH - lanes), classes + at));
        }
        uint64_t word = (uint64_t)_mm512_reduce_or_epi64(any);
        word |= word >> 32;
        word |= word >> 16;
        word |= word >> 8;
        result = (uint32_t)(word & 0xff);
    }
    else
#endif
    {
        for (uint32_t at = first; at < end; at += 1)
        {
            result |= classes[at];
        }
    }
    (void)scalar;
    return result;
}

static uint64_t ir_unroll_mask(uint32_t type)
{
    const uint32_t bits = ir_type_bits[type];
    return bits >= 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
}

static bool ir_unroll_integer(uint32_t type)
{
    return type == IR_I8 || type == IR_I16 || type == IR_I32 || type == IR_I64;
}

// Value `v`'s bits where it is a constant, read through copies, extensions and truncations (the lowering's `sext
// (const 1)` steps, which the optimizer folds only later).
static bool ir_unroll_constant(const IrFunction* f, uint32_t v, uint64_t* value)
{
    uint32_t chain[BUSTER_IR_UNROLL_CONSTANT_LINKS];
    uint32_t length = 0;
    uint32_t at = v;
    bool walking = true;
    while (walking && length < BUSTER_IR_UNROLL_CONSTANT_LINKS)
    {
        chain[length] = at;
        length += 1;
        const uint32_t op = f->op[at];
        walking = (op == IR_COPY || op == IR_ZEXT || op == IR_SEXT || op == IR_TRUNC) && ir_unroll_integer(f->type[at]) && f->a[at] < f->instruction_count;
        at = walking ? f->a[at] : at;
    }
    const uint32_t root = chain[length - 1];
    bool found = f->op[root] == IR_CONST && ir_unroll_integer(f->type[root]) && f->a[root] < f->constant_count;
    uint64_t bits = found ? f->constants[f->a[root]].low & ir_unroll_mask(f->type[root]) : 0;
    for (uint32_t k = length - 1; found && k != 0; k -= 1)
    {
        const uint32_t step = chain[k - 1];
        const uint32_t source_bits = ir_type_bits[f->type[chain[k]]];
        const uint64_t mask = ir_unroll_mask(f->type[step]);
        const bool negative = source_bits < 64 && ((bits >> (source_bits - 1)) & 1) != 0;
        const uint64_t extended = f->op[step] == IR_SEXT && negative ? bits | ~((UINT64_C(1) << source_bits) - 1) : bits;
        bits = extended & mask;
    }
    *value = bits;
    return found;
}

// The predicate p(b, a) for p(a, b), and the predicate of !p(a, b).
static uint32_t ir_unroll_mirror(uint32_t predicate)
{
    static const uint8_t mirrored[IR_PREDICATE_COUNT] = {
        [IR_EQ] = IR_EQ,
        [IR_NE] = IR_NE,
        [IR_LT] = IR_GT,
        [IR_LE] = IR_GE,
        [IR_GT] = IR_LT,
        [IR_GE] = IR_LE,
        [IR_ULT] = IR_UGT,
        [IR_ULE] = IR_UGE,
        [IR_UGT] = IR_ULT,
        [IR_UGE] = IR_ULE,
    };
    return predicate < IR_PREDICATE_COUNT ? mirrored[predicate] : IR_PREDICATE_COUNT;
}

static uint32_t ir_unroll_inverse(uint32_t predicate)
{
    static const uint8_t inverted[IR_PREDICATE_COUNT] = {
        [IR_EQ] = IR_NE,
        [IR_NE] = IR_EQ,
        [IR_LT] = IR_GE,
        [IR_LE] = IR_GT,
        [IR_GT] = IR_LE,
        [IR_GE] = IR_LT,
        [IR_ULT] = IR_UGE,
        [IR_ULE] = IR_UGT,
        [IR_UGT] = IR_ULE,
        [IR_UGE] = IR_ULT,
    };
    return predicate < IR_PREDICATE_COUNT ? inverted[predicate] : IR_PREDICATE_COUNT;
}

// Whether `block` is one of loop `loop`'s.
static bool ir_unroll_inside(const IrUnrollWork* work, uint32_t loop, uint32_t block)
{
    return block < work->function->block_count && dominators_loop_contains(&work->loops, loop, block);
}

static bool ir_unroll_value_inside(const IrUnrollWork* work, uint32_t loop, uint32_t value)
{
    return value < work->function->instruction_count && ir_unroll_inside(work, loop, work->cfg.instruction_blocks[value]);
}

// The identity of an operation of `type`, and of a minimum or maximum by its combining predicate.
static uint64_t ir_unroll_identity(uint32_t op, uint32_t predicate, uint32_t type)
{
    const uint64_t mask = ir_unroll_mask(type);
    const uint64_t sign = (mask >> 1) + 1;
    uint64_t identity = 0;
    if (op == IR_MUL)
    {
        identity = 1;
    }
    else if (op == IR_AND)
    {
        identity = mask;
    }
    else if (op == IR_SELECT)
    {
        // The combining predicate picks the new accumulator where it is greater (a maximum) or less (a minimum).
        identity = predicate == IR_GT ? sign : predicate == IR_LT ? sign - 1 : predicate == IR_UGT ? 0 : mask;
    }
    return identity;
}

static bool ir_unroll_reducible_op(uint32_t op)
{
    return op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_AND || op == IR_OR || op == IR_XOR;
}

// Whether `v` is an update op(r, x) or op(x, r) of phi `r` (r - x alone for a subtraction), read only by `readers`.
static bool ir_unroll_update(const IrUnrollWork* work, uint32_t r, uint32_t v, uint32_t readers)
{
    const IrFunction* f = work->function;
    const uint32_t op = f->op[v];
    const bool left = f->a[v] == r && f->b[v] != r;
    const bool right = f->b[v] == r && f->a[v] != r && op != IR_SUB;
    return ir_unroll_reducible_op(op) && f->type[v] == f->type[r] && (left || right) && work->uses[v] == readers;
}

// Header phi `r`, whose latch value is `v`: an integer reduction the copies may split, its combining operation (an
// arithmetic one, or IR_SELECT for a minimum or maximum by `predicate`), and its identity. The loop reads r and its
// update nowhere else, so no other value depends on a partial result.
static bool ir_unroll_reduction(const IrUnrollWork* work, uint32_t r, uint32_t v, uint8_t* combine, uint8_t* predicate, uint64_t* identity)
{
    const IrFunction* f = work->function;
    const uint32_t type = f->type[r];
    bool found = false;
    uint32_t op = IR_OP_COUNT;
    uint32_t order = IR_PREDICATE_COUNT;
    if (ir_unroll_integer(type) && v < f->instruction_count && v != r && f->type[v] == type && work->uses[v] == 1)
    {
        if (ir_unroll_reducible_op(f->op[v]) && work->uses[r] == 1 && ir_unroll_update(work, r, v, 1))
        {
            found = true;
            op = f->op[v] == IR_SUB ? IR_ADD : f->op[v];
        }
        else if (f->op[v] == IR_SELECT && work->uses[r] == 2)
        {
            const uint32_t condition = f->a[v];
            const uint32_t taken = f->b[v];
            const uint32_t other = f->c[v];
            const uint32_t update = other == r ? taken : taken == r ? other : BUSTER_IR_NONE;
            if (update != BUSTER_IR_NONE && update != r && ir_unroll_update(work, r, update, 1))
            {
                // select(c, op(r, x), r): op(r, c ? x : identity), exact in any order.
                found = true;
                op = f->op[update] == IR_SUB ? IR_ADD : f->op[update];
            }
            else if (update != BUSTER_IR_NONE && update != r && f->op[condition] == IR_ICMP && work->uses[condition] == 1 &&
                     ((f->a[condition] == r && f->b[condition] == update) || (f->b[condition] == r && f->a[condition] == update)))
            {
                // select(p(r, x), x, r) or select(p(r, x), r, x): a minimum or a maximum of r and x.
                const uint32_t compared = f->a[condition] == r ? f->c[condition] : ir_unroll_mirror(f->c[condition]);
                const bool picks_new = taken == update;
                const bool less = compared == IR_LT || compared == IR_LE || compared == IR_ULT || compared == IR_ULE;
                const bool greater = compared == IR_GT || compared == IR_GE || compared == IR_UGT || compared == IR_UGE;
                const bool is_signed = compared == IR_LT || compared == IR_LE || compared == IR_GT || compared == IR_GE;
                const bool maximum = (less && picks_new) || (greater && !picks_new);
                found = less || greater;
                op = IR_SELECT;
                order = maximum ? (is_signed ? IR_GT : IR_UGT) : (is_signed ? IR_LT : IR_ULT);
            }
        }
    }
    if (found)
    {
        *combine = (uint8_t)op;
        *predicate = (uint8_t)order;
        *identity = ir_unroll_identity(op, order, type);
    }
    return found;
}

// Each loop instruction's value operands, through `visit` for operand values inside loop `loop`: the uses count, or the
// liveness walk's push.
static uint32_t ir_unroll_operands(const IrFunction* f, uint32_t i, uint32_t* operands)
{
    uint32_t count = 0;
    const uint32_t mask = ir_value_mask(f, i);
    if ((mask & 1) != 0)
    {
        operands[count++] = f->a[i];
    }
    if ((mask & 2) != 0)
    {
        operands[count++] = f->b[i];
    }
    if ((mask & 4) != 0)
    {
        operands[count++] = f->c[i];
    }
    for (uint32_t k = 0, extra = ir_extra_value_count(f, i); k < extra; k += 1)
    {
        operands[count++] = ir_extra_value(f, i, k);
    }
    return count;
}

// The estimated cost of one copy of loop `loop`'s body: the instructions a copy keeps alive (its effects, its traps,
// its internal branches' conditions and the next copy's phi inputs, but not its exit test, which becomes a jump), less
// the free ones. Also each instruction's uses inside the loop, for the reductions.
static uint32_t ir_unroll_cost(IrUnrollWork* work, uint32_t loop, const uint32_t* list, uint32_t block_count, uint32_t exiting)
{
    const IrFunction* f = work->function;
    uint32_t pushed = 0;
    for (uint32_t t = 0; t < block_count; t += 1)
    {
        const uint32_t block = list[t];
        for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
        {
            uint32_t operands[5];
            const uint32_t count = f->op[i] == IR_PHI ? 0 : ir_unroll_operands(f, i, operands);
            for (uint32_t k = 0; k < count; k += 1)
            {
                work->uses[operands[k]] += ir_unroll_value_inside(work, loop, operands[k]) ? 1 : 0;
            }
            for (uint32_t p = 0; f->op[i] == IR_PHI && p < f->b[i]; p += 1)
            {
                const uint32_t predecessor = f->extra[f->a[i] + p * 2];
                const uint32_t value = f->extra[f->a[i] + p * 2 + 1];
                const bool edge = ir_unroll_inside(work, loop, predecessor);
                work->uses[value] += edge && ir_unroll_value_inside(work, loop, value) ? 1 : 0;
            }
            const bool observable = (f->op[i] == IR_LOAD || f->op[i] == IR_MASKED_LOAD || f->op[i] == IR_GATHER) && (f->flags[i] & BUSTER_IR_OBSERVABLE) != 0;
            const bool test = block == exiting && i + 1 == f->block_offsets[(uint64_t)block + 1];
            const bool root = ((work->classes[i] & (BUSTER_IR_UNROLL_EFFECT | BUSTER_IR_UNROLL_PHI)) != 0 || observable) && !test;
            if (root && work->live[i] == 0)
            {
                work->live[i] = 1;
                work->stack[pushed++] = i;
            }
        }
    }
    uint32_t cost = 0;
    while (pushed != 0)
    {
        pushed -= 1;
        const uint32_t i = work->stack[pushed];
        cost += (work->classes[i] & BUSTER_IR_UNROLL_FREE) != 0 ? 0 : 1;
        uint32_t operands[5];
        const uint32_t count = f->op[i] == IR_PHI ? 0 : ir_unroll_operands(f, i, operands);
        for (uint32_t k = 0; k < count; k += 1)
        {
            if (ir_unroll_value_inside(work, loop, operands[k]) && work->live[operands[k]] == 0)
            {
                work->live[operands[k]] = 1;
                work->stack[pushed++] = operands[k];
            }
        }
        for (uint32_t p = 0; f->op[i] == IR_PHI && p < f->b[i]; p += 1)
        {
            const uint32_t value = f->extra[f->a[i] + p * 2 + 1];
            if (ir_unroll_inside(work, loop, f->extra[f->a[i] + p * 2]) && ir_unroll_value_inside(work, loop, value) && work->live[value] == 0)
            {
                work->live[value] = 1;
                work->stack[pushed++] = value;
            }
        }
    }
    return cost;
}

// The incoming value of phi `phi` from `predecessor`, or BUSTER_IR_NONE.
static uint32_t ir_unroll_incoming(const IrFunction* f, uint32_t phi, uint32_t predecessor)
{
    uint32_t value = BUSTER_IR_NONE;
    for (uint32_t p = 0; p < f->b[phi]; p += 1)
    {
        value = f->extra[f->a[phi] + p * 2] == predecessor ? f->extra[f->a[phi] + p * 2 + 1] : value;
    }
    return value;
}

// The instructions a loop's preheader gains before its new branch: the bound's constant made again where it is one, and
// the first test, start < n.
static uint32_t ir_unroll_preheader_size(const IrUnrollLoop* loop)
{
    return 1u + (loop->bound_constant ? 1u : 0u);
}

// MP's instructions: K', n - start and its test, the main bound n - K', each split reduction's identity, and its branch.
static uint32_t ir_unroll_main_entry_size(const IrUnrollLoop* loop)
{
    return 4u + loop->reductions + 1u;
}

// The constants a loop's rewrite adds: the bound's, K', the identities, the copies' induction offsets and the main step.
static uint32_t ir_unroll_constant_count(const IrUnrollLoop* loop)
{
    return 1u + loop->reductions + loop->factor + (loop->bound_constant ? 1u : 0u);
}

// Loop `loop`'s recognition: its outcome, and on IR_UNROLL_UNROLLED its plan in *plan and its blocks appended to the
// work's lists. `inner` says whether another loop's parent it is; `remaining` is the function's growth left.
static IrUnrollRefusal ir_unroll_loop(IrUnrollWork* work, uint32_t loop, bool inner, uint32_t remaining, IrUnrollOptions options, IrUnrollLoop* plan)
{
    const IrFunction* f = work->function;
    const IrCfg* cfg = &work->cfg;
    const uint32_t header = work->loops.loop_header[loop];
    IrUnrollRefusal outcome = IR_UNROLL_UNROLLED;
    IrUnrollLoop result = { .header = header, .preheader = BUSTER_IR_NONE, .latch = BUSTER_IR_NONE, .exiting = BUSTER_IR_NONE };
    if (inner)
    {
        outcome = IR_UNROLL_OUTER;
    }
    else if (work->loops.loop_irreducible[loop] != 0)
    {
        outcome = IR_UNROLL_IRREDUCIBLE;
    }
    // The preheader: the one reachable outside predecessor, whose only successor is the header by a `br`; the latch: the
    // one predecessor inside.
    uint32_t outside = 0;
    uint32_t latches = 0;
    for (uint32_t edge = cfg->predecessor_offsets[header]; outcome == IR_UNROLL_UNROLLED && edge < cfg->predecessor_offsets[(uint64_t)header + 1]; edge += 1)
    {
        const uint32_t predecessor = cfg->predecessors[edge];
        if (ir_unroll_inside(work, loop, predecessor))
        {
            latches += 1;
            result.latch = predecessor;
        }
        else if (work->loops.rpo_index[predecessor] != BUSTER_DOMINATOR_NONE)
        {
            outside += 1;
            result.preheader = predecessor;
        }
    }
    if (outcome == IR_UNROLL_UNROLLED && (outside != 1 || f->op[f->block_offsets[(uint64_t)result.preheader + 1] - 1] != IR_BR))
    {
        outcome = IR_UNROLL_PREHEADER;
    }
    else if (outcome == IR_UNROLL_UNROLLED && latches != 1)
    {
        outcome = IR_UNROLL_LATCH;
    }
    // The loop's blocks, header first, their classes and the exit edges.
    const uint32_t list = work->list_count;
    uint32_t block_count = 0;
    uint32_t instructions = 0;
    uint32_t classes = 0;
    uint32_t exits = 0;
    bool labeled = false;
    if (outcome == IR_UNROLL_UNROLLED)
    {
        work->lists[list] = header;
        block_count = 1;
        const uint64_t* row = work->loops.loop_bodies + (uint64_t)loop * work->loops.row_words;
        for (uint64_t word = 0; word < work->loops.row_words; word += 1)
        {
            uint64_t bits = row[word];
            while (bits != 0)
            {
                const uint32_t block = (uint32_t)(word * 64 + (uint64_t)__builtin_ctzll(bits));
                bits &= bits - 1;
                if (block != header)
                {
                    work->lists[list + block_count] = block;
                    block_count += 1;
                }
            }
        }
        for (uint32_t t = 0; t < block_count; t += 1)
        {
            const uint32_t block = work->lists[list + t];
            const uint32_t first = f->block_offsets[block];
            const uint32_t end = f->block_offsets[(uint64_t)block + 1];
            instructions += end - first;
            classes |= ir_unroll_block_classes(work->classes, first, end, BUSTER_SCALAR_PATH(options.scalar));
            labeled = labeled || work->labeled[block] != 0;
            for (uint32_t edge = cfg->successor_offsets[block]; edge < cfg->successor_offsets[(uint64_t)block + 1]; edge += 1)
            {
                if (!ir_unroll_inside(work, loop, cfg->successors[edge]))
                {
                    exits += 1;
                    result.exiting = block;
                }
            }
        }
        outcome = (classes & BUSTER_IR_UNROLL_CALL) != 0               ? IR_UNROLL_CALL_REFUSED :
                  (classes & BUSTER_IR_UNROLL_CONTROL) != 0 || labeled ? IR_UNROLL_CONTROL_REFUSED :
                  (classes & BUSTER_IR_UNROLL_STACK) != 0              ? IR_UNROLL_STACK_REFUSED :
                  (classes & BUSTER_IR_UNROLL_EXIT) != 0 || exits != 1 || (result.exiting != header && result.exiting != result.latch) ? IR_UNROLL_EXIT :
                  instructions > BUSTER_IR_UNROLL_LOOP_LIMIT                                                                       ? IR_UNROLL_SIZE :
                                                                                                                                    IR_UNROLL_UNROLLED;
    }
    // The exit test: an icmp of the induction (or its increment) against a value from outside the loop.
    uint32_t test = BUSTER_IR_NONE;
    uint32_t predicate = IR_PREDICATE_COUNT;
    uint32_t compared = BUSTER_IR_NONE;
    if (outcome == IR_UNROLL_UNROLLED)
    {
        const uint32_t branch = f->block_offsets[(uint64_t)result.exiting + 1] - 1;
        const bool split = f->op[branch] == IR_CBR && ir_unroll_inside(work, loop, f->b[branch]) != ir_unroll_inside(work, loop, f->c[branch]);
        test = split ? f->a[branch] : BUSTER_IR_NONE;
        outcome = test != BUSTER_IR_NONE && f->op[test] == IR_ICMP && ir_unroll_integer(f->type[f->a[test]]) ? IR_UNROLL_UNROLLED : IR_UNROLL_TEST;
        if (outcome == IR_UNROLL_UNROLLED)
        {
            const bool continues = ir_unroll_inside(work, loop, f->b[branch]);
            const uint32_t left = f->a[test];
            const uint32_t right = f->b[test];
            // A constant is invariant wherever the lowering defined it: the preheader makes it again.
            uint64_t left_value = 0;
            uint64_t right_value = 0;
            const bool left_constant = ir_unroll_constant(f, left, &left_value);
            const bool right_constant = ir_unroll_constant(f, right, &right_value);
            const bool left_inside = !left_constant && ir_unroll_value_inside(work, loop, left);
            const bool right_inside = !right_constant && ir_unroll_value_inside(work, loop, right);
            predicate = continues ? f->c[test] : ir_unroll_inverse(f->c[test]);
            if (!left_constant && left_inside && !right_inside)
            {
                compared = left;
                result.bound = right;
                result.bound_constant = right_constant;
                result.bound_value = right_value;
            }
            else if (!right_constant && right_inside && !left_inside)
            {
                compared = right;
                result.bound = left;
                result.bound_constant = left_constant;
                result.bound_value = left_value;
                predicate = ir_unroll_mirror(predicate);
            }
            outcome = compared == BUSTER_IR_NONE ? IR_UNROLL_BOUND :
                      predicate != IR_ULT && predicate != IR_ULE && predicate != IR_LT && predicate != IR_LE ? IR_UNROLL_PREDICATE :
                                                                                                              IR_UNROLL_UNROLLED;
        }
    }
    // The induction: a header phi whose latch value adds it a positive constant step, compared itself or plus a constant
    // c >= 0 (its increment, c the step, or another sum, as `i + 1 < n`): iteration k returns while i + k * step + c
    // passes, so `distance` is c.
    uint32_t increment = BUSTER_IR_NONE;
    uint64_t distance = 0;
    if (outcome == IR_UNROLL_UNROLLED)
    {
        uint32_t phi_end = f->block_offsets[header];
        while (f->op[phi_end] == IR_PHI)
        {
            phi_end += 1;
        }
        result.phi_end = phi_end;
        // The compared value's phi and constant: the value itself, or one operand of its add.
        uint32_t base = compared;
        uint64_t added_constant = 0;
        bool offset_known = true;
        if (compared != BUSTER_IR_NONE && f->op[compared] == IR_ADD && ir_unroll_constant(f, f->b[compared], &added_constant))
        {
            base = f->a[compared];
        }
        else if (compared != BUSTER_IR_NONE && f->op[compared] == IR_ADD && ir_unroll_constant(f, f->a[compared], &added_constant))
        {
            base = f->b[compared];
        }
        for (uint32_t phi = f->block_offsets[header]; phi < phi_end; phi += 1)
        {
            if (phi == base)
            {
                result.induction = phi;
                increment = ir_unroll_incoming(f, phi, result.latch);
                distance = added_constant;
            }
        }
        const uint32_t type = increment != BUSTER_IR_NONE ? f->type[result.induction] : IR_VOID;
        uint64_t step = 0;
        const uint32_t added = increment != BUSTER_IR_NONE && f->op[increment] == IR_ADD && f->type[increment] == type ? (f->a[increment] == result.induction ? f->b[increment] : f->b[increment] == result.induction ? f->a[increment] : BUSTER_IR_NONE) : BUSTER_IR_NONE;
        const bool constant = added != BUSTER_IR_NONE && ir_unroll_constant(f, added, &step);
        const uint64_t mask = ir_unroll_mask(type);
        const uint64_t sign = (mask >> 1) + 1;
        const bool positive = constant && step != 0 && (step & sign) == 0 && step <= BUSTER_IR_UNROLL_STEP_LIMIT;
        // The compared sum's constant: of the induction's type, at least 0 and at most the step limit.
        offset_known = (compared == result.induction || f->type[compared] == type) && (distance & sign) == 0 && distance <= BUSTER_IR_UNROLL_STEP_LIMIT;
        outcome = ir_unroll_integer(type) && ir_unroll_value_inside(work, loop, increment) && positive && offset_known ? IR_UNROLL_UNROLLED : IR_UNROLL_INDUCTION_REFUSED;
        result.type = (uint8_t)type;
        result.step = step;
        result.start = outcome == IR_UNROLL_UNROLLED ? ir_unroll_incoming(f, result.induction, result.preheader) : BUSTER_IR_NONE;
        result.predicate = predicate == IR_ULT || predicate == IR_ULE ? IR_ULT : IR_LT;
    }
    // The factor whose copies fit the budget and whose offset the type holds.
    uint32_t cost = 0;
    if (outcome == IR_UNROLL_UNROLLED)
    {
        cost = ir_unroll_cost(work, loop, work->lists + list, block_count, result.exiting);
        uint32_t factor = 2;
        while (factor * 2 <= options.factor && (uint64_t)factor * 2 * (cost == 0 ? 1 : cost) <= options.budget)
        {
            factor *= 2;
        }
        const uint64_t mask = ir_unroll_mask(result.type);
        const uint64_t limit = result.predicate == IR_LT ? mask >> 1 : mask;
        const bool strict = predicate == IR_ULT || predicate == IR_LT;
        uint64_t offset = ((uint64_t)factor - 1) * result.step + distance - (strict ? 0 : 1);
        while (factor > 2 && offset > limit)
        {
            factor /= 2;
            offset = ((uint64_t)factor - 1) * result.step + distance - (strict ? 0 : 1);
        }
        result.factor = (uint8_t)factor;
        result.offset = offset;
        outcome = options.factor < 2 || (uint64_t)2 * (cost == 0 ? 1 : cost) > options.budget ? IR_UNROLL_SIZE : offset > limit ? IR_UNROLL_INDUCTION_REFUSED : IR_UNROLL_UNROLLED;
    }
    // The addresses whose index is the induction (or its extension, which doesn't wrap in the main loop): a copy's
    // offset from the main induction joins their displacement. And the general values the main loop reads from outside.
    uint32_t invariants = 0;
    if (outcome == IR_UNROLL_UNROLLED)
    {
        const uint32_t extension = result.predicate == IR_LT ? IR_SEXT : IR_ZEXT;
        for (uint32_t t = 0; t < block_count; t += 1)
        {
            const uint32_t block = work->lists[list + t];
            for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
            {
                const uint32_t index = f->op[i] == IR_ADDRESS ? f->b[i] : BUSTER_IR_NONE;
                const bool phi = index == result.induction && result.type == IR_I64;
                const bool extended = index != BUSTER_IR_NONE && !phi && f->op[index] == extension && f->a[index] == result.induction && f->type[index] == IR_I64 &&
                                      ir_unroll_value_inside(work, loop, index) && dominators_dominates(&work->loops, work->cfg.instruction_blocks[index], result.latch);
                work->folds[i] = phi ? BUSTER_IR_UNROLL_FOLD_PHI : extended ? BUSTER_IR_UNROLL_FOLD_EXTENSION : 0;
                result.addresses += work->folds[i] != 0 ? 1u : 0u;
                uint32_t operands[5];
                const uint32_t operand_count = f->op[i] == IR_PHI ? 0 : ir_unroll_operands(f, i, operands);
                for (uint32_t k = 0; k < operand_count; k += 1)
                {
                    const uint32_t value = operands[k];
                    const uint32_t op = f->op[value];
                    const bool general = ir_unroll_integer(f->type[value]) || f->type[value] == IR_PTR;
                    const bool remade = op == IR_CONST || op == IR_SYMBOL_ADDR || op == IR_STACK_ADDR || op == IR_UNDEF;
                    // Counted at most once: the mark is the live byte's second bit, which nothing in the loop reads.
                    if (general && !remade && !ir_unroll_value_inside(work, loop, value) && (work->live[value] & 2u) == 0)
                    {
                        work->live[value] |= 2u;
                        invariants += 1;
                    }
                }
            }
        }
        // Outside values' marks cleared, so that another loop counts them again.
        for (uint32_t t = 0; t < block_count; t += 1)
        {
            const uint32_t block = work->lists[list + t];
            for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
            {
                uint32_t operands[5];
                const uint32_t operand_count = f->op[i] == IR_PHI ? 0 : ir_unroll_operands(f, i, operands);
                for (uint32_t k = 0; k < operand_count; k += 1)
                {
                    work->live[operands[k]] &= (uint8_t)~2u;
                }
            }
        }
    }
    // The header's phis' roles, with as many accumulators as the main loop's general values leave registers for.
    if (outcome == IR_UNROLL_UNROLLED)
    {
        uint32_t general_phis = 0;
        uint32_t candidates = 0;
        for (uint32_t phi = f->block_offsets[header]; phi < result.phi_end; phi += 1)
        {
            uint8_t combine = 0;
            uint8_t order = 0;
            uint64_t identity = 0;
            const bool general = ir_unroll_integer(f->type[phi]) || f->type[phi] == IR_PTR;
            general_phis += general ? 1u : 0u;
            candidates += phi != result.induction && ir_unroll_reduction(work, phi, ir_unroll_incoming(f, phi, result.latch), &combine, &order, &identity) ? 1u : 0u;
        }
        uint32_t accumulators = options.accumulators < result.factor ? options.accumulators : result.factor;
        accumulators = accumulators < 1 ? 1 : accumulators;
        while (accumulators > 1 && general_phis + candidates * (accumulators - 1u) + invariants + 1u > BUSTER_IR_UNROLL_PRESSURE)
        {
            accumulators /= 2;
        }
        result.pressure = general_phis + candidates * (accumulators - 1u) + invariants + 1u;
        result.accumulators = (uint8_t)accumulators;
        result.main_phis = 0;
        result.combines = 0;
        result.reductions = 0;
        for (uint32_t phi = f->block_offsets[header]; phi < result.phi_end; phi += 1)
        {
            const uint32_t value = ir_unroll_incoming(f, phi, result.latch);
            uint8_t combine = 0;
            uint8_t order = 0;
            uint64_t identity = 0;
            if (phi == result.induction)
            {
                work->roles[phi] = BUSTER_IR_UNROLL_INDUCTION;
                result.main_phis += 1;
            }
            else if (result.accumulators > 1 && ir_unroll_reduction(work, phi, value, &combine, &order, &identity))
            {
                work->roles[phi] = BUSTER_IR_UNROLL_REDUCTION;
                work->combine_ops[phi] = combine;
                work->combine_predicates[phi] = order;
                work->identities[phi] = identity;
                result.main_phis += result.accumulators;
                result.combines += (result.accumulators - 1u) * (combine == IR_SELECT ? 2u : 1u);
                result.reductions += 1;
            }
            else
            {
                work->roles[phi] = BUSTER_IR_UNROLL_CARRIED;
                result.main_phis += 1;
            }
        }
        // P's bound, guard and identities; the copies; the header copies' induction; copy `factor`'s test; MX.
        const uint32_t phis = result.phi_end - f->block_offsets[header];
        const uint64_t preheader = (uint64_t)ir_unroll_preheader_size(&result) + ir_unroll_main_entry_size(&result);
        const uint64_t copies = (uint64_t)result.factor * (instructions - phis) + result.main_phis + 2u * (result.factor - 1u) + 3u;
        const uint64_t exit = (uint64_t)result.main_phis + result.combines + 1u;
        const uint64_t added = preheader + copies + exit;
        result.added = added <= UINT32_MAX ? (uint32_t)added : UINT32_MAX;
        outcome = added > remaining ? IR_UNROLL_GROWTH_REFUSED : IR_UNROLL_UNROLLED;
    }
    if (outcome == IR_UNROLL_UNROLLED)
    {
        result.list = list;
        result.block_count = block_count;
        result.instructions = instructions;
        work->list_count += block_count;
        for (uint32_t t = 0; t < block_count; t += 1)
        {
            work->positions[work->lists[list + t]] = t;
        }
        *plan = result;
    }
    return outcome;
}

// The fixed operands of instructions [first, first + count) of `f`, through `map` where they are values, into `a`, `b`
// and `c` from `out`; their opcode, type, flags and alignment copied. Phis', calls' and extra values' operands, which
// the extra array holds, are the caller's.
static void ir_unroll_remap(const IrFunction* f, uint32_t first, uint32_t count, const uint32_t* map, IrFunction* view, uint32_t out, bool scalar)
{
    memcpy(view->op + out, f->op + first, count);
    memcpy(view->type + out, f->type + first, count);
    memcpy(view->flags + out, f->flags + first, count);
    memcpy(view->alignment_log2 + out, f->alignment_log2 + first, count);
#if BUSTER_HOST_VECTOR
    if (!scalar && f->instruction_count <= INT32_MAX)
    {
        const __m512i none = _mm512_set1_epi32(-1);
        const __m512i one = _mm512_set1_epi32(1);
        const __m512i two = _mm512_set1_epi32(2);
        const __m512i four = _mm512_set1_epi32(4);
        for (uint32_t at = 0; at < count; at += 16)
        {
            const uint32_t lanes = count - at < 16 ? count - at : 16;
            const __mmask16 active = (__mmask16)((1u << lanes) - 1u);
            const __m128i operations = _mm_maskz_loadu_epi8(active, f->op + first + at);
            const __m512i operands = ir_lookup_bytes(ir_operand_table, operations);
            const __m512i optional = ir_lookup_bytes(ir_optional_table, operations);
            const __m512i a = _mm512_maskz_loadu_epi32(active, f->a + first + at);
            const __m512i b = _mm512_maskz_loadu_epi32(active, f->b + first + at);
            const __m512i c = _mm512_maskz_loadu_epi32(active, f->c + first + at);
            // ir_value_mask: an opcode's operands, and its optional ones where they aren't BUSTER_IR_NONE.
            const __mmask16 use_a = active & (_mm512_test_epi32_mask(operands, one) | (_mm512_test_epi32_mask(optional, one) & _mm512_cmpneq_epi32_mask(a, none)));
            const __mmask16 use_b = active & (_mm512_test_epi32_mask(operands, two) | (_mm512_test_epi32_mask(optional, two) & _mm512_cmpneq_epi32_mask(b, none)));
            const __mmask16 use_c = active & (_mm512_test_epi32_mask(operands, four) | (_mm512_test_epi32_mask(optional, four) & _mm512_cmpneq_epi32_mask(c, none)));
            _mm512_mask_storeu_epi32(view->a + out + at, active, _mm512_mask_i32gather_epi32(a, use_a, a, map, 4));
            _mm512_mask_storeu_epi32(view->b + out + at, active, _mm512_mask_i32gather_epi32(b, use_b, b, map, 4));
            _mm512_mask_storeu_epi32(view->c + out + at, active, _mm512_mask_i32gather_epi32(c, use_c, c, map, 4));
        }
    }
    else
#endif
    {
        for (uint32_t k = 0; k < count; k += 1)
        {
            const uint32_t i = first + k;
            const uint32_t mask = ir_value_mask(f, i);
            view->a[out + k] = (mask & 1) != 0 ? map[f->a[i]] : f->a[i];
            view->b[out + k] = (mask & 2) != 0 ? map[f->b[i]] : f->b[i];
            view->c[out + k] = (mask & 4) != 0 ? map[f->c[i]] : f->c[i];
        }
    }
    (void)scalar;
}

// The rewrite's state: the new function's columns and the next free extra word, call and constant.
typedef struct IrUnrollEmit IrUnrollEmit;
struct IrUnrollEmit
{
    IrFunction* view;
    uint32_t extra;
    uint32_t call;
    uint32_t constant;
    uint32_t address;
};

static void ir_unroll_put(IrUnrollEmit* emit, uint32_t at, uint32_t op, uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    IrFunction* view = emit->view;
    view->op[at] = (uint8_t)op;
    view->type[at] = (uint8_t)type;
    view->flags[at] = 0;
    view->alignment_log2[at] = 0;
    view->a[at] = a;
    view->b[at] = b;
    view->c[at] = c;
}

static void ir_unroll_put_constant(IrUnrollEmit* emit, uint32_t at, uint32_t type, uint64_t value)
{
    emit->view->constants[emit->constant] = (IrConstant){ .low = value & ir_unroll_mask(type), .high = 0 };
    ir_unroll_put(emit, at, IR_CONST, type, emit->constant, BUSTER_IR_NONE, BUSTER_IR_NONE);
    emit->constant += 1;
}

// A phi of two incoming values at `at`.
static void ir_unroll_put_phi(IrUnrollEmit* emit, uint32_t at, uint32_t type, uint32_t first_block, uint32_t first_value, uint32_t second_block, uint32_t second_value)
{
    uint32_t* extra = emit->view->extra + emit->extra;
    extra[0] = first_block;
    extra[1] = first_value;
    extra[2] = second_block;
    extra[3] = second_value;
    ir_unroll_put(emit, at, IR_PHI, type, emit->extra, 2, BUSTER_IR_NONE);
    emit->extra += 4;
}

// The extra-array operands of the instructions [first, end) of `f`, now at [out, ...): a phi's incoming pairs (its
// predecessors through `blocks`, NONE dropping a pair), a call's arguments and an instruction's extra values, through
// `map`, each in a slice of its own past the input's words, which keep their places (a switch's and an indirect branch's
// lists).
static void ir_unroll_extras(IrUnrollEmit* emit, const IrFunction* f, uint32_t first, uint32_t end, uint32_t out, const uint32_t* map, const uint32_t* blocks)
{
    IrFunction* view = emit->view;
    for (uint32_t i = first; i < end; i += 1)
    {
        const uint32_t at = out + (i - first);
        if (f->op[i] == IR_PHI)
        {
            view->a[at] = emit->extra;
            view->b[at] = 0;
            for (uint32_t p = 0; p < f->b[i]; p += 1)
            {
                const uint32_t predecessor = blocks != NULL ? blocks[f->extra[f->a[i] + p * 2]] : f->extra[f->a[i] + p * 2];
                if (predecessor != BUSTER_IR_NONE)
                {
                    view->extra[emit->extra++] = predecessor;
                    view->extra[emit->extra++] = map[f->extra[f->a[i] + p * 2 + 1]];
                    view->b[at] += 1;
                }
            }
        }
        else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
        {
            const IrCall call = f->calls[f->b[i]];
            view->b[at] = emit->call;
            view->calls[emit->call] = call;
            view->calls[emit->call].argument_first = emit->extra;
            for (uint32_t k = 0; k < call.argument_count; k += 1)
            {
                view->extra[emit->extra++] = map[f->extra[call.argument_first + k]];
            }
            emit->call += 1;
        }
        else if (ir_extra_value_count(f, i) != 0)
        {
            view->c[at] = ir_intrinsic_ternary_c(f->c[i], emit->extra);
            for (uint32_t k = 0, values = ir_extra_value_count(f, i); k < values; k += 1)
            {
                view->extra[emit->extra++] = map[ir_extra_value(f, i, k)];
            }
        }
    }
}

// The extra words instructions [first, end) of `f` take in slices of their own.
static uint64_t ir_unroll_extra_words(const IrFunction* f, uint32_t first, uint32_t end)
{
    uint64_t words = 0;
    for (uint32_t i = first; i < end; i += 1)
    {
        words += f->op[i] == IR_PHI ? (uint64_t)f->b[i] * 2 : f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT ? f->calls[f->b[i]].argument_count : ir_extra_value_count(f, i);
    }
    return words;
}

// A loop's new blocks: MP (its first), the block of copy `copy` (0-based) of the loop's block at list index `t`, and MX.
static uint32_t ir_unroll_entry_block(const IrUnrollLoop* loop)
{
    return loop->first_block;
}

static uint32_t ir_unroll_copy_block(const IrUnrollLoop* loop, uint32_t copy, uint32_t t)
{
    return loop->first_block + 1u + copy * loop->block_count + t;
}

static uint32_t ir_unroll_exit_block(const IrUnrollLoop* loop)
{
    return loop->first_block + 1u + (uint32_t)loop->factor * loop->block_count;
}

// The instructions a new block of `loop` holds: copy `copy`'s block `t`, or MX for copy `factor`.
static uint32_t ir_unroll_block_size(const IrFunction* f, const IrUnrollLoop* loop, const uint32_t* lists, uint32_t copy, uint32_t t)
{
    uint32_t size = loop->main_phis + loop->combines + 1u;
    if (copy < loop->factor)
    {
        const uint32_t block = lists[loop->list + t];
        size = f->block_offsets[(uint64_t)block + 1] - f->block_offsets[block];
        size -= t == 0 ? loop->phi_end - f->block_offsets[block] : 0;
        size += t == 0 ? (copy == 0 ? loop->main_phis : 2u) : 0;
        size += block == loop->latch && copy + 1u == loop->factor ? 3u : 0;
    }
    return size;
}

// Where copy `copy` of loop instruction `i` (not a header phi) goes: its block copy's prefix (the main phis in MH, the
// induction's constant and add in another header copy), its place, and a terminator last.
static uint32_t ir_unroll_clone(const IrUnrollWork* work, const IrUnrollLoop* loop, const uint32_t* offsets, uint32_t copy, uint32_t i)
{
    const IrFunction* f = work->function;
    const uint32_t block = work->cfg.instruction_blocks[i];
    const uint32_t t = work->positions[block];
    const uint32_t out_block = ir_unroll_copy_block(loop, copy, t);
    const uint32_t first = t == 0 ? loop->phi_end : f->block_offsets[block];
    const uint32_t end = f->block_offsets[(uint64_t)block + 1];
    const uint32_t prefix = t == 0 ? (copy == 0 ? loop->main_phis : 2u) : 0u;
    return i + 1u == end ? offsets[(uint64_t)out_block + 1] - 1u : offsets[out_block] + prefix + (i - first);
}

// One admitted loop's part of the rewrite, after the original blocks: its preheader's additions and branch, its header's
// phis entered from MX, then MH and the copies, and MX. `map` holds `values` on entry and on return; `blocks_map` is
// scratch of a word per original block.
static IrError ir_unroll_rewrite_loop(IrUnrollWork* work, const IrUnrollLoop* loop, IrUnrollEmit* emit, const uint32_t* offsets, const uint32_t* values, uint32_t* map, uint32_t* blocks_map, Arena* scratch, bool scalar)
{
    const IrFunction* const f = work->function;
    IrFunction* view = emit->view;
    const uint64_t mark = scratch->length;
    const uint32_t header = loop->header;
    const uint32_t entry_block = ir_unroll_entry_block(loop);
    const uint32_t main_block = ir_unroll_copy_block(loop, 0, 0);
    const uint32_t exit_block = ir_unroll_exit_block(loop);
    const uint32_t main_latch = ir_unroll_copy_block(loop, loop->factor - 1u, work->positions[loop->latch]);
    const uint32_t type = loop->type;
    const uint32_t phi_first = f->block_offsets[header];
    const uint32_t phis = loop->phi_end - phi_first;
    const uint32_t accumulators_per_phi = loop->accumulators;
    bool ok = true;
    // The main phis at MH's start and MX's, in the header phis' order, a split reduction's accumulators together; each
    // header phi's value in the copy being made, and each accumulator's latest value.
    uint32_t* main_phis = ir_allocate(scratch, phis, 4, &ok);
    uint32_t* exit_phis = ir_allocate(scratch, phis, 4, &ok);
    uint32_t* next = ir_allocate(scratch, phis, 4, &ok);
    uint32_t* accumulators = ir_allocate(scratch, (uint64_t)phis * accumulators_per_phi, 4, &ok);
    uint32_t* identities = ir_allocate(scratch, phis, 4, &ok);
    const IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    if (error == IR_OK)
    {
        uint32_t slot = 0;
        for (uint32_t j = 0; j < phis; j += 1)
        {
            main_phis[j] = offsets[main_block] + slot;
            exit_phis[j] = offsets[exit_block] + slot;
            slot += work->roles[phi_first + j] == BUSTER_IR_UNROLL_REDUCTION ? accumulators_per_phi : 1u;
            for (uint32_t a = 0; a < accumulators_per_phi; a += 1)
            {
                accumulators[(uint64_t)j * accumulators_per_phi + a] = main_phis[j] + a;
            }
        }
        const uint32_t induction = main_phis[loop->induction - phi_first];
        // The preheader's additions: the bound (made again where it is a constant) and the first test, start < n, which a
        // loop of no iteration fails at once, to the original loop.
        const uint32_t preheader_end = offsets[(uint64_t)loop->preheader + 1] - 1u;
        uint32_t at = preheader_end - ir_unroll_preheader_size(loop);
        uint32_t bound = BUSTER_IR_NONE;
        if (loop->bound_constant)
        {
            bound = at;
            ir_unroll_put_constant(emit, at++, type, loop->bound_value);
        }
        else
        {
            bound = values[loop->bound];
        }
        const uint32_t start = values[loop->start];
        const uint32_t first_test = at;
        ir_unroll_put(emit, at++, IR_ICMP, IR_I1, start, bound, loop->predicate);
        ir_unroll_put(emit, preheader_end, IR_CBR, IR_VOID, first_test, entry_block, header);
        // MP: with start < n, the `factor` iterations all return exactly when n - start, a true difference, exceeds K';
        // then n - K' doesn't wrap, and the main loop's test is next < n - K'.
        at = offsets[entry_block];
        const uint32_t offset_constant = at;
        ir_unroll_put_constant(emit, at++, type, loop->offset);
        const uint32_t difference = at;
        ir_unroll_put(emit, at++, IR_SUB, type, bound, start, BUSTER_IR_NONE);
        const uint32_t enough = at;
        ir_unroll_put(emit, at++, IR_ICMP, IR_I1, difference, offset_constant, IR_UGT);
        const uint32_t main_bound = at;
        ir_unroll_put(emit, at++, IR_SUB, type, bound, offset_constant, BUSTER_IR_NONE);
        for (uint32_t j = 0; j < phis; j += 1)
        {
            if (work->roles[phi_first + j] == BUSTER_IR_UNROLL_REDUCTION)
            {
                identities[j] = at;
                ir_unroll_put_constant(emit, at++, f->type[phi_first + j], work->identities[phi_first + j]);
            }
        }
        ir_unroll_put(emit, at, IR_CBR, IR_VOID, enough, main_block, exit_block);
        for (uint32_t b = 0; b < f->block_count; b += 1)
        {
            blocks_map[b] = BUSTER_IR_NONE;
        }
        // The copies. Copy 0's header values are the main phis; copy k's, what copy k - 1 passes on its back edge, but
        // the induction's, i + k * step from the main phi, and a split reduction's, its accumulator's latest value.
        uint32_t increment = BUSTER_IR_NONE;
        for (uint32_t copy = 0; copy < loop->factor; copy += 1)
        {
            const uint32_t header_block = ir_unroll_copy_block(loop, copy, 0);
            for (uint32_t j = 0; j < phis; j += 1)
            {
                const uint32_t phi = phi_first + j;
                const uint32_t role = work->roles[phi];
                const uint32_t accumulator = accumulators[(uint64_t)j * accumulators_per_phi + copy % accumulators_per_phi];
                next[j] = copy == 0                              ? main_phis[j] :
                          role == BUSTER_IR_UNROLL_INDUCTION      ? offsets[header_block] + 1u :
                          role == BUSTER_IR_UNROLL_REDUCTION      ? accumulator :
                                                                    map[ir_unroll_incoming(f, phi, loop->latch)];
            }
            for (uint32_t j = 0; j < phis; j += 1)
            {
                map[phi_first + j] = next[j];
            }
            if (copy != 0)
            {
                ir_unroll_put_constant(emit, offsets[header_block], type, (uint64_t)copy * loop->step);
                ir_unroll_put(emit, offsets[header_block] + 1u, IR_ADD, type, induction, offsets[header_block], BUSTER_IR_NONE);
            }
            // This copy's blocks and its instructions' indices: a header copy's after its prefix, each terminator last.
            for (uint32_t t = 0; t < loop->block_count; t += 1)
            {
                blocks_map[work->lists[loop->list + t]] = ir_unroll_copy_block(loop, copy, t);
            }
            for (uint32_t t = 0; t < loop->block_count; t += 1)
            {
                const uint32_t block = work->lists[loop->list + t];
                const uint32_t first = t == 0 ? loop->phi_end : f->block_offsets[block];
                for (uint32_t i = first; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
                {
                    map[i] = ir_unroll_clone(work, loop, offsets, copy, i);
                }
            }
            for (uint32_t t = 0; t < loop->block_count; t += 1)
            {
                const uint32_t block = work->lists[loop->list + t];
                const uint32_t out_block = ir_unroll_copy_block(loop, copy, t);
                const uint32_t first = t == 0 ? loop->phi_end : f->block_offsets[block];
                const uint32_t end = f->block_offsets[(uint64_t)block + 1];
                const uint32_t prefix = t == 0 ? (copy == 0 ? loop->main_phis : 2u) : 0u;
                const uint32_t terminator = offsets[(uint64_t)out_block + 1] - 1u;
                const uint32_t branch = end - 1u;
                ir_unroll_remap(f, first, branch - first, map, view, offsets[out_block] + prefix, scalar);
                ir_unroll_remap(f, branch, 1, map, view, terminator, scalar);
                ir_unroll_extras(emit, f, first, branch, offsets[out_block] + prefix, map, blocks_map);
                for (uint32_t i = first; view->debug != NULL && i < end; i += 1)
                {
                    view->debug->origins[map[i]] = f->debug->origins[i];
                }
                // An address of the induction's copy: the main induction (or copy 0's extension of it) and the copy's
                // offset in the displacement, exact since no copy's induction wraps.
                for (uint32_t i = first; copy != 0 && i < branch; i += 1)
                {
                    if (work->folds[i] != 0)
                    {
                        const uint32_t out = map[i];
                        const IrAddress address = f->addresses[f->c[i]];
                        const uint64_t shift = (uint64_t)copy * loop->step * address.scale;
                        view->b[out] = work->folds[i] == BUSTER_IR_UNROLL_FOLD_PHI ? induction : ir_unroll_clone(work, loop, offsets, 0, f->b[i]);
                        view->addresses[emit->address] = (IrAddress){
                            .offset = (int64_t)((uint64_t)address.offset + shift),
                            .scale = address.scale,
                        };
                        view->c[out] = emit->address;
                        emit->address += 1;
                    }
                }
                // The terminator: the latch's goes on to the next copy or, in the last, tests for another main
                // iteration; the exit test's goes on into the loop; any other's stays in this copy.
                if (block == loop->latch && copy + 1u == loop->factor)
                {
                    const uint32_t step_at = terminator - 3u;
                    ir_unroll_put_constant(emit, step_at, type, (uint64_t)loop->factor * loop->step);
                    ir_unroll_put(emit, step_at + 1u, IR_ADD, type, induction, step_at, BUSTER_IR_NONE);
                    ir_unroll_put(emit, step_at + 2u, IR_ICMP, IR_I1, step_at + 1u, main_bound, loop->predicate);
                    ir_unroll_put(emit, terminator, IR_CBR, IR_VOID, step_at + 2u, main_block, exit_block);
                    increment = step_at + 1u;
                }
                else if (block == loop->latch)
                {
                    ir_unroll_put(emit, terminator, IR_BR, IR_VOID, ir_unroll_copy_block(loop, copy + 1u, 0), BUSTER_IR_NONE, BUSTER_IR_NONE);
                }
                else if (block == loop->exiting)
                {
                    const uint32_t target = blocks_map[f->b[branch]] != BUSTER_IR_NONE ? blocks_map[f->b[branch]] : blocks_map[f->c[branch]];
                    ir_unroll_put(emit, terminator, IR_BR, IR_VOID, target, BUSTER_IR_NONE, BUSTER_IR_NONE);
                }
                else if (f->op[branch] == IR_BR)
                {
                    view->a[terminator] = blocks_map[f->a[branch]];
                }
                else
                {
                    view->b[terminator] = blocks_map[f->b[branch]];
                    view->c[terminator] = blocks_map[f->c[branch]];
                }
            }
            // A split reduction's accumulator takes this copy's update.
            for (uint32_t j = 0; j < phis; j += 1)
            {
                const uint32_t phi = phi_first + j;
                if (work->roles[phi] == BUSTER_IR_UNROLL_REDUCTION)
                {
                    accumulators[(uint64_t)j * accumulators_per_phi + copy % accumulators_per_phi] = map[ir_unroll_incoming(f, phi, loop->latch)];
                }
            }
        }
        // The main phis and MX's: from P the entry values (a split reduction's other accumulators its identity), from
        // the main latch what the last copy passes on. MX combines each split reduction's accumulators in order.
        uint32_t exit_at = offsets[exit_block] + loop->main_phis;
        for (uint32_t j = 0; j < phis; j += 1)
        {
            const uint32_t phi = phi_first + j;
            const uint32_t role = work->roles[phi];
            const uint32_t phi_type = f->type[phi];
            const uint32_t entry = values[ir_unroll_incoming(f, phi, loop->preheader)];
            uint32_t combined = exit_phis[j];
            if (role == BUSTER_IR_UNROLL_REDUCTION)
            {
                for (uint32_t a = 0; a < accumulators_per_phi; a += 1)
                {
                    const uint32_t initial = a == 0 ? entry : identities[j];
                    const uint32_t last = accumulators[(uint64_t)j * accumulators_per_phi + a];
                    ir_unroll_put_phi(emit, main_phis[j] + a, phi_type, entry_block, initial, main_latch, last);
                    ir_unroll_put_phi(emit, exit_phis[j] + a, phi_type, entry_block, initial, main_latch, last);
                }
                for (uint32_t a = 1; a < accumulators_per_phi; a += 1)
                {
                    const uint32_t operand = exit_phis[j] + a;
                    if (work->combine_ops[phi] == IR_SELECT)
                    {
                        ir_unroll_put(emit, exit_at, IR_ICMP, IR_I1, operand, combined, work->combine_predicates[phi]);
                        ir_unroll_put(emit, exit_at + 1u, IR_SELECT, phi_type, exit_at, operand, combined);
                        combined = exit_at + 1u;
                        exit_at += 2u;
                    }
                    else
                    {
                        ir_unroll_put(emit, exit_at, work->combine_ops[phi], phi_type, combined, operand, BUSTER_IR_NONE);
                        combined = exit_at;
                        exit_at += 1u;
                    }
                }
            }
            else
            {
                const uint32_t last = role == BUSTER_IR_UNROLL_INDUCTION ? increment : map[ir_unroll_incoming(f, phi, loop->latch)];
                ir_unroll_put_phi(emit, main_phis[j], phi_type, entry_block, entry, main_latch, last);
                ir_unroll_put_phi(emit, exit_phis[j], phi_type, entry_block, entry, main_latch, last);
            }
            // The header's phi again, in a slice of its own: its pairs as they were, P's among them, and MX's.
            const uint32_t out = values[phi];
            const uint32_t pairs = view->b[out];
            const uint32_t old_first = view->a[out];
            view->a[out] = emit->extra;
            for (uint32_t p = 0; p < pairs * 2u; p += 1)
            {
                view->extra[emit->extra++] = view->extra[old_first + p];
            }
            view->extra[emit->extra++] = exit_block;
            view->extra[emit->extra++] = combined;
            view->b[out] = pairs + 1u;
        }
        ir_unroll_put(emit, exit_at, IR_BR, IR_VOID, header, BUSTER_IR_NONE, BUSTER_IR_NONE);
        // Another loop's copies read this loop's values as the original's.
        for (uint32_t t = 0; t < loop->block_count; t += 1)
        {
            const uint32_t block = work->lists[loop->list + t];
            for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
            {
                map[i] = values[i];
            }
        }
        work->statistics.copies += loop->factor;
        work->statistics.reductions += loop->reductions;
    }
    arena_pop_to(scratch, mark);
    return error;
}

// The rewritten function: original blocks in place, the preheaders' additions, then each loop's new blocks.
static IrError ir_unroll_rewrite(IrUnrollWork* work, Arena* output, Arena* scratch, bool scalar, IrFunction* result)
{
    const IrFunction* const f = work->function;
    const uint64_t mark = output->length;
    const uint32_t count = f->instruction_count;
    const uint32_t blocks = f->block_count;
    bool ok = true;
    // Each preheader's additions, each new block's first block, and the totals.
    uint32_t* inserts = ir_allocate(scratch, blocks, 4, &ok);
    uint64_t new_blocks = 0;
    uint64_t constants = 0;
    uint64_t addresses = 0;
    for (uint32_t k = 0; ok && k < work->accepted_count; k += 1)
    {
        IrUnrollLoop* loop = &work->accepted[k];
        loop->first_block = (uint32_t)(blocks + new_blocks);
        new_blocks += (uint64_t)loop->factor * loop->block_count + 2u;
        constants += ir_unroll_constant_count(loop);
        addresses += (uint64_t)(loop->factor - 1u) * loop->addresses;
        inserts[loop->preheader] = ir_unroll_preheader_size(loop);
    }
    const uint64_t total_blocks = blocks + new_blocks;
    ok = ok && total_blocks < UINT32_MAX && constants + f->constant_count <= UINT32_MAX && addresses + f->address_count <= UINT32_MAX;
    uint32_t* offsets = ir_allocate(scratch, total_blocks + 1, 4, &ok);
    uint64_t total = 0;
    uint64_t extras = f->extra_count;
    for (uint32_t b = 0; ok && b < blocks; b += 1)
    {
        offsets[b] = (uint32_t)total;
        total += (uint64_t)f->block_offsets[(uint64_t)b + 1] - f->block_offsets[b] + inserts[b];
        ok = total < UINT32_MAX;
    }
    extras += ok ? ir_unroll_extra_words(f, 0, count) : 0;
    for (uint32_t k = 0; ok && k < work->accepted_count; k += 1)
    {
        const IrUnrollLoop* loop = &work->accepted[k];
        uint64_t loop_extra = 0;
        for (uint32_t t = 0; t < loop->block_count; t += 1)
        {
            const uint32_t block = work->lists[loop->list + t];
            loop_extra += ir_unroll_extra_words(f, t == 0 ? loop->phi_end : f->block_offsets[block], f->block_offsets[(uint64_t)block + 1]);
        }
        // The main phis and MX's, two pairs each, and the header's phis again with MX's pair.
        uint64_t header_words = 0;
        for (uint32_t i = f->block_offsets[loop->header]; i < loop->phi_end; i += 1)
        {
            header_words += ((uint64_t)f->b[i] + 1u) * 2u;
        }
        extras += loop_extra * loop->factor + (uint64_t)loop->main_phis * 8u + header_words;
        offsets[ir_unroll_entry_block(loop)] = (uint32_t)total;
        total += ir_unroll_main_entry_size(loop);
        for (uint32_t copy = 0; copy <= loop->factor; copy += 1)
        {
            for (uint32_t t = 0; t < (copy < loop->factor ? loop->block_count : 1u); t += 1)
            {
                const uint32_t block = copy < loop->factor ? ir_unroll_copy_block(loop, copy, t) : ir_unroll_exit_block(loop);
                offsets[block] = (uint32_t)total;
                total += ir_unroll_block_size(f, loop, work->lists, copy, t);
            }
        }
        ok = total < UINT32_MAX;
    }
    ok = ok && extras <= UINT32_MAX;
    if (ok)
    {
        offsets[total_blocks] = (uint32_t)total;
    }
    IrError error = ok ? IR_OK : IR_ERROR_LIMIT;
    uint32_t* values = ir_allocate(scratch, count, 4, &ok);
    uint32_t* map = ir_allocate(scratch, count, 4, &ok);
    uint32_t* blocks_map = ir_allocate(scratch, blocks, 4, &ok);
    IrFunction view = *f;
    view.instruction_count = (uint32_t)total;
    view.block_count = (uint32_t)total_blocks;
    view.op = ir_allocate(output, total, 1, &ok);
    view.type = ir_allocate(output, total, 1, &ok);
    view.flags = ir_allocate(output, total, 1, &ok);
    view.alignment_log2 = ir_allocate(output, total, 1, &ok);
    view.a = ir_allocate(output, total, 4, &ok);
    view.b = ir_allocate(output, total, 4, &ok);
    view.c = ir_allocate(output, total, 4, &ok);
    view.block_offsets = ir_allocate(output, total_blocks + 1, 4, &ok);
    view.extra = ir_allocate(output, extras, 4, &ok);
    view.calls = ir_allocate(output, f->call_count, sizeof(IrCall), &ok);
    view.constants = ir_allocate(output, (uint64_t)f->constant_count + constants, sizeof(IrConstant), &ok);
    view.constant_count = (uint32_t)((uint64_t)f->constant_count + constants);
    view.addresses = addresses != 0 ? ir_allocate(output, (uint64_t)f->address_count + addresses, sizeof(IrAddress), &ok) : f->addresses;
    view.address_count = (uint32_t)((uint64_t)f->address_count + addresses);
    error = error == IR_OK && !ok ? IR_ERROR_MEMORY : error;
    if (error == IR_OK && !ir_debug_create(output, &view, f->debug))
    {
        error = IR_ERROR_MEMORY;
    }
    if (error == IR_OK)
    {
        memcpy(view.block_offsets, offsets, (total_blocks + 1) * 4);
        if (f->extra_count != 0)
        {
            memcpy(view.extra, f->extra, (uint64_t)f->extra_count * 4);
        }
        if (f->constant_count != 0)
        {
            memcpy(view.constants, f->constants, (uint64_t)f->constant_count * sizeof(IrConstant));
        }
        if (addresses != 0 && f->address_count != 0)
        {
            memcpy(view.addresses, f->addresses, (uint64_t)f->address_count * sizeof(IrAddress));
        }
        // The original instructions' new indices: a preheader's terminator after its additions.
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            const uint32_t first = f->block_offsets[b];
            const uint32_t end = f->block_offsets[(uint64_t)b + 1];
            for (uint32_t i = first; i < end; i += 1)
            {
                values[i] = i + 1 == end ? offsets[(uint64_t)b + 1] - 1 : offsets[b] + (i - first);
            }
        }
        ir_debug_remap(f, &view, values, NULL, NULL);
        memcpy(map, values, (uint64_t)count * 4);
        IrUnrollEmit emit = {
            .view = &view,
            .extra = f->extra_count,
            .call = 0,
            .constant = f->constant_count,
            .address = f->address_count,
        };
        // The original blocks, as they were but for the preheaders' terminators and the headers' phis' entries.
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            const uint32_t first = f->block_offsets[b];
            const uint32_t end = f->block_offsets[(uint64_t)b + 1];
            ir_unroll_remap(f, first, end - first - 1, values, &view, offsets[b], scalar);
            ir_unroll_remap(f, end - 1, 1, values, &view, offsets[(uint64_t)b + 1] - 1, scalar);
            ir_unroll_extras(&emit, f, first, end - 1, offsets[b], values, NULL);
            ir_unroll_extras(&emit, f, end - 1, end, offsets[(uint64_t)b + 1] - 1, values, NULL);
        }
        for (uint32_t k = 0; error == IR_OK && k < work->accepted_count; k += 1)
        {
            error = ir_unroll_rewrite_loop(work, &work->accepted[k], &emit, offsets, values, map, blocks_map, scratch, scalar);
        }
        view.extra_count = emit.extra;
        view.call_count = emit.call;
        view.constant_count = emit.constant;
        view.address_count = emit.address;
        view.arena_bytes = output->length - mark;
        work->statistics.added = (uint32_t)(total - count);
        if (error == IR_OK)
        {
            *result = view;
        }
    }
    if (error != IR_OK)
    {
        arena_pop_to(output, mark);
    }
    return error;
}

static __attribute__((unused)) IrCheck ir_optimize_unroll(const IrFunction* function, Arena* output, Arena* scratch, IrUnrollOptions options, IrUnrollResult* result)
{
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    const bool scalar = BUSTER_SCALAR_PATH(options.scalar);
    IrUnrollWork work = { .function = function };
    work.statistics.function = *function;
    IrCheck check = ir_check(output != scratch ? IR_OK : IR_ERROR_STRUCTURE, BUSTER_IR_NONE, 0);
    const uint32_t count = function->instruction_count;
    const uint32_t blocks = function->block_count;
    bool ok = check.error == IR_OK;
    work.classes = ir_allocate(scratch, count, 1, &ok);
    check.error = check.error == IR_OK && !ok ? IR_ERROR_MEMORY : check.error;
    const bool phis = check.error == IR_OK && count != 0 && ir_unroll_classify(function, work.classes, scalar);
    if (check.error == IR_OK && phis)
    {
        check.error = ir_cfg_validated(function, scratch, scratch, &work.cfg);
    }
    if (check.error == IR_OK && phis)
    {
        const DominatorCfg cfg = {
            .block_count = work.cfg.block_count,
            .edge_count = work.cfg.edge_count,
            .entry = blocks == 0 ? BUSTER_DOMINATOR_NONE : 0,
            .successor_offsets = work.cfg.successor_offsets,
            .successors = work.cfg.successors,
            .predecessor_offsets = work.cfg.predecessor_offsets,
            .predecessors = work.cfg.predecessors,
        };
        DominatorOptions dom = dominators_options();
        dom.outputs = BUSTER_DOMINATOR_LOOPS;
        dom.path = scalar ? DOMINATOR_SCALAR : DOMINATOR_AUTO;
        const DominatorStatus status = dominators_analyze(scratch, output, &cfg, dom, &work.loops);
        // A function past the analysis' limits is left as it is.
        check.error = status == DOMINATOR_OK ? IR_OK : status == DOMINATOR_NO_MEMORY ? IR_ERROR_MEMORY : IR_ERROR_UNSUPPORTED;
    }
    if (check.error == IR_OK && phis && work.loops.loop_count != 0)
    {
        work.labeled = ir_allocate(scratch, blocks, 1, &ok);
        work.positions = ir_allocate(scratch, blocks, 4, &ok);
        work.uses = ir_allocate(scratch, count, 4, &ok);
        work.live = ir_allocate(scratch, count, 1, &ok);
        work.roles = ir_allocate(scratch, count, 1, &ok);
        work.combine_ops = ir_allocate(scratch, count, 1, &ok);
        work.combine_predicates = ir_allocate(scratch, count, 1, &ok);
        work.identities = ir_allocate(scratch, count, 8, &ok);
        work.folds = ir_allocate(scratch, count, 1, &ok);
        work.stack = ir_allocate(scratch, count, 4, &ok);
        work.lists = ir_allocate(scratch, blocks, 4, &ok);
        work.accepted = ir_allocate(scratch, work.loops.loop_count, sizeof(IrUnrollLoop), &ok);
        uint8_t* parents = ir_allocate(scratch, work.loops.loop_count, 1, &ok);
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
        for (uint32_t label = 0; check.error == IR_OK && label < function->label_count; label += 1)
        {
            if (function->labels[label] < blocks)
            {
                work.labeled[function->labels[label]] = 1;
            }
        }
        for (uint32_t loop = 0; check.error == IR_OK && loop < work.loops.loop_count; loop += 1)
        {
            const uint32_t parent = work.loops.loop_parent[loop];
            if (parent < work.loops.loop_count)
            {
                parents[parent] = 1;
            }
        }
        if (check.error == IR_OK)
        {
            memset(work.positions, 0xff, (uint64_t)blocks * 4);
        }
        uint32_t remaining = options.growth;
        for (uint32_t loop = 0; check.error == IR_OK && loop < work.loops.loop_count; loop += 1)
        {
            IrUnrollLoop plan = { 0 };
            const IrUnrollRefusal outcome = ir_unroll_loop(&work, loop, parents[loop] != 0, remaining, options, &plan);
            work.statistics.outcomes[outcome] += 1;
            if (outcome == IR_UNROLL_UNROLLED)
            {
                work.accepted[work.accepted_count] = plan;
                work.accepted_count += 1;
                remaining -= plan.added;
            }
        }
    }
    if (check.error == IR_OK && work.accepted_count != 0)
    {
        IrFunction rewritten = { 0 };
        check.error = ir_unroll_rewrite(&work, output, scratch, scalar, &rewritten);
        work.statistics.function = rewritten;
    }
    if (check.error == IR_OK || check.error == IR_ERROR_UNSUPPORTED)
    {
        *result = work.statistics;
        result->function = check.error == IR_OK ? work.statistics.function : *function;
    }
    if (check.error != IR_OK)
    {
        arena_pop_to(output, om);
    }
    arena_pop_to(scratch, sm);
    return check;
}

#endif
