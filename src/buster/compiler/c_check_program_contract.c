// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Expression-scoped scalar contraction (#1239), included by c_check_program_lower.c. The checked source tree still
// distinguishes a product's value from a stored result: only the former can be consumed by a sum's FMA. The shared
// matcher also keeps the ordinary binary batch/templates from emitting an unfused copy. No graph is rewritten, no
// operand is evaluated twice, and the existing exact x86 row is carried through both native tiers.
//
// Clang 23's `on` contracts during expression emission; `fast` instead marks ordinary operations for its optimizer,
// whose -O0 pipeline leaves them separate. This lowering follows that -O0 contract. The explicit task walk retains
// comma/selected-condition effects and left-to-right operand evaluation without recursion or a per-node side table.

typedef struct CheckLowerContract CheckLowerContract;
struct CheckLowerContract
{
    uint32_t product;
    uint32_t wrapper;
    uint8_t class;
    bool right;
    bool negative_product;
    bool negative_addend;
};

// Read only completed, nonvolatile constant initializer receipts. Weak objects keep their runtime load even when
// this translation unit provides an initializer, so require the source reader's nonnull (non-weak) address proof.
static bool check_lower_contract_constant(const CheckProgram* program, uint32_t node, CheckValue* loaded)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckValue value = check_stream_value(program->stream, node);
    const uint32_t restricted = BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_VOLATILE_PART | BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_CONST_READ_RESTRICTED;
    const uint32_t position = graph->source_positions != NULL ? graph->source_positions[node] : BUSTER_CHECK_NONE;
    const bool known = value.category == CHECK_LVALUE && value.fault == CHECK_FAULT_NONE && value.constant_kind == CHECK_CONSTANT_ADDRESS &&
                       (value.flags & restricted) == 0 && (value.flags & BUSTER_CHECK_NONNULL_ADDRESS) != 0 &&
                       (type_qualifiers(value.type) & BUSTER_TYPE_VOLATILE) == 0 &&
                       check_const_load(program->types, graph->const_values, value.object, value.offset, value.object_bit_offset,
                                        value.bit_width, value.type, position, loaded) &&
                       (loaded->constant_kind == CHECK_CONSTANT_FLOATING || loaded->constant_kind == CHECK_CONSTANT_INTEGER);
    return known;
}

// Clang removes a conditional's join only when its controlling value is known without runtime effects. The runtime
// checker's ordinary truth query deliberately does not read constant lvalues; its initializer receipt can still
// prove this source-emission decision. Floating environment accesses retain the ordinary conditional boundary.
static bool check_lower_contract_truth(const CheckProgram* program, uint32_t node, uint32_t condition, bool* truth)
{
    const CheckGraph* graph = &program->input->graph;
    const bool environment = graph->fp_environments != NULL && (graph->fp_environments[node] & BUSTER_CHECK_FP_ENV_ACCESS) != 0;
    bool known = false;
    if (!check_program_runtime_effects(program, condition))
    {
        known = check_program_runtime_truth(program, node, condition, truth);
        if (!known)
        {
            CheckValue loaded = { 0 };
            known = check_lower_contract_constant(program, condition, &loaded) &&
                    !(environment && check_action_floating_type(program->types, loaded.type)) &&
                    check_action_truth(program->types, &loaded, truth);
        }
    }
    return known;
}

