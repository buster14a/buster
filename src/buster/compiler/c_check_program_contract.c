// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Expression-scoped scalar contraction (#1239). Match checked source edges, never local definitions or earlier
// statements: a store, a call result, a narrowing conversion and a control-flow join are rounding barriers. No-op
// wrappers and a comma's right operand preserve the product; its left operand still executes exactly once. The
// existing X86 FMA row consumes three values in their original evaluation order. Both object and direct emission
// use this lowering. Included after the operand, instruction and operation-table helpers, before compound updates.

// A lexical override is mode + 1; zero restores the command-line default. Source ON requests expression contraction
// even with a fast command-line default, as Clang's -O0 emission does.
// Tier 0 implements the expression contractions of on. The fast modes permit broader optimization but, like Clang
// -O0, do not introduce expression fmuladd operations themselves; they do not turn on unsafe cross-statement folding.
static uint8_t check_lower_contract_mode(const CheckLowerWriter* writer, uint32_t node)
{
    const CheckInput* input = writer->program->input;
    uint8_t mode = writer->program->fp_contract;
    if (input->fp_contracts != NULL)
    {
        const uint32_t token = input->graph.tokens[node];
        const uint8_t override = token < input->token_count ? input->fp_contracts[token] : 0;
        mode = override != 0 ? (uint8_t)(override - 1) : mode;
    }
    return mode;
}

typedef struct CheckLowerContract CheckLowerContract;
struct CheckLowerContract
{
    uint32_t root;
    uint32_t product;
    bool right;
    bool negative;
};

// Find a still-unrounded multiply, possibly behind no-op wrappers and one negation, as Clang's expression emission
// does. Two negations are not a multiply's immediate use. Folded products are already rounded constants, not FMA
// candidates. Every step moves to an earlier graph node, bounding malformed chains as well as deeply nested input.
static uint32_t check_lower_contract_product(const CheckLowerWriter* writer, uint32_t root, uint8_t class, bool* negative)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    uint32_t product = BUSTER_CHECK_NONE;
    uint32_t node = root;
    bool valid = node < stream->count;
    bool negated = false;
    while (valid && product == BUSTER_CHECK_NONE)
    {
        const CheckValue value = check_stream_value(stream, node);
        const CheckOp op = graph->ops[node];
        const bool folded = value.constant_kind != CHECK_CONSTANT_NONE && value.fault == CHECK_FAULT_NONE &&
                            (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
        valid = !folded && value.overload_head == 0 && check_lower_class(writer, value.type) == class;
        if (valid && op == CHECK_OP_MULTIPLY)
        {
            valid = check_lower_class(writer, stream->conversions[node].operands[0].type) == class &&
                    check_lower_class(writer, stream->conversions[node].operands[1].type) == class;
            product = valid ? node : BUSTER_CHECK_NONE;
        }
        else if (valid)
        {
            const bool wrapper = op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE || op == CHECK_OP_SELECT;
            const bool negate = op == CHECK_OP_NEGATIVE && !negated;
            const uint32_t child = op == CHECK_OP_COMMA ? graph->operand1[node] : graph->operand0[node];
            valid = (wrapper || negate || op == CHECK_OP_COMMA) && child < node;
            negated |= negate;
            node = child;
        }
    }
    *negative = negated;
    return product;
}

// The left product wins when both operands are products. A compound assignment already has its old destination on
// the stack, so only its right operand can supply a product. Target features are the function's, not the host's or
// merely the translation unit's: a target attribute can enable or disable this path independently.
static CheckLowerContract check_lower_contract_plan(const CheckLowerWriter* writer, uint32_t node, TypeId type, bool update)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    const bool arithmetic = update ? op == CHECK_OP_ADD_ASSIGN || op == CHECK_OP_SUBTRACT_ASSIGN : op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT;
    const uint64_t instructions = BUSTER_X86_FEATURE(FMA) | BUSTER_X86_FEATURE(AVX512F);
    CheckLowerContract plan = { .root = BUSTER_CHECK_NONE, .product = BUSTER_CHECK_NONE };
    if (arithmetic && (writer->target.features & instructions) != 0 && check_lower_contract_mode(writer, node) == BUSTER_PREDEFINES_CONTRACT_ON)
    {
        const uint8_t class = check_lower_class(writer, type);
        const bool scalar = class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64;
        for (uint32_t side = update ? 1 : 0; scalar && side < 2 && plan.product == BUSTER_CHECK_NONE; side += 1)
        {
            const uint32_t root = side == 0 ? graph->operand0[node] : graph->operand1[node];
            bool negative = false;
            const uint32_t product = check_lower_class(writer, stream->conversions[node].operands[side].type) == class ?
                                     check_lower_contract_product(writer, root, class, &negative) : BUSTER_CHECK_NONE;
            if (product != BUSTER_CHECK_NONE)
            {
                plan = (CheckLowerContract){ .root = root, .product = product, .right = side != 0, .negative = negative };
            }
        }
    }
    return plan;
}

