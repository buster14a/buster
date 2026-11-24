// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Integer truth values compared with zero (#1218) or their true encoding (#3036). Include after ir.c. The C driver
// simplifies its owned, verified SSA before the optimizer builds use lists or publishes an analysis revision. Only an ICMP's operands
// and predicate change: blocks, instruction identities, types and side tables remain unchanged. No memory operation
// moves or is duplicated; a comparison keeps referring to the same SSA values, including values read by observable
// loads. SCCP/DCE subsequently remove the now-unused comparison, extensions and constants.
//
// The candidate scan is 64 bounded opcode/type bytes at a time. The dependent walks run
// only for equality/inequality tests, through copies and zero/sign extensions, to a constant and another ICMP.
// False stays zero. True has low one bits up to the source width of the first zero extension, or the full width if
// there is none. Truncations, other constant encodings, floating comparisons and arbitrary producers do not fold.
// A bounded number of links prevents pathological copy chains from making this pass quadratic.
//
// ir_truth_simplify mutates exclusively owned, already verified columns. No use lists or verified-analysis handles
// may be live across this call. Input columns are disjoint, have exactly instruction_count elements with no padding,
// and remain allocated throughout. The caller runs the ordinary optimizer and final SSA verifier afterwards.
//
// ir_truth_simplify_values, which if-conversion runs on its rounds' own columns (#1382), also takes the truth values
// it makes: a select of truth values tested against zero, and a test of an extended i1 value that is no comparison.
// The test itself becomes, in its row and type, an i1 and, or, select or copy: `select c, (ext x), 0` tested nonzero is
// `and c, x` (c && x), `select c, 1, (ext y)` is `or c, y` (c || y), `select c, (ext x), (ext y)` is
// `select c, x, y`, and `select c, 1, 0` is c (tested equal to zero, `select c, 0, 1` is c too); `(ext v)` tested
// nonzero is v. A form that would need c's or a value's complement stays. Tier 1 computes an i1 and or or as its 32-bit
// operation (#2677), as clang's `&&` and `||` values are, where an i1 select is a test and a cmov. The operation and its
// operands change, and nothing else does.

#ifndef BUSTER_IR_TRUTH_C
#define BUSTER_IR_TRUTH_C

#define BUSTER_IR_TRUTH_BATCH 64u
#define BUSTER_IR_TRUTH_LINK_LIMIT 32u