// Clang emits direct const scalar declarations as constants, including through ordinary arithmetic/casts, but keeps
// reads through an array, member or dereference dynamic. The runtime checker intentionally does not fold const
// lvalues. Read their completed initializer receipts with the existing constant-evaluation access guards, solely to
// decide whether a product must retain its already-rounded value; do not fold or discard any side effects here.
// The small worklist proves that some operand remains dynamic. If it fills, retain the ordinary product instead of
// speculating. Pure checked constants and straight wrapper chains consume no additional pending slots.
#define BUSTER_CHECK_CONTRACT_CONSTANT_PENDING 64u
static bool check_lower_contract_runtime_product(const CheckLowerWriter* writer, uint32_t product)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    uint32_t pending[BUSTER_CHECK_CONTRACT_CONSTANT_PENDING];
    uint32_t count = 1;
    bool runtime = false;
    bool bounded = true;
    pending[0] = product;
    while (count != 0 && !runtime && bounded)
    {
        const uint32_t node = pending[--count];
        const CheckOp op = graph->ops[node];
        const CheckValue value = check_stream_value(stream, node);
        const bool floating_environment = (value.flags & BUSTER_CHECK_FP_ENVIRONMENT) != 0;
        const bool constant = value.category == CHECK_RVALUE && value.fault == CHECK_FAULT_NONE && !floating_environment &&
                              (value.constant_kind == CHECK_CONSTANT_FLOATING || value.constant_kind == CHECK_CONSTANT_INTEGER);
        if (floating_environment || op == CHECK_OP_STATEMENT_VALUE)
        {
            runtime = true;
        }
        else if (op == CHECK_OP_VALUE && value.category == CHECK_LVALUE)
        {
            CheckValue loaded = { 0 };
            const bool compound = value.object != 0 && value.object < program->object_count &&
                                  check_input_bit(program->input->declaration_bits, value.object - 1, BUSTER_CHECK_BITS_COMPOUND);
            runtime = compound || !check_lower_contract_constant(program, node, &loaded);
        }
        else if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_SELECT || op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE ||
                 op == CHECK_OP_REAL || op == CHECK_OP_BIT_NOT || op == CHECK_OP_NOT || op == CHECK_OP_CONDITION)
        {
            pending[count++] = graph->operand0[node];
        }
        else if (op == CHECK_OP_COMMA || op == CHECK_OP_ASSIGN)
        {
            pending[count++] = graph->operand1[node];
        }
        else if (op == CHECK_OP_CONDITIONAL)
        {
            bool truth = false;
            const uint32_t condition = graph->operand0[node];
            const bool known = check_lower_contract_truth(program, node, condition, &truth);
            if (known)
            {
                pending[count++] = truth ? graph->operand1[node] : graph->operand2[node];
            }
            runtime = !known;
        }
        else if (op == CHECK_OP_BUILTIN_MISC && graph->members[node] == CBUILTIN_ARITHMETIC_FENCE && graph->argument_counts[node] == 1)
        {
            pending[count++] = graph->argument_nodes[graph->argument_offsets[node]];
        }
        else if (op < CHECK_OP_PRE_INCREMENT && check_lower_binary(op) != BUSTER_TYPED_NONE)
        {
            bounded = count + 2 <= BUSTER_CHECK_CONTRACT_CONSTANT_PENDING;
            if (bounded)
            {
                pending[count++] = graph->operand1[node];
                pending[count++] = graph->operand0[node];
            }
        }
        else if (op == CHECK_OP_VALUE || op == CHECK_OP_SUBSCRIPT || op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW || op == CHECK_OP_DEREFERENCE ||
                 op == CHECK_OP_CALL || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN))
        {
            runtime = !constant;
        }
        else if (!constant)
        {
            bounded = false;
        }
    }
    return runtime;
}

// The source wrappers through which Clang's expression emitter still sees the product. Their result and consumed
// operand must have the same floating class; even an exact widening of an already-rounded product is a barrier.
// Assignment, statement-expression and nonconstant conditional nodes deliberately have no child here.
static uint32_t check_lower_contract_child(const CheckLowerWriter* writer, uint32_t node, uint8_t class)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    uint32_t child = BUSTER_CHECK_NONE;
    if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_SELECT || op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_REAL)
    {
        child = graph->operand0[node];
    }
    else if (op == CHECK_OP_COMMA)
    {
        child = graph->operand1[node];
    }
    else if (op == CHECK_OP_CONDITIONAL)
    {
        bool truth = false;
        const uint32_t condition = graph->operand0[node];
        if (check_lower_contract_truth(program, node, condition, &truth))
        {
            child = truth ? graph->operand1[node] : graph->operand2[node];
        }
    }
    else if (op == CHECK_OP_BUILTIN_MISC && graph->members[node] == CBUILTIN_ARITHMETIC_FENCE && graph->argument_counts[node] == 1)
    {
        child = graph->argument_nodes[graph->argument_offsets[node]];
    }
    if (child >= node || child >= stream->count || check_lower_class(writer, check_stream_type(stream, child)) != class)
    {
        child = BUSTER_CHECK_NONE;
    }
    return child;
}