// The product's two operands in place of its rounded result. A deferred task at a comma preserves prefix effects
// without recursion, a second evaluation, or an input-sized temporary allocation. The match already proved every
// skipped wrapper preserves the floating class. A product negation changes its first factor before the FMA.
static void check_lower_contract_operands(CheckLowerWriter* writer, uint32_t node, bool negative)
{
    const CheckGraph* graph = &writer->program->input->graph;
    const CheckStream* stream = writer->program->stream;
    while (graph->ops[node] != CHECK_OP_MULTIPLY && graph->ops[node] != CHECK_OP_COMMA)
    {
        node = graph->operand0[node];
    }
    if (graph->ops[node] == CHECK_OP_COMMA)
    {
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_FP_PRODUCT, .node = graph->operand1[node], .value = negative });
        check_lower_operand(writer, node, 0, false, BUSTER_TYPE_VOID);
    }
    else
    {
        const TypeId left = stream->conversions[node].operands[0].type;
        const TypeId right = stream->conversions[node].operands[1].type;
        check_lower_operand(writer, node, 1, false, right);
        if (negative)
        {
            check_lower_instruction(writer, BUSTER_TYPED_NEG, check_lower_class(writer, left), 0);
        }
        check_lower_operand(writer, node, 0, false, left);
    }
}

static bool check_lower_contract(CheckLowerWriter* writer, uint32_t node, TypeId type, bool update)
{
    const CheckLowerContract plan = check_lower_contract_plan(writer, node, type, update);
    const bool contracted = plan.product != BUSTER_CHECK_NONE;
    if (contracted)
    {
        const CheckGraph* graph = &writer->program->input->graph;
        const CheckStream* stream = writer->program->stream;
        const CheckOp op = graph->ops[node];
        const bool subtract = op == CHECK_OP_SUBTRACT || op == CHECK_OP_SUBTRACT_ASSIGN;
        const bool negative = plan.negative != (plan.right && subtract);
        const uint8_t class = check_lower_class(writer, type);
        Tier0Operation row;
        const char* reason = check_vector_scalar_row(&writer->target, CBUILTIN_ELEMENTWISE_FMA, class, 3, &row);
        if (reason != NULL)
        {
            check_lower_unsupported(writer, reason);
        }
        else
        {
            if (plan.right)
            {
                // Stack order is c, a, b: 231 computes c + a*b without moving an evaluation ahead of another.
                const bool evex = (writer->target.features & BUSTER_X86_FEATURE(AVX512F)) != 0;
                const bool d = class == BUSTER_TYPED_CLASS_F64;
                row.records[0].form = evex ? d ? X86_FORM_VFMADD231SD_X_X_XM64_EVEX : X86_FORM_VFMADD231SS_X_X_XM32_EVEX :
                                            d ? X86_FORM_VFMADD231SD_X_X_XM64 : X86_FORM_VFMADD231SS_X_X_XM32;
            }
            check_lower_instruction(writer, BUSTER_TYPED_X86, class, check_lower_operation_index(writer, &row));
            if (!plan.right)
            {
                if (subtract)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_NEG, class, 0);
                }
                check_lower_operand(writer, node, 1, false, stream->conversions[node].operands[1].type);
            }
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_FP_PRODUCT, .node = plan.root, .value = negative });
            if (plan.right && !update)
            {
                check_lower_operand(writer, node, 0, false, stream->conversions[node].operands[0].type);
            }
            writer->vectors = true;
        }
    }
    return contracted;
}