_Static_assert(IR_PREDICATE_COUNT == 10, "new integer predicates need a truth-complement rule");
static const uint8_t ir_truth_inverse[IR_PREDICATE_COUNT] = {
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

typedef struct IrTruthSource IrTruthSource;
struct IrTruthSource
{
    uint32_t value;
    // The last value reached by sign extension alone from the source, ignoring copies.
    uint32_t sign_end;
};

static IrTruthSource ir_truth_unextend(const IrFunction* function, uint32_t value)
{
    const uint32_t count = function->instruction_count;
    const uint8_t* const operations = function->op;
    const uint32_t* const inputs = function->a;
    uint32_t sign_end = value;
    for (uint32_t link = 0; value < count && link < BUSTER_IR_TRUTH_LINK_LIMIT; link += 1)
    {
        const uint32_t operation = operations[value];
        if (operation != IR_COPY && operation != IR_ZEXT && operation != IR_SEXT)
        {
            break;
        }
        value = inputs[value];
        if (operation == IR_ZEXT)
        {
            sign_end = value;
        }
    }
    return (IrTruthSource){ .value = value, .sign_end = sign_end };
}

static bool ir_truth_zero(const IrFunction* function, uint32_t value)
{
    value = ir_truth_unextend(function, value).value;
    bool zero = false;
    if (value < function->instruction_count && function->op[value] == IR_CONST)
    {
        const IrConstant constant = function->constants[function->a[value]];
        zero = constant.low == 0 && constant.high == 0;
    }
    return zero;
}

// Match the constant at its own width. If its sign bit must be one, its leading ones must extend exactly as far
// as the comparison's do. Otherwise its sign bit is zero and the rest of its extensions add only zeros.
static bool ir_truth_matches_true(const IrFunction* function, IrTruthSource comparison, IrTruthSource constant, IrConstant bits)
{
    uint32_t ones = ir_type_bits[function->type[comparison.sign_end]];
    const uint32_t width = ir_type_bits[function->type[constant.value]];
    bool possible = true;
    if (ones >= width)
    {
        possible = ones == ir_type_bits[function->type[constant.sign_end]];
        ones = width;
    }
    const unsigned __int128 mask = ~(unsigned __int128)0 >> (128 - ones);
    return possible && bits.low == (uint64_t)mask && bits.high == (uint64_t)(mask >> 64);
}

static bool ir_truth_rewrite(IrFunction* function, uint32_t root)
{
    const uint32_t predicate = function->c[root];
    bool changed = false;
    if (predicate == IR_EQ || predicate == IR_NE)
    {
        const IrTruthSource left = ir_truth_unextend(function, function->a[root]);
        const IrTruthSource right = ir_truth_unextend(function, function->b[root]);
        const bool constant_left = function->op[left.value] == IR_CONST;
        const IrTruthSource constant = constant_left ? left : right;
        const IrTruthSource comparison = constant_left ? right : left;
        const uint32_t value = comparison.value;
        if (function->op[constant.value] == IR_CONST && value != root && function->op[value] == IR_ICMP)
        {
            const IrConstant bits = function->constants[function->a[constant.value]];
            const bool zero = bits.low == 0 && bits.high == 0;
            if (zero || ir_truth_matches_true(function, comparison, constant, bits))
            {
                const uint32_t inner_left = function->a[value];
                const uint32_t inner_right = function->b[value];
                const uint32_t inner_predicate = function->c[value];
                const bool invert = predicate == (zero ? IR_EQ : IR_NE);
                function->a[root] = inner_left;
                function->b[root] = inner_right;
                function->c[root] = invert ? ir_truth_inverse[inner_predicate] : inner_predicate;
                changed = true;
            }
        }
    }
    return changed;
}

// A select arm's truth: 1 a zero constant, 2 a nonzero one, 3 an i1 value (*value), 0 none, through extensions.
static uint32_t ir_truth_kind(const IrFunction* function, uint32_t value, uint32_t* truth)
{
    const uint32_t inner = ir_truth_unextend(function, value).value;
    uint32_t kind = 0;
    if (inner < function->instruction_count && function->op[inner] == IR_CONST)
    {
        const IrConstant constant = function->constants[function->a[inner]];
        kind = constant.low == 0 && constant.high == 0 ? 1 : 2;
    }
    else if (inner < function->instruction_count && function->type[inner] == IR_I1)
    {
        kind = 3;
        *truth = inner;
    }
    return kind;
}

static bool ir_truth_rewrite_value(IrFunction* function, uint32_t root)
{
    const uint32_t predicate = function->c[root];
    bool changed = false;
    if (predicate == IR_EQ || predicate == IR_NE)
    {
        const uint32_t left = function->a[root];
        const uint32_t right = function->b[root];
        const uint32_t operand = ir_truth_zero(function, right) ? left : ir_truth_zero(function, left) ? right : BUSTER_IR_NONE;
        const uint32_t value = ir_truth_unextend(function, operand).value;
        uint32_t op = IR_OP_COUNT;
        uint32_t a = BUSTER_IR_NONE;
        uint32_t b = BUSTER_IR_NONE;
        uint32_t c = BUSTER_IR_NONE;
        if (value < function->instruction_count && value != root && function->op[value] == IR_SELECT)
        {
            const uint32_t condition = function->a[value];
            uint32_t x = BUSTER_IR_NONE;
            uint32_t y = BUSTER_IR_NONE;
            const uint32_t kx = ir_truth_kind(function, function->b[value], &x);
            const uint32_t ky = ir_truth_kind(function, function->c[value], &y);
            const uint32_t shape = kx << 2 | ky;
            const bool nonzero = predicate == IR_NE;
            op = nonzero && shape == (3u << 2 | 1u) ? IR_AND : nonzero && shape == (2u << 2 | 3u) ? IR_OR : nonzero && shape == (3u << 2 | 3u) ? IR_SELECT :
                 (nonzero && shape == (2u << 2 | 1u)) || (!nonzero && shape == (1u << 2 | 2u)) ? IR_COPY : IR_OP_COUNT;
            a = condition;
            b = op == IR_AND || op == IR_SELECT ? x : op == IR_OR ? y : BUSTER_IR_NONE;
            c = op == IR_SELECT ? y : BUSTER_IR_NONE;
        }
        else if (value < function->instruction_count && value != root && function->op[value] != IR_ICMP && function->type[value] == IR_I1 && predicate == IR_NE)
        {
            op = IR_COPY;
            a = value;
        }
        changed = op != IR_OP_COUNT && a != root && b != root && c != root;
        if (changed)
        {
            function->op[root] = (uint8_t)op;
            function->a[root] = a;
            function->b[root] = b;
            function->c[root] = c;
        }
    }
    return changed;
}

// The comparisons of an i1 result among `lanes` (1 to 64) rows of `operations` and `types`, a bit each.
static uint64_t ir_truth_candidates(const uint8_t* operations, const uint8_t* types, uint32_t lanes)
{
    const __mmask64 valid = UINT64_MAX >> (BUSTER_IR_TRUTH_BATCH - lanes);
    const __m512i op = _mm512_maskz_loadu_epi8(valid, operations);
    const __m512i type = _mm512_maskz_loadu_epi8(valid, types);
    return valid & _mm512_cmpeq_epi8_mask(op, _mm512_set1_epi8(IR_ICMP)) & _mm512_cmpeq_epi8_mask(type, _mm512_set1_epi8(IR_I1));
}

// The former operand dominates the rewritten comparison. Each copy or extension on its path has
// one ordinary input that dominates it. If that path ends at an ICMP, both of that ICMP's operands
// therefore dominate the root. Check the actual written pair against that source, in operand order.
static bool ir_truth_inherits_operands(const IrFunction* f, uint32_t root, uint32_t former)
{
    const uint32_t source = ir_truth_unextend(f, former).value;
    return source < f->instruction_count && source != root && f->op[source] == IR_ICMP &&
           f->a[root] == f->a[source] && f->b[root] == f->b[source];
}

// Only the ordinary ICMP simplification can preserve a supplied proof. It edits a, b and the
// predicate of the root alone. The row diagnostic checks the actual result before the proof is reused.
static uint32_t ir_truth_simplify_rules(IrFunction* function, bool values, bool* verified)
{
    const uint32_t count = function->instruction_count;
    const uint8_t* const operations = function->op;
    const uint8_t* const types = function->type;
    uint32_t changed = 0;
    for (uint64_t first = 0; first < count; first += BUSTER_IR_TRUTH_BATCH)
    {
        const uint32_t lanes = count - first < BUSTER_IR_TRUTH_BATCH ? count - first : BUSTER_IR_TRUTH_BATCH;
        uint64_t candidates = ir_truth_candidates(operations + first, types + first, lanes);
        while (candidates != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(candidates);
            const uint32_t root = (uint32_t)first + lane;
            const uint32_t old_a = function->a[root];
            const uint32_t old_b = function->b[root];
            const bool rewritten = ir_truth_rewrite(function, root);
            const bool edited = rewritten || (values && ir_truth_rewrite_value(function, root));
            if (edited && verified != NULL && *verified)
            {
                *verified = rewritten && function->op[root] == IR_ICMP && function->type[root] == IR_I1 &&
                            ir_check_instruction(function, root, false).error == IR_OK &&
                            (ir_truth_inherits_operands(function, root, old_a) || ir_truth_inherits_operands(function, root, old_b));
            }
            changed += edited;
            candidates &= candidates - 1;
        }
    }
    return changed;
}

static __attribute__((unused)) uint32_t ir_truth_simplify(IrFunction* function)
{
    return ir_truth_simplify_rules(function, false, NULL);
}

static __attribute__((unused)) uint32_t ir_truth_simplify_values(IrFunction* function)
{
    return ir_truth_simplify_rules(function, true, NULL);
}

#endif