static CheckLowerContract check_lower_contract_plan(const CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    const bool update = op == CHECK_OP_ADD_ASSIGN || op == CHECK_OP_SUBTRACT_ASSIGN;
    const bool arithmetic = update || op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT;
    const bool subtract = op == CHECK_OP_SUBTRACT || op == CHECK_OP_SUBTRACT_ASSIGN;
    const uint64_t fused = BUSTER_X86_FEATURE(FMA) | BUSTER_X86_FEATURE(AVX512F);
    CheckLowerContract plan = { .product = BUSTER_CHECK_NONE };
    if (arithmetic && (writer->target.features & fused) != 0 && check_graph_fp_contract(graph, node) == BUSTER_CHECK_FP_CONTRACT_ON)
    {
        const TypeId type = update ? stream->conversions[node].operands[0].type : check_stream_type(stream, node);
        const uint8_t class = check_lower_class(writer, type);
        const bool scalar = class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64;
        const bool folded = check_stream_constant_kind(stream, node) == CHECK_CONSTANT_FLOATING &&
                            (check_stream_flags(stream, node) & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
        const bool same = scalar && check_lower_class(writer, stream->conversions[node].operands[0].type) == class &&
                          check_lower_class(writer, stream->conversions[node].operands[1].type) == class;
        for (uint32_t side = update ? 1 : 0; same && !folded && side < 2 && plan.product == BUSTER_CHECK_NONE; side += 1)
        {
            const uint32_t wrapper = side == 0 ? graph->operand0[node] : graph->operand1[node];
            uint32_t product = wrapper;
            bool negative = subtract && side == 1;
            while (product < node && check_lower_class(writer, check_stream_type(stream, product)) == class && graph->ops[product] != CHECK_OP_MULTIPLY)
            {
                negative ^= graph->ops[product] == CHECK_OP_NEGATIVE;
                product = check_lower_contract_child(writer, product, class);
            }
            if (product < node && graph->ops[product] == CHECK_OP_MULTIPLY && check_graph_fp_contract(graph, product) == BUSTER_CHECK_FP_CONTRACT_ON &&
                check_lower_class(writer, check_stream_type(stream, product)) == class &&
                check_lower_class(writer, stream->conversions[product].operands[0].type) == class &&
                check_lower_class(writer, stream->conversions[product].operands[1].type) == class)
            {
                if (check_lower_contract_runtime_product(writer, product))
                {
                    plan = (CheckLowerContract){
                        .product = product,
                        .wrapper = wrapper,
                        .class = class,
                        .right = side != 0,
                        .negative_product = negative,
                        .negative_addend = subtract && side == 0,
                    };
                }
            }
        }
    }
    return plan;
}

static bool check_lower_contract_candidate(const CheckLowerWriter* writer, uint32_t node)
{
    return check_lower_contract_plan(writer, node).product != BUSTER_CHECK_NONE;
}

// Continue down a previously matched wrapper chain. Its task holds the product identity and the already-combined
// sign. A comma's discarded operand (or a selected conditional's condition) runs before the next wrapper. At the
// product, only its two operands are emitted; its multiplication is the consuming row's single fused rounding.
static void check_lower_contract_product(CheckLowerWriter* writer, uint32_t node, uint32_t product, bool negative)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint8_t class = check_lower_class(writer, check_stream_type(stream, product));
    if (node == product)
    {
        check_lower_operand(writer, product, 1, false, stream->conversions[product].operands[1].type);
        if (negative)
        {
            check_lower_instruction(writer, BUSTER_TYPED_NEG, class, 0);
        }
        check_lower_operand(writer, product, 0, false, stream->conversions[product].operands[0].type);
    }
    else
    {
        const uint32_t child = check_lower_contract_child(writer, node, class);
        if (child == BUSTER_CHECK_NONE)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CONTRACT, .node = child, .type = product, .value = negative });
            if (graph->ops[node] == CHECK_OP_COMMA || graph->ops[node] == CHECK_OP_CONDITIONAL)
            {
                check_lower_operand(writer, node, 0, false, BUSTER_TYPE_VOID);
            }
        }
    }
}

// Push the consuming row and, in source order, its operands. A compound assignment has already kept its old LHS
// value below the RHS; its existing update code remains responsible for the address, conversion and one store.
static bool check_lower_contract(CheckLowerWriter* writer, uint32_t node, bool update)
{
    const CheckLowerContract plan = check_lower_contract_plan(writer, node);
    const bool contracted = plan.product != BUSTER_CHECK_NONE;
    if (contracted)
    {
        const CheckStream* stream = writer->program->stream;
        Tier0Operation row;
        const char* reason = check_vector_contract_row(&writer->target, plan.class, plan.right, &row);
        if (reason != NULL)
        {
            check_lower_unsupported(writer, reason);
        }
        else
        {
            check_lower_instruction(writer, BUSTER_TYPED_X86, plan.class, check_lower_operation_index(writer, &row));
            if (!plan.right)
            {
                if (plan.negative_addend)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_NEG, plan.class, 0);
                }
                check_lower_operand(writer, node, 1, false, stream->conversions[node].operands[1].type);
            }
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CONTRACT, .node = plan.wrapper, .type = plan.product, .value = plan.negative_product });
            if (plan.right && !update)
            {
                check_lower_operand(writer, node, 0, false, stream->conversions[node].operands[0].type);
            }
            writer->vectors = true;
        }
    }
    return contracted;
}
