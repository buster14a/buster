// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Integer truth values compared with zero (#1218). Include after ir.c. The C driver's owned, verified SSA is
// simplified before the optimizer builds its use lists or publishes an analysis revision. Only an ICMP's operands
// and predicate change: blocks, instruction identities, types and side tables remain unchanged. No memory operation
// moves or is duplicated; a comparison keeps referring to the same SSA values, including values read by observable
// loads. SCCP/DCE subsequently remove the now-unused comparison, extensions and zero constants.
//
// The candidate scan is 64 bounded opcode/type bytes at a time, with an exact scalar twin. The dependent walks run
// only for equality/inequality tests, through copies and zero/sign extensions, to a zero constant and another ICMP.
// Both extensions preserve zero versus nonzero, including sext i1 (whose true value becomes -1). Truncations,
// nonzero constants, floating comparisons and arbitrary boolean producers deliberately stop the walks. A bounded
// number of links prevents pathological copy chains from making this pass quadratic; longer chains remain intact.
//
// ir_truth_simplify mutates exclusively owned, already verified columns. No use lists or verified-analysis handles
// may be live across this call. Input columns are disjoint, have exactly instruction_count elements with no padding,
// and remain allocated throughout. The caller runs the ordinary optimizer and final SSA verifier afterwards.
//
// ir_truth_simplify_values, which if-conversion runs on its rounds' own columns (#1382), also takes the truth values
// it makes: a select of truth values tested against zero, and a test of an extended i1 value that is no comparison.
// The test itself becomes, in its row and type, an i1 select or a copy: `select c, (ext x), 0` tested nonzero is
// `select c, x, c` (c && x), `select c, 1, (ext y)` is `select c, c, y` (c || y), `select c, (ext x), (ext y)` is
// `select c, x, y`, and `select c, 1, 0` is c (tested equal to zero, `select c, 0, 1` is c too); `(ext v)` tested
// nonzero is v. A form that would need c's or a value's complement stays. Tier 1 selects i1 selects as it does wider
// ones, while its integer rules take no i1 and/or (#1382's follow-up). The operation changes, and nothing else does.

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

static uint32_t ir_truth_unextend(const IrFunction* function, uint32_t value)
{
    const uint32_t count = function->instruction_count;
    const uint8_t* const operations = function->op;
    const uint32_t* const inputs = function->a;
    for (uint32_t link = 0; value < count && link < BUSTER_IR_TRUTH_LINK_LIMIT; link += 1)
    {
        const uint32_t operation = operations[value];
        if (operation != IR_COPY && operation != IR_ZEXT && operation != IR_SEXT)
        {
            break;
        }
        value = inputs[value];
    }
    return value;
}

static bool ir_truth_zero(const IrFunction* function, uint32_t value)
{
    value = ir_truth_unextend(function, value);
    bool zero = false;
    if (value < function->instruction_count && function->op[value] == IR_CONST)
    {
        const IrConstant constant = function->constants[function->a[value]];
        zero = constant.low == 0 && constant.high == 0;
    }
    return zero;
}

static bool ir_truth_rewrite(IrFunction* function, uint32_t root)
{
    const uint32_t predicate = function->c[root];
    bool changed = false;
    if (predicate == IR_EQ || predicate == IR_NE)
    {
        const uint32_t left = function->a[root];
        const uint32_t right = function->b[root];
        const uint32_t operand = ir_truth_zero(function, right) ? left : ir_truth_zero(function, left) ? right : BUSTER_IR_NONE;
        const uint32_t value = ir_truth_unextend(function, operand);
        if (value < function->instruction_count && value != root && function->op[value] == IR_ICMP && function->type[value] == IR_I1)
        {
            // Snapshot the definition before any store; the IR columns are disjoint but a and b name other values.
            const uint32_t inner_left = function->a[value];
            const uint32_t inner_right = function->b[value];
            const uint32_t inner_predicate = function->c[value];
            function->a[root] = inner_left;
            function->b[root] = inner_right;
            function->c[root] = predicate == IR_EQ ? ir_truth_inverse[inner_predicate] : inner_predicate;
            changed = true;
        }
    }
    return changed;
}

// A select arm's truth: 1 a zero constant, 2 a nonzero one, 3 an i1 value (*value), 0 none, through extensions.
static uint32_t ir_truth_kind(const IrFunction* function, uint32_t value, uint32_t* truth)
{
    const uint32_t inner = ir_truth_unextend(function, value);
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
        const uint32_t value = ir_truth_unextend(function, operand);
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
            op = nonzero && (shape == (3u << 2 | 1u) || shape == (2u << 2 | 3u) || shape == (3u << 2 | 3u)) ? IR_SELECT :
                 (nonzero && shape == (2u << 2 | 1u)) || (!nonzero && shape == (1u << 2 | 2u)) ? IR_COPY : IR_OP_COUNT;
            a = condition;
            b = op != IR_SELECT ? BUSTER_IR_NONE : kx == 3 ? x : condition;
            c = op != IR_SELECT ? BUSTER_IR_NONE : ky == 3 ? y : condition;
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

static uint32_t ir_truth_simplify_rules(IrFunction* function, bool scalar, bool values)
{
    const uint32_t count = function->instruction_count;
    const uint8_t* const operations = function->op;
    const uint8_t* const types = function->type;
    uint32_t changed = 0;
    for (uint64_t first = 0; first < count; first += BUSTER_IR_TRUTH_BATCH)
    {
        const uint32_t lanes = count - first < BUSTER_IR_TRUTH_BATCH ? count - first : BUSTER_IR_TRUTH_BATCH;
        uint64_t candidates = 0;
#if BUSTER_HOST_VECTOR
        if (!scalar)
        {
            const __mmask64 valid = UINT64_MAX >> (BUSTER_IR_TRUTH_BATCH - lanes);
            const __m512i op = _mm512_maskz_loadu_epi8(valid, operations + first);
            const __m512i type = _mm512_maskz_loadu_epi8(valid, types + first);
            candidates = valid & _mm512_cmpeq_epi8_mask(op, _mm512_set1_epi8(IR_ICMP)) & _mm512_cmpeq_epi8_mask(type, _mm512_set1_epi8(IR_I1));
        }
        else
#endif
        {
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                const uint64_t candidate = operations[first + lane] == IR_ICMP && types[first + lane] == IR_I1;
                candidates |= candidate << lane;
            }
        }
        while (candidates != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(candidates);
            const uint32_t root = (uint32_t)first + lane;
            const bool rewritten = ir_truth_rewrite(function, root);
            changed += rewritten || (values && ir_truth_rewrite_value(function, root));
            candidates &= candidates - 1;
        }
    }
    (void)scalar;
    return changed;
}

static __attribute__((unused)) uint32_t ir_truth_simplify(IrFunction* function, bool scalar)
{
    return ir_truth_simplify_rules(function, scalar, false);
}

static __attribute__((unused)) uint32_t ir_truth_simplify_values(IrFunction* function, bool scalar)
{
    return ir_truth_simplify_rules(function, scalar, true);
}

#endif
