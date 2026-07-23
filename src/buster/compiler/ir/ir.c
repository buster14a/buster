#include <buster/compiler/ir/ir.h>

#include <buster/file.h>
#include <buster/string.h>

typedef struct IrLowered IrLowered;
struct IrLowered
{
    IrValueId value;
    AnalysisTypeId type;
    AnalysisLocalId local;
    IrValueCategory category;
};

typedef struct IrBuilder IrBuilder;
struct IrBuilder
{
    Arena* result_arena;
    Arena* scratch_arena;
    AnalysisResult* analysis;
    AnalysisEntity* entity;
    AnalysisBody* body;
    IrFunction* function;
    IrBlockId current;
};

typedef struct IrLowerTask IrLowerTask;
typedef enum IrLowerTaskKind
{
    IR_LOWER_TASK_STATEMENT,
    IR_LOWER_TASK_SEAL_BLOCK,
} IrLowerTaskKind;

struct IrLowerTask
{
    IrLowerTask* previous;
    AstStatement* statement;
    IrBlockId block;
    IrBlockId end;
    IrBlockId break_block;
    IrBlockId continue_block;
    IrLowerTaskKind kind;
};

BUSTER_GLOBAL_LOCAL bool ir_type_id_equal(AnalysisTypeId left, AnalysisTypeId right)
{
    return left.value == right.value;
}

BUSTER_GLOBAL_LOCAL bool ir_entity_id_equal(AnalysisEntityId left, AnalysisEntityId right)
{
    return left.module.value == right.module.value && left.index.value == right.index.value;
}

BUSTER_GLOBAL_LOCAL bool ir_block_id_valid(IrFunction* function, IrBlockId block)
{
    return block.value < function->block_count;
}

BUSTER_GLOBAL_LOCAL bool ir_value_id_valid(IrFunction* function, IrValueId value)
{
    return value.value < function->value_count;
}

BUSTER_GLOBAL_LOCAL u32 ir_node_arity(AstNode* node)
{
    switch (node->id)
    {
        case AST_NODE_CONSTANT_INTEGER:
        case AST_NODE_CONSTANT_FLOAT:
        case AST_NODE_CONSTANT_CHARACTER:
        case AST_NODE_CONSTANT_STRING:
        case AST_NODE_IDENTIFIER:
        case AST_NODE_UNDEFINED:
        case AST_NODE_ENUM_LITERAL: return 0;
        case AST_NODE_ARRAY_LITERAL: return node->array_literal.element_count;
        case AST_NODE_ARRAY_INDEX: return 2;
        case AST_NODE_ARRAY_SLICE:
        {
            return 1 + (u32)node->array_slice.has_start + (u32)node->array_slice.has_end;
        }
        case AST_NODE_AGGREGATE_LITERAL: return node->aggregate_literal.field_count;
        case AST_NODE_CALL: return 1 + node->call.argument_count;
        case AST_NODE_INTRINSIC_CALL: return node->intrinsic_call.argument_count;
        case AST_NODE_MEMBER_ACCESS:
        case AST_NODE_UNARY_MINUS:
        case AST_NODE_UNARY_PLUS:
        case AST_NODE_UNARY_LOGICAL_NOT:
        case AST_NODE_UNARY_BITWISE_NOT:
        case AST_NODE_ADDRESS_OF:
        case AST_NODE_DEREFERENCE: return 1;
        case AST_NODE_BINARY_PLUS:
        case AST_NODE_BINARY_MINUS:
        case AST_NODE_BINARY_ASTERISK:
        case AST_NODE_BINARY_SLASH:
        case AST_NODE_BINARY_PERCENT:
        case AST_NODE_BINARY_SHIFT_LEFT:
        case AST_NODE_BINARY_SHIFT_RIGHT:
        case AST_NODE_BINARY_EQUAL:
        case AST_NODE_BINARY_NOT_EQUAL:
        case AST_NODE_BINARY_LESS:
        case AST_NODE_BINARY_LESS_EQUAL:
        case AST_NODE_BINARY_GREATER:
        case AST_NODE_BINARY_GREATER_EQUAL:
        case AST_NODE_BINARY_AMPERSAND:
        case AST_NODE_BINARY_BAR:
        case AST_NODE_BINARY_CARET:
        case AST_NODE_BINARY_BOOLEAN_AND:
        case AST_NODE_BINARY_BOOLEAN_OR:
        case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
        case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
        case AST_NODE_BINARY_RANGE: return 2;
        case AST_NODE_COUNT: break;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL IrBlockId ir_block_create(IrBuilder* builder)
{
    IrFunction* function = builder->function;
    BUSTER_CHECK(function->block_count < function->block_capacity);
    IrBlockId id = { .value = function->block_count };
    function->blocks[function->block_count] = (IrBlock){
        .local_values = arena_allocate(
            builder->result_arena,
            IrValueId,
            function->local_count),
        .first_instruction = IR_INSTRUCTION_ID_INVALID,
        .last_instruction = IR_INSTRUCTION_ID_INVALID,
        .id = id,
    };
    for (u32 local_index = 0; local_index < function->local_count; local_index += 1)
    {
        function->blocks[function->block_count].local_values[local_index] = IR_VALUE_ID_INVALID;
    }
    function->block_count += 1;
    return id;
}

BUSTER_GLOBAL_LOCAL IrValueId ir_value_create(
    IrBuilder* builder,
    AnalysisTypeId type,
    IrInstructionId definition,
    IrValueCategory category)
{
    IrFunction* function = builder->function;
    BUSTER_CHECK(function->value_count < function->value_capacity);
    IrValueId id = { .value = function->value_count };
    function->values[function->value_count] = (IrValue){
        .type = type,
        .definition = definition,
        .category = category,
    };
    function->value_count += 1;
    return id;
}

BUSTER_GLOBAL_LOCAL void ir_predecessor_add(
    IrBuilder* builder,
    IrBlockId target,
    IrBlockId predecessor)
{
    IrBlock* block = builder->function->blocks + target.value;
    BUSTER_CHECK(!block->sealed);
    IrPredecessor* edge = arena_allocate(builder->result_arena, IrPredecessor, 1);
    *edge = (IrPredecessor){ .block = predecessor };
    if (block->last_predecessor)
    {
        block->last_predecessor->next = edge;
    }
    else
    {
        block->first_predecessor = edge;
    }
    block->last_predecessor = edge;
    block->predecessor_count += 1;
}

BUSTER_GLOBAL_LOCAL IrBlockParameter* ir_block_parameter_create(
    IrBuilder* builder,
    IrBlockId block_id,
    AnalysisLocalId local,
    AnalysisTypeId type)
{
    IrBlock* block = builder->function->blocks + block_id.value;
    IrBlockParameter* parameter = arena_allocate(builder->result_arena, IrBlockParameter, 1);
    *parameter = (IrBlockParameter){
        .type = type,
        .local = local,
    };
    parameter->value = ir_value_create(
        builder,
        type,
        IR_INSTRUCTION_ID_INVALID,
        IR_VALUE_VALUE);
    if (block->last_parameter)
    {
        block->last_parameter->next = parameter;
    }
    else
    {
        block->first_parameter = parameter;
    }
    block->last_parameter = parameter;
    block->parameter_count += 1;
    if (local.value != ANALYSIS_ID_UNDERLYING_INVALID)
    {
        block->local_values[local.value] = parameter->value;
    }
    return parameter;
}

BUSTER_GLOBAL_LOCAL void ir_block_parameter_incoming_add(
    IrBuilder* builder,
    IrBlockParameter* parameter,
    IrBlockId predecessor,
    IrValueId value)
{
    IrIncoming* incoming = arena_allocate(builder->result_arena, IrIncoming, 1);
    *incoming = (IrIncoming){ .predecessor = predecessor, .value = value };
    if (parameter->last_incoming)
    {
        parameter->last_incoming->next = incoming;
    }
    else
    {
        parameter->first_incoming = incoming;
    }
    parameter->last_incoming = incoming;
    parameter->incoming_count += 1;
}

typedef enum IrSsaReadState
{
    IR_SSA_READ_BEGIN,
    IR_SSA_READ_WAIT_SINGLE,
    IR_SSA_READ_MULTI,
    IR_SSA_READ_WAIT_MULTI,
} IrSsaReadState;

typedef struct IrSsaReadFrame IrSsaReadFrame;
struct IrSsaReadFrame
{
    IrPredecessor* predecessor;
    IrBlockParameter* parameter;
    IrBlockId block;
    IrBlockId pending_predecessor;
    IrSsaReadState state;
};

BUSTER_GLOBAL_LOCAL IrValueId ir_ssa_read(
    IrBuilder* builder,
    IrBlockId block_id,
    AnalysisLocalId local)
{
    IrFunction* function = builder->function;
    IrSsaReadFrame* frames = arena_allocate(
        builder->scratch_arena,
        IrSsaReadFrame,
        function->block_count + 1);
    u32 depth = 1;
    IrValueId completed = IR_VALUE_ID_INVALID;
    frames[0] = (IrSsaReadFrame){ .block = block_id };
    while (depth)
    {
        IrSsaReadFrame* frame = frames + depth - 1;
        IrBlock* block = function->blocks + frame->block.value;
        if (frame->state == IR_SSA_READ_BEGIN)
        {
            IrValueId existing = block->local_values[local.value];
            if (existing.value != IR_ID_UNDERLYING_INVALID)
            {
                completed = existing;
                depth -= 1;
                continue;
            }
            AnalysisTypeId type = builder->body->locals[local.value].type;
            if (!block->sealed)
            {
                completed = ir_block_parameter_create(builder, frame->block, local, type)->value;
                depth -= 1;
                continue;
            }
            if (!block->predecessor_count)
            {
                completed = IR_VALUE_ID_INVALID;
                depth -= 1;
                continue;
            }
            if (block->predecessor_count == 1)
            {
                frame->state = IR_SSA_READ_WAIT_SINGLE;
                BUSTER_CHECK(depth < function->block_count + 1);
                frames[depth] = (IrSsaReadFrame){
                    .block = block->first_predecessor->block,
                };
                depth += 1;
                continue;
            }
            frame->parameter = ir_block_parameter_create(builder, frame->block, local, type);
            frame->predecessor = block->first_predecessor;
            frame->state = IR_SSA_READ_MULTI;
        }
        else if (frame->state == IR_SSA_READ_WAIT_SINGLE)
        {
            block->local_values[local.value] = completed;
            depth -= 1;
        }
        else if (frame->state == IR_SSA_READ_MULTI)
        {
            if (!frame->predecessor)
            {
                completed = frame->parameter->value;
                depth -= 1;
                continue;
            }
            frame->pending_predecessor = frame->predecessor->block;
            frame->predecessor = frame->predecessor->next;
            frame->state = IR_SSA_READ_WAIT_MULTI;
            BUSTER_CHECK(depth < function->block_count + 1);
            frames[depth] = (IrSsaReadFrame){ .block = frame->pending_predecessor };
            depth += 1;
        }
        else
        {
            BUSTER_CHECK(completed.value != IR_ID_UNDERLYING_INVALID);
            ir_block_parameter_incoming_add(
                builder,
                frame->parameter,
                frame->pending_predecessor,
                completed);
            frame->state = IR_SSA_READ_MULTI;
        }
    }
    return completed;
}

BUSTER_GLOBAL_LOCAL void ir_ssa_write(
    IrBuilder* builder,
    IrBlockId block,
    AnalysisLocalId local,
    IrValueId value)
{
    BUSTER_CHECK(local.value < builder->function->local_count);
    builder->function->blocks[block.value].local_values[local.value] = value;
}

BUSTER_GLOBAL_LOCAL void ir_block_seal(IrBuilder* builder, IrBlockId block_id)
{
    IrBlock* block = builder->function->blocks + block_id.value;
    if (block->sealed)
    {
        return;
    }
    block->sealed = true;
    for (IrBlockParameter* parameter = block->first_parameter;
        parameter;
        parameter = parameter->next)
    {
        if (parameter->incoming_count)
        {
            continue;
        }
        for (IrPredecessor* predecessor = block->first_predecessor;
            predecessor;
            predecessor = predecessor->next)
        {
            IrValueId incoming = ir_ssa_read(builder, predecessor->block, parameter->local);
            BUSTER_CHECK(incoming.value != IR_ID_UNDERLYING_INVALID);
            ir_block_parameter_incoming_add(
                builder,
                parameter,
                predecessor->block,
                incoming);
        }
    }
}

BUSTER_GLOBAL_LOCAL IrInstruction* ir_emit(
    IrBuilder* builder,
    IrOpcode opcode,
    AnalysisTypeId type,
    IrValueCategory category,
    ParserSourceRange source,
    IrValueId* operands,
    u32 operand_count,
    bool produces_value)
{
    IrFunction* function = builder->function;
    BUSTER_CHECK(ir_block_id_valid(function, builder->current));
    IrBlock* block = function->blocks + builder->current.value;
    BUSTER_CHECK(!block->terminated);
    BUSTER_CHECK(function->instruction_count < function->instruction_capacity);
    IrInstructionId id = { .value = function->instruction_count };
    IrInstruction* instruction = function->instructions + function->instruction_count;
    *instruction = (IrInstruction){
        .type = type,
        .entity = ANALYSIS_ENTITY_ID_INVALID,
        .local = ANALYSIS_LOCAL_ID_INVALID,
        .id = id,
        .next = IR_INSTRUCTION_ID_INVALID,
        .result = IR_VALUE_ID_INVALID,
        .source = source,
        .opcode = opcode,
        .ast_operation = AST_NODE_COUNT,
        .operand_count = operand_count,
    };
    if (operand_count)
    {
        instruction->operands = arena_allocate(builder->result_arena, IrValueId, operand_count);
        for (u32 index = 0; index < operand_count; index += 1)
        {
            instruction->operands[index] = operands[index];
        }
    }
    if (produces_value)
    {
        instruction->result = ir_value_create(builder, type, id, category);
    }
    if (block->last_instruction.value != IR_ID_UNDERLYING_INVALID)
    {
        function->instructions[block->last_instruction.value].next = id;
    }
    else
    {
        block->first_instruction = id;
    }
    block->last_instruction = id;
    function->instruction_count += 1;
    return instruction;
}

BUSTER_GLOBAL_LOCAL void ir_terminate(
    IrBuilder* builder,
    IrOpcode opcode,
    ParserSourceRange source,
    IrValueId* operands,
    u32 operand_count,
    IrBlockId* targets,
    u32 target_count)
{
    IrInstruction* instruction = ir_emit(
        builder,
        opcode,
        builder->analysis->types.builtin.void_type,
        IR_VALUE_VALUE,
        source,
        operands,
        operand_count,
        false);
    if (target_count)
    {
        instruction->targets = arena_allocate(builder->result_arena, IrBlockId, target_count);
        for (u32 index = 0; index < target_count; index += 1)
        {
            instruction->targets[index] = targets[index];
        }
        instruction->target_count = target_count;
        for (u32 index = 0; index < target_count; index += 1)
        {
            ir_predecessor_add(builder, targets[index], builder->current);
        }
    }
    builder->function->blocks[builder->current.value].terminated = true;
}

BUSTER_GLOBAL_LOCAL void ir_branch(IrBuilder* builder, IrBlockId target, ParserSourceRange source)
{
    ir_terminate(builder, IR_OPCODE_BRANCH, source, 0, 0, &target, 1);
}

BUSTER_GLOBAL_LOCAL AnalysisTypedExpression* ir_typed_expression_find(
    AnalysisBody* body,
    AstExpression ast)
{
    for (AnalysisTypedExpression* expression = body->first_expression;
        expression;
        expression = expression->next)
    {
        if (expression->ast.nodes == ast.nodes && expression->ast.count == ast.count)
        {
            return expression;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL IrValueId ir_materialize(IrBuilder* builder, IrLowered lowered, ParserSourceRange source)
{
    if (lowered.category == IR_VALUE_PLACE)
    {
        IrInstruction* load = ir_emit(
            builder,
            IR_OPCODE_LOAD,
            lowered.type,
            IR_VALUE_VALUE,
            source,
            &lowered.value,
            1,
            true);
        return load->result;
    }
    return lowered.value;
}

BUSTER_GLOBAL_LOCAL u32 ir_field_index(
    AnalysisResult* analysis,
    AnalysisTypeId aggregate_type,
    String8 name)
{
    AnalysisType* type = analysis_type_from_id(analysis, aggregate_type);
    if (type->kind == ANALYSIS_TYPE_STRUCT || type->kind == ANALYSIS_TYPE_UNION)
    {
        AnalysisResult* declaration_module = 0;
        if (analysis->module.id.value == type->as.declaration.module.value)
        {
            declaration_module = analysis;
        }
        else
        {
            for (u32 index = 0; index < analysis->program_module_count; index += 1)
            {
                AnalysisResult* candidate = analysis->program_modules[index];
                if (candidate &&
                    candidate->module.id.value ==
                            type->as.declaration.module.value)
                {
                    declaration_module = candidate;
                    break;
                }
            }
        }
        if (!declaration_module ||
            type->as.declaration.index.value >=
                    declaration_module->module.entity_count)
        {
            return UINT32_MAX;
        }
        AnalysisEntitySemantic* semantic =
                declaration_module->module.semantics +
                type->as.declaration.index.value;
        for (u32 index = 0; index < semantic->field_count; index += 1)
        {
            if (string_equal(semantic->fields[index].name, name))
            {
                return index;
            }
        }
    }
    return UINT32_MAX;
}

typedef enum IrExpressionTaskKind
{
    IR_EXPRESSION_TASK_VISIT,
    IR_EXPRESSION_TASK_EMIT,
    IR_EXPRESSION_TASK_SHORT_AFTER_LEFT,
    IR_EXPRESSION_TASK_SHORT_AFTER_RIGHT,
} IrExpressionTaskKind;

typedef struct IrExpressionTask IrExpressionTask;
struct IrExpressionTask
{
    IrBlockId merge;
    IrBlockId short_block;
    IrValueId short_value;
    u32 node_index;
    u32 right_index;
    IrExpressionTaskKind kind;
};

BUSTER_GLOBAL_LOCAL void ir_expression_child_roots(
    AnalysisTypedExpression* expression,
    u32 node_index,
    u32* roots,
    u32 arity)
{
    u32 cursor = node_index;
    for (u32 child = arity; child > 0; child -= 1)
    {
        cursor -= 1;
        roots[child - 1] = cursor;
        cursor = expression->nodes[cursor].subtree_start;
    }
}

BUSTER_GLOBAL_LOCAL IrLowered ir_lower_expression(IrBuilder* builder, AstExpression ast)
{
    IrLowered invalid = {
        .value = IR_VALUE_ID_INVALID,
        .type = builder->analysis->types.builtin.poison,
        .local = ANALYSIS_LOCAL_ID_INVALID,
        .category = IR_VALUE_VALUE,
    };
    if (!ast.count)
    {
        return invalid;
    }
    AnalysisTypedExpression* expression = ir_typed_expression_find(builder->body, ast);
    BUSTER_CHECK(expression);
    IrLowered* results = arena_allocate(builder->scratch_arena, IrLowered, ast.count);
    IrExpressionTask* tasks = arena_allocate(
        builder->scratch_arena,
        IrExpressionTask,
        ast.count * 3 + 1);
    u32 task_count = 1;
    tasks[0] = (IrExpressionTask){
        .node_index = ast.count - 1,
        .kind = IR_EXPRESSION_TASK_VISIT,
    };
    while (task_count)
    {
        IrExpressionTask task = tasks[task_count - 1];
        task_count -= 1;
        u32 node_index = task.node_index;
        AstNode* node = ast.nodes + node_index;
        AnalysisTypedNode* typed = expression->nodes + node_index;
        u32 arity = ir_node_arity(node);
        u32* roots = arena_allocate(builder->scratch_arena, u32, arity);
        ir_expression_child_roots(expression, node_index, roots, arity);
        bool short_circuit = node->id == AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT ||
            node->id == AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT;
        if (task.kind == IR_EXPRESSION_TASK_VISIT)
        {
            BUSTER_CHECK(task_count + arity + 1 <= ast.count * 3 + 1);
            if (short_circuit)
            {
                BUSTER_CHECK(arity == 2);
                tasks[task_count++] = (IrExpressionTask){
                    .node_index = node_index,
                    .right_index = roots[1],
                    .kind = IR_EXPRESSION_TASK_SHORT_AFTER_LEFT,
                };
                tasks[task_count++] = (IrExpressionTask){
                    .node_index = roots[0],
                    .kind = IR_EXPRESSION_TASK_VISIT,
                };
            }
            else
            {
                tasks[task_count++] = (IrExpressionTask){
                    .node_index = node_index,
                    .kind = IR_EXPRESSION_TASK_EMIT,
                };
                for (u32 child = arity; child > 0; child -= 1)
                {
                    tasks[task_count++] = (IrExpressionTask){
                        .node_index = roots[child - 1],
                        .kind = IR_EXPRESSION_TASK_VISIT,
                    };
                }
            }
            continue;
        }
        if (task.kind == IR_EXPRESSION_TASK_SHORT_AFTER_LEFT)
        {
            IrValueId condition = ir_materialize(builder, results[roots[0]], builder->entity->range);
            IrBlockId right_block = ir_block_create(builder);
            IrBlockId short_block = ir_block_create(builder);
            IrBlockId merge = ir_block_create(builder);
            IrBlockId targets[2] = { right_block, short_block };
            bool is_or = node->id == AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT;
            if (is_or)
            {
                targets[0] = short_block;
                targets[1] = right_block;
            }
            ir_terminate(
                builder,
                IR_OPCODE_BRANCH_IF,
                builder->entity->range,
                &condition,
                1,
                targets,
                2);
            builder->current = short_block;
            ir_block_seal(builder, short_block);
            IrInstruction* constant = ir_emit(
                builder,
                IR_OPCODE_CONSTANT_INTEGER,
                typed->type,
                IR_VALUE_VALUE,
                builder->entity->range,
                0,
                0,
                true);
            constant->immediates = arena_allocate(builder->result_arena, u64, 1);
            constant->immediates[0] = is_or;
            constant->immediate_count = 1;
            IrBlockId short_predecessor = builder->current;
            ir_branch(builder, merge, builder->entity->range);
            builder->current = right_block;
            ir_block_seal(builder, right_block);
            tasks[task_count++] = (IrExpressionTask){
                .merge = merge,
                .short_block = short_predecessor,
                .short_value = constant->result,
                .node_index = node_index,
                .right_index = task.right_index,
                .kind = IR_EXPRESSION_TASK_SHORT_AFTER_RIGHT,
            };
            tasks[task_count++] = (IrExpressionTask){
                .node_index = task.right_index,
                .kind = IR_EXPRESSION_TASK_VISIT,
            };
            continue;
        }
        if (task.kind == IR_EXPRESSION_TASK_SHORT_AFTER_RIGHT)
        {
            IrValueId right = ir_materialize(
                builder,
                results[task.right_index],
                builder->entity->range);
            IrBlockId right_predecessor = builder->current;
            ir_branch(builder, task.merge, builder->entity->range);
            ir_block_seal(builder, task.merge);
            IrBlockParameter* parameter = ir_block_parameter_create(
                builder,
                task.merge,
                ANALYSIS_LOCAL_ID_INVALID,
                typed->type);
            ir_block_parameter_incoming_add(
                builder,
                parameter,
                task.short_block,
                task.short_value);
            ir_block_parameter_incoming_add(
                builder,
                parameter,
                right_predecessor,
                right);
            results[node_index] = (IrLowered){
                .value = parameter->value,
                .type = typed->type,
                .local = ANALYSIS_LOCAL_ID_INVALID,
                .category = IR_VALUE_VALUE,
            };
            builder->current = task.merge;
            continue;
        }
        IrLowered lowered = invalid;
        lowered.type = typed->type;
        lowered.local = typed->local;
        bool semantic_place = typed->category == ANALYSIS_VALUE_CATEGORY_IMMUTABLE_PLACE ||
            typed->category == ANALYSIS_VALUE_CATEGORY_MUTABLE_PLACE;
        lowered.category = semantic_place ? IR_VALUE_PLACE : IR_VALUE_VALUE;
        IrInstruction* instruction = 0;
        ParserSourceRange source = builder->entity->range;
        switch (node->id)
        {
            case AST_NODE_CONSTANT_INTEGER:
            case AST_NODE_CONSTANT_CHARACTER:
            {
                instruction = ir_emit(
                    builder,
                    IR_OPCODE_CONSTANT_INTEGER,
                    typed->type,
                    IR_VALUE_VALUE,
                    source,
                    0,
                    0,
                    true);
                instruction->immediates = arena_allocate(builder->result_arena, u64, 1);
                instruction->immediates[0] = typed->constant.integer;
                instruction->immediate_count = 1;
                instruction->immediate_is_negative = typed->constant.is_negative;
            } break;
            case AST_NODE_CONSTANT_FLOAT:
            {
                instruction = ir_emit(
                    builder,
                    IR_OPCODE_CONSTANT_FLOAT,
                    typed->type,
                    IR_VALUE_VALUE,
                    source,
                    0,
                    0,
                    true);
                instruction->literal = node->floating.spelling;
            } break;
            case AST_NODE_CONSTANT_STRING:
            {
                instruction = ir_emit(
                    builder,
                    IR_OPCODE_CONSTANT_STRING,
                    typed->type,
                    IR_VALUE_VALUE,
                    source,
                    0,
                    0,
                    true);
                instruction->literal = node->string.value;
            } break;
            case AST_NODE_IDENTIFIER:
            {
                if (typed->is_namespace)
                {
                    lowered.value = IR_VALUE_ID_INVALID;
                    lowered.category = IR_VALUE_VALUE;
                }
                else if (typed->local.value != ANALYSIS_ID_UNDERLYING_INVALID)
                {
                    if (builder->function->local_uses_memory[typed->local.value])
                    {
                        lowered.value = builder->function->local_places[typed->local.value];
                        lowered.category = IR_VALUE_PLACE;
                    }
                    else
                    {
                        lowered.value = ir_ssa_read(builder, builder->current, typed->local);
                        BUSTER_CHECK(lowered.value.value != IR_ID_UNDERLYING_INVALID);
                        lowered.category = IR_VALUE_VALUE;
                    }
                }
                else if (typed->constant.kind != ANALYSIS_CONSTANT_NONE)
                {
                    instruction = ir_emit(
                        builder,
                        IR_OPCODE_CONSTANT_INTEGER,
                        typed->type,
                        IR_VALUE_VALUE,
                        source,
                        0,
                        0,
                        true);
                    instruction->immediates = arena_allocate(builder->result_arena, u64, 1);
                    instruction->immediates[0] = typed->constant.integer;
                    instruction->immediate_count = 1;
                }
                else
                {
                    instruction = ir_emit(
                        builder,
                        IR_OPCODE_FUNCTION,
                        typed->type,
                        IR_VALUE_VALUE,
                        source,
                        0,
                        0,
                        true);
                    instruction->entity = typed->entity;
                }
            } break;
            case AST_NODE_UNDEFINED:
            {
                instruction = ir_emit(
                    builder,
                    IR_OPCODE_UNDEFINED,
                    typed->type,
                    IR_VALUE_VALUE,
                    source,
                    0,
                    0,
                    true);
            } break;
            case AST_NODE_ENUM_LITERAL:
            {
                instruction = ir_emit(
                    builder,
                    IR_OPCODE_ENUM,
                    typed->type,
                    IR_VALUE_VALUE,
                    node->enum_literal.range,
                    0,
                    0,
                    true);
                instruction->immediates = arena_allocate(builder->result_arena, u64, 1);
                instruction->immediates[0] = typed->constant.integer;
                instruction->immediate_count = 1;
            } break;
            case AST_NODE_ARRAY_LITERAL:
            case AST_NODE_AGGREGATE_LITERAL:
            {
                IrValueId* operands = arena_allocate(builder->scratch_arena, IrValueId, arity);
                for (u32 index = 0; index < arity; index += 1)
                {
                    operands[index] = ir_materialize(builder, results[roots[index]], source);
                }
                instruction = ir_emit(
                    builder,
                    node->id == AST_NODE_ARRAY_LITERAL ? IR_OPCODE_ARRAY : IR_OPCODE_AGGREGATE,
                    typed->type,
                    IR_VALUE_VALUE,
                    source,
                    operands,
                    arity,
                    true);
                if (node->id == AST_NODE_AGGREGATE_LITERAL && arity)
                {
                    instruction->immediates = arena_allocate(builder->result_arena, u64, arity);
                    u32 field_index = 0;
                    for (AstAggregateLiteralField* field = node->aggregate_literal.first_field;
                        field;
                        field = field->next)
                    {
                        BUSTER_CHECK(field_index < arity);
                        instruction->immediates[field_index] = ir_field_index(
                            builder->analysis,
                            typed->type,
                            field->name.text);
                        field_index += 1;
                    }
                    BUSTER_CHECK(field_index == arity);
                    instruction->immediate_count = arity;
                }
            } break;
            case AST_NODE_ARRAY_INDEX:
            {
                IrValueId operands[2] = {
                    results[roots[0]].value,
                    ir_materialize(builder, results[roots[1]], source),
                };
                instruction = ir_emit(
                    builder,
                    IR_OPCODE_INDEX,
                    typed->type,
                    lowered.category,
                    node->array_index.range,
                    operands,
                    2,
                    true);
            } break;
            case AST_NODE_ARRAY_SLICE:
            {
                IrValueId* operands = arena_allocate(builder->scratch_arena, IrValueId, arity);
                operands[0] = results[roots[0]].value;
                for (u32 index = 1; index < arity; index += 1)
                {
                    operands[index] = ir_materialize(builder, results[roots[index]], source);
                }
                instruction = ir_emit(
                    builder,
                    IR_OPCODE_SLICE,
                    typed->type,
                    IR_VALUE_VALUE,
                    node->array_slice.range,
                    operands,
                    arity,
                    true);
                instruction->immediates = arena_allocate(builder->result_arena, u64, 2);
                instruction->immediates[0] = node->array_slice.has_start;
                instruction->immediates[1] = node->array_slice.has_end;
                instruction->immediate_count = 2;
            } break;
            case AST_NODE_MEMBER_ACCESS:
            {
                AnalysisTypedNode* base_typed =
                        expression->nodes + roots[0];
                if (base_typed->is_namespace)
                {
                    instruction = ir_emit(
                            builder,
                            IR_OPCODE_FUNCTION,
                            typed->type,
                            IR_VALUE_VALUE,
                            node->member_access.range,
                            0,
                            0,
                            true);
                    instruction->entity = typed->entity;
                }
                else
                {
                    IrValueId operand = results[roots[0]].value;
                    instruction = ir_emit(
                        builder,
                        IR_OPCODE_FIELD,
                        typed->type,
                        lowered.category,
                        node->member_access.range,
                        &operand,
                        1,
                        true);
                    instruction->immediates = arena_allocate(builder->result_arena, u64, 1);
                    instruction->immediates[0] = ir_field_index(
                        builder->analysis,
                        results[roots[0]].type,
                        node->member_access.member.text);
                    instruction->immediate_count = 1;
                }
            } break;
            case AST_NODE_CALL:
            {
                IrValueId* operands = arena_allocate(builder->scratch_arena, IrValueId, arity);
                for (u32 index = 0; index < arity; index += 1)
                {
                    operands[index] = ir_materialize(builder, results[roots[index]], source);
                }
                instruction = ir_emit(
                    builder,
                    IR_OPCODE_CALL,
                    typed->type,
                    IR_VALUE_VALUE,
                    node->call.range,
                    operands,
                    arity,
                    analysis_type_from_id(builder->analysis, typed->type)->kind != ANALYSIS_TYPE_VOID);
            } break;
            case AST_NODE_INTRINSIC_CALL:
            {
                IrValueId* operands = arena_allocate(builder->scratch_arena, IrValueId, arity);
                for (u32 index = 0; index < arity; index += 1)
                {
                    operands[index] = ir_materialize(builder, results[roots[index]], source);
                }
                IrOpcode opcode = string_equal(node->intrinsic_call.name.text, S8("cast")) ?
                    IR_OPCODE_CAST : IR_OPCODE_REVERSE;
                instruction = ir_emit(
                    builder,
                    opcode,
                    typed->type,
                    IR_VALUE_VALUE,
                    node->intrinsic_call.range,
                    operands,
                    arity,
                    true);
            } break;
            case AST_NODE_ADDRESS_OF:
            {
                IrValueId operand = results[roots[0]].value;
                instruction = ir_emit(
                    builder,
                    IR_OPCODE_ADDRESS_OF,
                    typed->type,
                    IR_VALUE_VALUE,
                    node->pointer_operator.range,
                    &operand,
                    1,
                    true);
            } break;
            case AST_NODE_DEREFERENCE:
            {
                IrValueId operand = ir_materialize(builder, results[roots[0]], source);
                instruction = ir_emit(
                    builder,
                    IR_OPCODE_DEREFERENCE,
                    typed->type,
                    IR_VALUE_PLACE,
                    node->pointer_operator.range,
                    &operand,
                    1,
                    true);
            } break;
            case AST_NODE_UNARY_MINUS:
            case AST_NODE_UNARY_PLUS:
            case AST_NODE_UNARY_LOGICAL_NOT:
            case AST_NODE_UNARY_BITWISE_NOT:
            {
                IrValueId operand = ir_materialize(builder, results[roots[0]], source);
                instruction = ir_emit(
                    builder,
                    IR_OPCODE_UNARY,
                    typed->type,
                    IR_VALUE_VALUE,
                    source,
                    &operand,
                    1,
                    true);
                instruction->ast_operation = node->id;
            } break;
            case AST_NODE_BINARY_PLUS:
            case AST_NODE_BINARY_MINUS:
            case AST_NODE_BINARY_ASTERISK:
            case AST_NODE_BINARY_SLASH:
            case AST_NODE_BINARY_PERCENT:
            case AST_NODE_BINARY_SHIFT_LEFT:
            case AST_NODE_BINARY_SHIFT_RIGHT:
            case AST_NODE_BINARY_EQUAL:
            case AST_NODE_BINARY_NOT_EQUAL:
            case AST_NODE_BINARY_LESS:
            case AST_NODE_BINARY_LESS_EQUAL:
            case AST_NODE_BINARY_GREATER:
            case AST_NODE_BINARY_GREATER_EQUAL:
            case AST_NODE_BINARY_AMPERSAND:
            case AST_NODE_BINARY_BAR:
            case AST_NODE_BINARY_CARET:
            case AST_NODE_BINARY_BOOLEAN_AND:
            case AST_NODE_BINARY_BOOLEAN_OR:
            case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
            case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
            case AST_NODE_BINARY_RANGE:
            {
                IrValueId operands[2] = {
                    ir_materialize(builder, results[roots[0]], source),
                    ir_materialize(builder, results[roots[1]], source),
                };
                instruction = ir_emit(
                    builder,
                    IR_OPCODE_BINARY,
                    typed->type,
                    IR_VALUE_VALUE,
                    source,
                    operands,
                    2,
                    true);
                instruction->ast_operation = node->id;
            } break;
            case AST_NODE_COUNT: break;
        }
        if (instruction)
        {
            instruction->conversion = typed->conversion;
            lowered.value = instruction->result;
            lowered.category = instruction->result.value == IR_ID_UNDERLYING_INVALID ?
                IR_VALUE_VALUE : builder->function->values[instruction->result.value].category;
        }
        results[node_index] = lowered;
    }
    return results[ast.count - 1];
}

BUSTER_GLOBAL_LOCAL AnalysisLocal* ir_local_find(AnalysisBody* body, AstIdentifier identifier)
{
    for (u32 index = 0; index < body->local_count; index += 1)
    {
        AnalysisLocal* local = body->locals + index;
        if (local->range.offset == identifier.range.offset && string_equal(local->name, identifier.text))
        {
            return local;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void ir_task_push(
    Arena* arena,
    IrLowerTask** top,
    AstStatement* statement,
    IrBlockId block,
    IrBlockId end,
    IrBlockId break_block,
    IrBlockId continue_block)
{
    IrLowerTask* task = arena_allocate(arena, IrLowerTask, 1);
    *task = (IrLowerTask){
        .previous = *top,
        .statement = statement,
        .block = block,
        .end = end,
        .break_block = break_block,
        .continue_block = continue_block,
        .kind = IR_LOWER_TASK_STATEMENT,
    };
    *top = task;
}

BUSTER_GLOBAL_LOCAL void ir_seal_task_push(
    Arena* arena,
    IrLowerTask** top,
    IrBlockId block)
{
    IrLowerTask* task = arena_allocate(arena, IrLowerTask, 1);
    *task = (IrLowerTask){
        .previous = *top,
        .block = block,
        .kind = IR_LOWER_TASK_SEAL_BLOCK,
    };
    *top = task;
}

BUSTER_GLOBAL_LOCAL AstNodeId ir_assignment_operation(AstAssignmentOperator operation)
{
    switch (operation)
    {
        case AST_ASSIGNMENT_PLUS_EQUAL: return AST_NODE_BINARY_PLUS;
        case AST_ASSIGNMENT_MINUS_EQUAL: return AST_NODE_BINARY_MINUS;
        case AST_ASSIGNMENT_MULTIPLY_EQUAL: return AST_NODE_BINARY_ASTERISK;
        case AST_ASSIGNMENT_DIVIDE_EQUAL: return AST_NODE_BINARY_SLASH;
        case AST_ASSIGNMENT_MODULO_EQUAL: return AST_NODE_BINARY_PERCENT;
        case AST_ASSIGNMENT_SHIFT_LEFT_EQUAL: return AST_NODE_BINARY_SHIFT_LEFT;
        case AST_ASSIGNMENT_SHIFT_RIGHT_EQUAL: return AST_NODE_BINARY_SHIFT_RIGHT;
        case AST_ASSIGNMENT_BITWISE_AND_EQUAL: return AST_NODE_BINARY_AMPERSAND;
        case AST_ASSIGNMENT_BITWISE_OR_EQUAL: return AST_NODE_BINARY_BAR;
        case AST_ASSIGNMENT_BITWISE_XOR_EQUAL: return AST_NODE_BINARY_CARET;
        case AST_ASSIGNMENT_EQUAL:
        case AST_ASSIGNMENT_COUNT: break;
    }
    return AST_NODE_COUNT;
}

BUSTER_GLOBAL_LOCAL void ir_lower_statement_task(
    IrBuilder* builder,
    IrLowerTask** top,
    IrLowerTask* task)
{
    if (task->kind == IR_LOWER_TASK_SEAL_BLOCK)
    {
        ir_block_seal(builder, task->block);
        return;
    }
    ir_block_seal(builder, task->block);
    builder->current = task->block;
    if (!task->statement)
    {
        if (!builder->function->blocks[task->block.value].terminated)
        {
            ir_branch(builder, task->end, builder->entity->range);
        }
        return;
    }
    AstStatement* statement = task->statement;
    switch (statement->id)
    {
        case AST_STATEMENT_RETURN:
        {
            IrValueId operand = IR_VALUE_ID_INVALID;
            u32 operand_count = 0;
            if (statement->return_statement.expression.count)
            {
                IrLowered lowered = ir_lower_expression(builder, statement->return_statement.expression);
                operand = ir_materialize(builder, lowered, statement->range);
                operand_count = 1;
            }
            ir_terminate(
                builder,
                IR_OPCODE_RETURN,
                statement->range,
                operand_count ? &operand : 0,
                operand_count,
                0,
                0);
        } break;
        case AST_STATEMENT_DATA:
        {
            AstDataStatement* data = &statement->data_statement;
            AnalysisLocal* local = ir_local_find(builder->body, data->name);
            BUSTER_CHECK(local);
            IrLowered initializer = ir_lower_expression(builder, data->initializer);
            IrValueId operands[2] = {
                builder->function->local_places[local->id.value],
                ir_materialize(builder, initializer, statement->range),
            };
            if (builder->function->local_uses_memory[local->id.value])
            {
                ir_emit(
                    builder,
                    IR_OPCODE_STORE,
                    builder->analysis->types.builtin.void_type,
                    IR_VALUE_VALUE,
                    statement->range,
                    operands,
                    2,
                    false);
            }
            else
            {
                ir_ssa_write(builder, task->block, local->id, operands[1]);
            }
            ir_task_push(
                builder->scratch_arena,
                top,
                statement->next,
                task->block,
                task->end,
                task->break_block,
                task->continue_block);
        } break;
        case AST_STATEMENT_EXPRESSION:
        {
            IrLowered expression = ir_lower_expression(
                builder,
                statement->expression_statement.expression);
            if (expression.value.value != IR_ID_UNDERLYING_INVALID && expression.category == IR_VALUE_PLACE)
            {
                ir_materialize(builder, expression, statement->range);
            }
            ir_task_push(
                builder->scratch_arena,
                top,
                statement->next,
                task->block,
                task->end,
                task->break_block,
                task->continue_block);
        } break;
        case AST_STATEMENT_ASSIGNMENT:
        {
            AstAssignmentStatement* assignment = &statement->assignment_statement;
            IrLowered target = ir_lower_expression(builder, assignment->target);
            IrLowered value = ir_lower_expression(builder, assignment->value);
            IrValueId stored = ir_materialize(builder, value, statement->range);
            if (assignment->operator != AST_ASSIGNMENT_EQUAL)
            {
                IrValueId operands[2] = {
                    ir_materialize(builder, target, statement->range),
                    stored,
                };
                IrInstruction* binary = ir_emit(
                    builder,
                    IR_OPCODE_BINARY,
                    target.type,
                    IR_VALUE_VALUE,
                    statement->range,
                    operands,
                    2,
                    true);
                binary->ast_operation = ir_assignment_operation(assignment->operator);
                stored = binary->result;
            }
            if (target.local.value != ANALYSIS_ID_UNDERLYING_INVALID &&
                !builder->function->local_uses_memory[target.local.value])
            {
                ir_ssa_write(builder, task->block, target.local, stored);
            }
            else
            {
                IrValueId operands[2] = { target.value, stored };
                ir_emit(
                    builder,
                    IR_OPCODE_STORE,
                    builder->analysis->types.builtin.void_type,
                    IR_VALUE_VALUE,
                    statement->range,
                    operands,
                    2,
                    false);
            }
            ir_task_push(
                builder->scratch_arena,
                top,
                statement->next,
                task->block,
                task->end,
                task->break_block,
                task->continue_block);
        } break;
        case AST_STATEMENT_IF:
        {
            AstIfStatement* conditional = &statement->if_statement;
            IrLowered condition = ir_lower_expression(builder, conditional->condition);
            IrValueId condition_value = ir_materialize(builder, condition, statement->range);
            IrBlockId then_block = ir_block_create(builder);
            IrBlockId else_block = ir_block_create(builder);
            IrBlockId merge = ir_block_create(builder);
            IrBlockId targets[2] = { then_block, else_block };
            ir_terminate(
                builder,
                IR_OPCODE_BRANCH_IF,
                statement->range,
                &condition_value,
                1,
                targets,
                2);
            ir_task_push(
                builder->scratch_arena,
                top,
                statement->next,
                merge,
                task->end,
                task->break_block,
                task->continue_block);
            if (conditional->alternative == AST_IF_ALTERNATIVE_IF)
            {
                ir_task_push(
                    builder->scratch_arena,
                    top,
                    conditional->else_if,
                    else_block,
                    merge,
                    task->break_block,
                    task->continue_block);
            }
            else
            {
                AstStatement* alternative = conditional->alternative == AST_IF_ALTERNATIVE_BLOCK ?
                    conditional->else_block.first_statement : 0;
                ir_task_push(
                    builder->scratch_arena,
                    top,
                    alternative,
                    else_block,
                    merge,
                    task->break_block,
                    task->continue_block);
            }
            ir_task_push(
                builder->scratch_arena,
                top,
                conditional->then_block.first_statement,
                then_block,
                merge,
                task->break_block,
                task->continue_block);
        } break;
        case AST_STATEMENT_SWITCH:
        {
            AstSwitchStatement* switch_statement = &statement->switch_statement;
            IrLowered switched = ir_lower_expression(builder, switch_statement->expression);
            IrValueId switched_value = ir_materialize(builder, switched, statement->range);
            IrBlockId merge = ir_block_create(builder);
            u32 non_else_count = switch_statement->case_count - (switch_statement->else_case ? 1u : 0u);
            IrBlockId* targets = arena_allocate(builder->scratch_arena, IrBlockId, non_else_count + 1);
            u64* values = arena_allocate(builder->scratch_arena, u64, non_else_count);
            AstSwitchCase** cases = arena_allocate(
                builder->scratch_arena,
                AstSwitchCase*,
                switch_statement->case_count);
            IrBlockId* case_blocks = arena_allocate(
                builder->scratch_arena,
                IrBlockId,
                switch_statement->case_count);
            u32 value_index = 0;
            u32 case_index = 0;
            IrBlockId default_block = merge;
            for (AstSwitchCase* switch_case = switch_statement->first_case;
                switch_case;
                switch_case = switch_case->next)
            {
                IrBlockId case_block = ir_block_create(builder);
                cases[case_index] = switch_case;
                case_blocks[case_index] = case_block;
                case_index += 1;
                if (switch_case->is_else)
                {
                    default_block = case_block;
                }
                else
                {
                    AnalysisTypedExpression* case_expression = ir_typed_expression_find(
                        builder->body,
                        switch_case->expression);
                    BUSTER_CHECK(case_expression && case_expression->ast.count);
                    AnalysisTypedNode* root = case_expression->nodes + case_expression->ast.count - 1;
                    targets[value_index] = case_block;
                    values[value_index] = root->constant.integer;
                    value_index += 1;
                }
            }
            BUSTER_CHECK(value_index == non_else_count);
            targets[non_else_count] = default_block;
            IrInstruction* switch_ir = ir_emit(
                builder,
                IR_OPCODE_SWITCH,
                builder->analysis->types.builtin.void_type,
                IR_VALUE_VALUE,
                statement->range,
                &switched_value,
                1,
                false);
            switch_ir->targets = arena_allocate(builder->result_arena, IrBlockId, non_else_count + 1);
            switch_ir->immediates = arena_allocate(builder->result_arena, u64, non_else_count);
            for (u32 index = 0; index < non_else_count; index += 1)
            {
                switch_ir->targets[index] = targets[index];
                switch_ir->immediates[index] = values[index];
            }
            switch_ir->targets[non_else_count] = targets[non_else_count];
            switch_ir->target_count = non_else_count + 1;
            switch_ir->immediate_count = non_else_count;
            for (u32 index = 0; index < switch_ir->target_count; index += 1)
            {
                ir_predecessor_add(builder, switch_ir->targets[index], builder->current);
            }
            builder->function->blocks[builder->current.value].terminated = true;
            ir_task_push(
                builder->scratch_arena,
                top,
                statement->next,
                merge,
                task->end,
                task->break_block,
                task->continue_block);
            for (u32 index = case_index; index > 0; index -= 1)
            {
                ir_task_push(
                    builder->scratch_arena,
                    top,
                    cases[index - 1]->body.first_statement,
                    case_blocks[index - 1],
                    merge,
                    task->break_block,
                    task->continue_block);
            }
        } break;
        case AST_STATEMENT_FOR:
        {
            AstForStatement* for_statement = &statement->for_statement;
            IrLowered iterable = ir_lower_expression(builder, for_statement->iterable);
            IrValueId iterable_value = ir_materialize(builder, iterable, statement->range);
            IrInstruction* iterator = ir_emit(
                builder,
                IR_OPCODE_ITERATOR_BEGIN,
                iterable.type,
                IR_VALUE_VALUE,
                statement->range,
                &iterable_value,
                1,
                true);
            IrBlockId header = ir_block_create(builder);
            IrBlockId body_block = ir_block_create(builder);
            IrBlockId exit = ir_block_create(builder);
            ir_branch(builder, header, statement->range);
            builder->current = header;
            IrInstruction* next = ir_emit(
                builder,
                IR_OPCODE_ITERATOR_NEXT,
                builder->analysis->types.builtin.bool_type,
                IR_VALUE_VALUE,
                statement->range,
                &iterator->result,
                1,
                true);
            IrBlockId targets[2] = { body_block, exit };
            ir_terminate(
                builder,
                IR_OPCODE_BRANCH_IF,
                statement->range,
                &next->result,
                1,
                targets,
                2);
            builder->current = body_block;
            AnalysisLocal* local = ir_local_find(builder->body, for_statement->name);
            BUSTER_CHECK(local);
            IrInstruction* element = ir_emit(
                builder,
                IR_OPCODE_ITERATOR_VALUE,
                local->type,
                IR_VALUE_VALUE,
                statement->range,
                &iterator->result,
                1,
                true);
            IrValueId store_operands[2] = {
                builder->function->local_places[local->id.value],
                element->result,
            };
            if (builder->function->local_uses_memory[local->id.value])
            {
                ir_emit(
                    builder,
                    IR_OPCODE_STORE,
                    builder->analysis->types.builtin.void_type,
                    IR_VALUE_VALUE,
                    statement->range,
                    store_operands,
                    2,
                    false);
            }
            else
            {
                ir_ssa_write(builder, body_block, local->id, element->result);
            }
            ir_task_push(
                builder->scratch_arena,
                top,
                statement->next,
                exit,
                task->end,
                task->break_block,
                task->continue_block);
            ir_seal_task_push(builder->scratch_arena, top, header);
            ir_task_push(
                builder->scratch_arena,
                top,
                for_statement->body.first_statement,
                body_block,
                header,
                exit,
                header);
        } break;
        case AST_STATEMENT_LOOP:
        {
            AstLoopStatement* loop = &statement->loop_statement;
            IrBlockId header = ir_block_create(builder);
            IrBlockId body_block = ir_block_create(builder);
            IrBlockId exit = ir_block_create(builder);
            ir_branch(builder, header, statement->range);
            builder->current = header;
            if (loop->has_condition)
            {
                IrLowered condition = ir_lower_expression(builder, loop->condition);
                IrValueId condition_value = ir_materialize(builder, condition, statement->range);
                IrBlockId targets[2] = { body_block, exit };
                ir_terminate(
                    builder,
                    IR_OPCODE_BRANCH_IF,
                    statement->range,
                    &condition_value,
                    1,
                    targets,
                    2);
            }
            else
            {
                ir_branch(builder, body_block, statement->range);
            }
            ir_task_push(
                builder->scratch_arena,
                top,
                statement->next,
                exit,
                task->end,
                task->break_block,
                task->continue_block);
            ir_seal_task_push(builder->scratch_arena, top, header);
            ir_task_push(
                builder->scratch_arena,
                top,
                loop->body.first_statement,
                body_block,
                header,
                exit,
                header);
        } break;
        case AST_STATEMENT_BREAK:
        {
            BUSTER_CHECK(ir_block_id_valid(builder->function, task->break_block));
            ir_branch(builder, task->break_block, statement->range);
        } break;
        case AST_STATEMENT_CONTINUE:
        {
            BUSTER_CHECK(ir_block_id_valid(builder->function, task->continue_block));
            ir_branch(builder, task->continue_block, statement->range);
        } break;
        case AST_STATEMENT_COUNT: break;
    }
}

BUSTER_GLOBAL_LOCAL bool ir_entity_has_diagnostic(AnalysisResult* analysis, AnalysisEntityId entity)
{
    for (AnalysisDiagnostic* diagnostic = analysis->first_diagnostic;
        diagnostic;
        diagnostic = diagnostic->next)
    {
        if (ir_entity_id_equal(diagnostic->entity, entity))
        {
            return true;
        }
    }
    return false;
}

typedef struct IrMeasureTask IrMeasureTask;
struct IrMeasureTask
{
    IrMeasureTask* previous;
    AstStatement* statement;
};

BUSTER_GLOBAL_LOCAL void ir_measure_task_push(
    Arena* arena,
    IrMeasureTask** top,
    AstStatement* statement)
{
    if (statement)
    {
        IrMeasureTask* task = arena_allocate(arena, IrMeasureTask, 1);
        *task = (IrMeasureTask){ .previous = *top, .statement = statement };
        *top = task;
    }
}

BUSTER_GLOBAL_LOCAL void ir_function_measure(
    Arena* scratch_arena,
    AstCode* code,
    AnalysisBody* body,
    u32* instruction_capacity,
    u32* block_capacity,
    u32* value_capacity)
{
    u32 node_count = 0;
    u32 expression_control_block_count = 0;
    for (AnalysisTypedExpression* expression = body->first_expression;
        expression;
        expression = expression->next)
    {
        node_count += expression->ast.count;
        for (u32 node_index = 0; node_index < expression->ast.count; node_index += 1)
        {
            AstNodeId id = expression->ast.nodes[node_index].id;
            if (id == AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT ||
                id == AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT)
            {
                expression_control_block_count += 3;
            }
        }
    }
    u32 statement_count = 0;
    u32 control_block_count = 0;
    IrMeasureTask* top = 0;
    ir_measure_task_push(scratch_arena, &top, code->body.first_statement);
    while (top)
    {
        IrMeasureTask* task = top;
        top = task->previous;
        AstStatement* statement = task->statement;
        statement_count += 1;
        ir_measure_task_push(scratch_arena, &top, statement->next);
        switch (statement->id)
        {
            case AST_STATEMENT_IF:
            {
                control_block_count += 3;
                ir_measure_task_push(
                    scratch_arena,
                    &top,
                    statement->if_statement.then_block.first_statement);
                if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_BLOCK)
                {
                    ir_measure_task_push(
                        scratch_arena,
                        &top,
                        statement->if_statement.else_block.first_statement);
                }
                else if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_IF)
                {
                    ir_measure_task_push(scratch_arena, &top, statement->if_statement.else_if);
                }
            } break;
            case AST_STATEMENT_SWITCH:
            {
                control_block_count += statement->switch_statement.case_count + 1;
                for (AstSwitchCase* switch_case = statement->switch_statement.first_case;
                    switch_case;
                    switch_case = switch_case->next)
                {
                    ir_measure_task_push(scratch_arena, &top, switch_case->body.first_statement);
                }
            } break;
            case AST_STATEMENT_FOR:
            {
                control_block_count += 3;
                ir_measure_task_push(
                    scratch_arena,
                    &top,
                    statement->for_statement.body.first_statement);
            } break;
            case AST_STATEMENT_LOOP:
            {
                control_block_count += 3;
                ir_measure_task_push(
                    scratch_arena,
                    &top,
                    statement->loop_statement.body.first_statement);
            } break;
            case AST_STATEMENT_RETURN:
            case AST_STATEMENT_DATA:
            case AST_STATEMENT_EXPRESSION:
            case AST_STATEMENT_ASSIGNMENT:
            case AST_STATEMENT_BREAK:
            case AST_STATEMENT_CONTINUE:
            case AST_STATEMENT_COUNT: break;
        }
    }
    *block_capacity = 2 + control_block_count + expression_control_block_count;
    *instruction_capacity = node_count * 4 + statement_count * 8 +
        body->local_count * 3 + *block_capacity * 2 + 32;
    *value_capacity = *instruction_capacity;
}

BUSTER_GLOBAL_LOCAL void ir_lower_function(
    Arena* result_arena,
    Arena* scratch_arena,
    AnalysisResult* analysis,
    AnalysisEntity* entity,
    IrFunction* function)
{
    u32 entity_index = entity->id.index.value;
    AnalysisBody* body = analysis->module.bodies + entity_index;
    AnalysisTypeId function_type_id = analysis->module.semantics[entity_index].type;
    AnalysisType* function_type = analysis_type_from_id(analysis, function_type_id);
    BUSTER_CHECK(function_type->kind == ANALYSIS_TYPE_FUNCTION);
    function->name = entity->name;
    function->entity = entity->id;
    function->type = function_type_id;
    function->local_count = body->local_count;
    ir_function_measure(
        scratch_arena,
        entity->ast.code,
        body,
        &function->instruction_capacity,
        &function->block_capacity,
        &function->value_capacity);
    function->blocks = arena_allocate(result_arena, IrBlock, function->block_capacity);
    function->instructions = arena_allocate(
        result_arena,
        IrInstruction,
        function->instruction_capacity);
    function->values = arena_allocate(result_arena, IrValue, function->value_capacity);
    function->local_places = arena_allocate(result_arena, IrValueId, function->local_count);
    function->local_uses_memory = arena_allocate(result_arena, bool, function->local_count);
    IrBuilder builder = {
        .result_arena = result_arena,
        .scratch_arena = scratch_arena,
        .analysis = analysis,
        .entity = entity,
        .body = body,
        .function = function,
    };
    function->entry = ir_block_create(&builder);
    builder.current = function->entry;
    for (u32 local_index = 0; local_index < body->local_count; local_index += 1)
    {
        AnalysisLocal* local = body->locals + local_index;
        function->local_places[local_index] = IR_VALUE_ID_INVALID;
        function->local_uses_memory[local_index] =
            local->address_taken || local->requires_storage;
        if (!function->local_uses_memory[local_index])
        {
            continue;
        }
        IrInstruction* storage = ir_emit(
            &builder,
            IR_OPCODE_LOCAL,
            local->type,
            IR_VALUE_PLACE,
            local->range,
            0,
            0,
            true);
        storage->local = local->id;
        function->local_places[local_index] = storage->result;
    }
    u32 argument_index = 0;
    for (u32 local_index = 0; local_index < body->local_count; local_index += 1)
    {
        AnalysisLocal* local = body->locals + local_index;
        if (local->kind != ANALYSIS_LOCAL_ARGUMENT)
        {
            continue;
        }
        IrInstruction* argument = ir_emit(
            &builder,
            IR_OPCODE_ARGUMENT,
            local->type,
            IR_VALUE_VALUE,
            local->range,
            0,
            0,
            true);
        argument->immediates = arena_allocate(result_arena, u64, 1);
        argument->immediates[0] = argument_index;
        argument->immediate_count = 1;
        IrValueId operands[2] = { function->local_places[local_index], argument->result };
        if (function->local_uses_memory[local_index])
        {
            ir_emit(
                &builder,
                IR_OPCODE_STORE,
                analysis->types.builtin.void_type,
                IR_VALUE_VALUE,
                local->range,
                operands,
                2,
                false);
        }
        else
        {
            ir_ssa_write(&builder, function->entry, local->id, argument->result);
        }
        argument_index += 1;
    }
    BUSTER_CHECK(argument_index == function_type->as.function.argument_count);
    IrBlockId exit = ir_block_create(&builder);
    builder.current = exit;
    if (analysis_type_from_id(analysis, function_type->as.function.return_type)->kind == ANALYSIS_TYPE_VOID)
    {
        ir_terminate(&builder, IR_OPCODE_RETURN, entity->range, 0, 0, 0, 0);
    }
    else
    {
        ir_terminate(&builder, IR_OPCODE_UNREACHABLE, entity->range, 0, 0, 0, 0);
    }
    IrLowerTask* top = 0;
    ir_task_push(
        scratch_arena,
        &top,
        entity->ast.code->body.first_statement,
        function->entry,
        exit,
        IR_BLOCK_ID_INVALID,
        IR_BLOCK_ID_INVALID);
    while (top)
    {
        IrLowerTask* task = top;
        top = task->previous;
        ir_lower_statement_task(&builder, &top, task);
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        ir_block_seal(&builder, (IrBlockId){ .value = block_index });
    }
    function->state = IR_FUNCTION_LOWERED;
}

BUSTER_GLOBAL_LOCAL IrModule ir_module_initialize(
    Arena* result_arena,
    AnalysisResult* analysis)
{
    IrModule module = {
        .name = analysis->module.name,
        .function_count = analysis->module.code_count,
    };
    module.functions = arena_allocate(result_arena, IrFunction, module.function_count);
    u32 function_index = 0;
    for (u32 entity_index = 0; entity_index < analysis->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = analysis->module.entities + entity_index;
        if (entity->kind != ANALYSIS_ENTITY_CODE)
        {
            continue;
        }
        module.functions[function_index] = (IrFunction){
            .name = entity->name,
            .entity = entity->id,
            .type = analysis->module.semantics[entity_index].type,
            .id = { .value = function_index },
            .entry = IR_BLOCK_ID_INVALID,
            .state = entity->ast.code->has_body ?
                IR_FUNCTION_NOT_LOWERED : IR_FUNCTION_DECLARATION,
        };
        function_index += 1;
    }
    BUSTER_CHECK(function_index == module.function_count);
    return module;
}

BUSTER_GLOBAL_LOCAL IrFunction* ir_function_from_entity(
    IrModule* module,
    AnalysisEntityId entity)
{
    for (u32 index = 0; index < module->function_count; index += 1)
    {
        if (ir_entity_id_equal(module->functions[index].entity, entity))
        {
            return module->functions + index;
        }
    }
    return 0;
}

typedef struct IrAnalysisConsumerContext IrAnalysisConsumerContext;
struct IrAnalysisConsumerContext
{
    IrModule* module;
};

BUSTER_GLOBAL_LOCAL void ir_analysis_body_consume(
    Arena* result_arena,
    Arena* scratch_arena,
    AnalysisResult* analysis,
    u32 entity_index,
    void* user_data)
{
    IrAnalysisConsumerContext* context = (IrAnalysisConsumerContext*)user_data;
    AnalysisEntity* entity = analysis->module.entities + entity_index;
    IrFunction* function = ir_function_from_entity(context->module, entity->id);
    BUSTER_CHECK(function && function->state == IR_FUNCTION_NOT_LOWERED);
    if (ir_entity_has_diagnostic(analysis, entity->id))
    {
        function->state = IR_FUNCTION_REJECTED;
        context->module->rejected_function_count += 1;
    }
    else
    {
        ir_lower_function(result_arena, scratch_arena, analysis, entity, function);
        context->module->lowered_function_count += 1;
    }
}

IrModule ir_generate_module(Arena* result_arena, AnalysisResult* analysis)
{
    BUSTER_CHECK(analysis && analysis->types.types);
    IrModule module = ir_module_initialize(result_arena, analysis);
    Arena* conflicts[] = { result_arena };
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    u32 function_index = 0;
    for (u32 entity_index = 0; entity_index < analysis->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = analysis->module.entities + entity_index;
        if (entity->kind != ANALYSIS_ENTITY_CODE)
        {
            continue;
        }
        IrFunction* function = module.functions + function_index;
        if (!entity->ast.code->has_body)
        {
            BUSTER_CHECK(function->state == IR_FUNCTION_DECLARATION);
        }
        else if (!analysis->module.bodies[entity_index].analyzed ||
            ir_entity_has_diagnostic(analysis, entity->id))
        {
            function->state = IR_FUNCTION_REJECTED;
            module.rejected_function_count += 1;
        }
        else
        {
            ir_lower_function(result_arena, scratch.arena, analysis, entity, function);
            module.lowered_function_count += 1;
        }
        function_index += 1;
    }
    BUSTER_CHECK(function_index == module.function_count);
    scratch_end(scratch);
    return module;
}

IrModule ir_analyze_and_generate_module(Arena* result_arena, AnalysisResult* analysis)
{
    BUSTER_CHECK(analysis && analysis->types.types);
    IrModule module = ir_module_initialize(result_arena, analysis);
    IrAnalysisConsumerContext context = { .module = &module };
    analysis_analyze_bodies_with_consumer(
        result_arena,
        analysis,
        ir_analysis_body_consume,
        &context);
    return module;
}

BUSTER_GLOBAL_LOCAL IrValidationResult ir_validation_error(
    IrValidationError error,
    IrFunction* function,
    IrBlockId block,
    IrInstructionId instruction)
{
    return (IrValidationResult){
        .error = error,
        .function = function->id,
        .block = block,
        .instruction = instruction,
    };
}

IrValidationResult ir_validate_module(AnalysisResult* analysis, IrModule* module)
{
    IrValidationResult result = {
        .function = IR_FUNCTION_ID_INVALID,
        .block = IR_BLOCK_ID_INVALID,
        .instruction = IR_INSTRUCTION_ID_INVALID,
    };
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        if (function->state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        if (!ir_block_id_valid(function, function->entry))
        {
            return ir_validation_error(
                IR_VALIDATION_INVALID_ID,
                function,
                IR_BLOCK_ID_INVALID,
                IR_INSTRUCTION_ID_INVALID);
        }
        AnalysisType* function_type = analysis_type_from_id(analysis, function->type);
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            if (!block->terminated)
            {
                return ir_validation_error(
                    IR_VALIDATION_UNTERMINATED_BLOCK,
                    function,
                    block->id,
                    block->last_instruction);
            }
            if (!block->sealed)
            {
                return ir_validation_error(
                    IR_VALIDATION_BLOCK_PARAMETER,
                    function,
                    block->id,
                    IR_INSTRUCTION_ID_INVALID);
            }
            for (IrBlockParameter* parameter = block->first_parameter;
                parameter;
                parameter = parameter->next)
            {
                if (!ir_value_id_valid(function, parameter->value) ||
                    parameter->incoming_count != block->predecessor_count ||
                    !ir_type_id_equal(function->values[parameter->value.value].type, parameter->type))
                {
                    return ir_validation_error(
                        IR_VALIDATION_BLOCK_PARAMETER,
                        function,
                        block->id,
                        IR_INSTRUCTION_ID_INVALID);
                }
                IrIncoming* incoming = parameter->first_incoming;
                IrPredecessor* predecessor = block->first_predecessor;
                while (incoming && predecessor)
                {
                    if (incoming->predecessor.value != predecessor->block.value ||
                        !ir_value_id_valid(function, incoming->value) ||
                        !ir_type_id_equal(
                            function->values[incoming->value.value].type,
                            parameter->type))
                    {
                        return ir_validation_error(
                            IR_VALIDATION_BLOCK_PARAMETER,
                            function,
                            block->id,
                            IR_INSTRUCTION_ID_INVALID);
                    }
                    incoming = incoming->next;
                    predecessor = predecessor->next;
                }
                if (incoming || predecessor)
                {
                    return ir_validation_error(
                        IR_VALIDATION_BLOCK_PARAMETER,
                        function,
                        block->id,
                        IR_INSTRUCTION_ID_INVALID);
                }
            }
            IrInstructionId instruction_id = block->first_instruction;
            bool saw_terminator = false;
            while (instruction_id.value != IR_ID_UNDERLYING_INVALID)
            {
                if (instruction_id.value >= function->instruction_count)
                {
                    return ir_validation_error(
                        IR_VALIDATION_INVALID_ID,
                        function,
                        block->id,
                        instruction_id);
                }
                IrInstruction* instruction = function->instructions + instruction_id.value;
                if (saw_terminator)
                {
                    return ir_validation_error(
                        IR_VALIDATION_INSTRUCTION_AFTER_TERMINATOR,
                        function,
                        block->id,
                        instruction_id);
                }
                for (u32 operand_index = 0;
                    operand_index < instruction->operand_count;
                    operand_index += 1)
                {
                    if (!ir_value_id_valid(function, instruction->operands[operand_index]))
                    {
                        return ir_validation_error(
                            IR_VALIDATION_INVALID_ID,
                            function,
                            block->id,
                            instruction_id);
                    }
                }
                for (u32 target_index = 0;
                    target_index < instruction->target_count;
                    target_index += 1)
                {
                    if (!ir_block_id_valid(function, instruction->targets[target_index]))
                    {
                        return ir_validation_error(
                            IR_VALIDATION_BRANCH_TARGET,
                            function,
                            block->id,
                            instruction_id);
                    }
                }
                if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
                {
                    if (!ir_value_id_valid(function, instruction->result) ||
                        !ir_type_id_equal(
                            function->values[instruction->result.value].type,
                            instruction->type))
                    {
                        return ir_validation_error(
                            IR_VALIDATION_RESULT_TYPE,
                            function,
                            block->id,
                            instruction_id);
                    }
                }
                if (instruction->opcode == IR_OPCODE_LOAD)
                {
                    IrValue* place = function->values + instruction->operands[0].value;
                    if (place->category != IR_VALUE_PLACE || !ir_type_id_equal(place->type, instruction->type))
                    {
                        return ir_validation_error(
                            IR_VALIDATION_OPERAND_TYPE,
                            function,
                            block->id,
                            instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_STORE)
                {
                    IrValue* place = function->values + instruction->operands[0].value;
                    IrValue* value = function->values + instruction->operands[1].value;
                    if (place->category != IR_VALUE_PLACE || !ir_type_id_equal(place->type, value->type))
                    {
                        return ir_validation_error(
                            IR_VALIDATION_OPERAND_TYPE,
                            function,
                            block->id,
                            instruction_id);
                    }
                }
                else if (instruction->opcode == IR_OPCODE_RETURN)
                {
                    AnalysisTypeId return_type = function_type->as.function.return_type;
                    AnalysisTypeKind return_kind = analysis_type_from_id(analysis, return_type)->kind;
                    bool valid_return = return_kind == ANALYSIS_TYPE_VOID ?
                        instruction->operand_count == 0 :
                        instruction->operand_count == 1 && ir_type_id_equal(
                            function->values[instruction->operands[0].value].type,
                            return_type);
                    if (!valid_return)
                    {
                        return ir_validation_error(
                            IR_VALIDATION_RETURN_TYPE,
                            function,
                            block->id,
                            instruction_id);
                    }
                }
                bool terminator = instruction->opcode == IR_OPCODE_BRANCH ||
                    instruction->opcode == IR_OPCODE_BRANCH_IF ||
                    instruction->opcode == IR_OPCODE_SWITCH ||
                    instruction->opcode == IR_OPCODE_RETURN ||
                    instruction->opcode == IR_OPCODE_UNREACHABLE;
                saw_terminator = terminator;
                instruction_id = instruction->next;
            }
            if (!saw_terminator)
            {
                return ir_validation_error(
                    IR_VALIDATION_UNTERMINATED_BLOCK,
                    function,
                    block->id,
                    block->last_instruction);
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 ir_opcode_name(IrOpcode opcode)
{
    switch (opcode)
    {
        case IR_OPCODE_ARGUMENT: return S8("argument");
        case IR_OPCODE_LOCAL: return S8("local");
        case IR_OPCODE_LOAD: return S8("load");
        case IR_OPCODE_STORE: return S8("store");
        case IR_OPCODE_CONSTANT_INTEGER: return S8("constant_integer");
        case IR_OPCODE_CONSTANT_FLOAT: return S8("constant_float");
        case IR_OPCODE_CONSTANT_STRING: return S8("constant_string");
        case IR_OPCODE_UNDEFINED: return S8("undefined");
        case IR_OPCODE_FUNCTION: return S8("function_reference");
        case IR_OPCODE_ARRAY: return S8("array");
        case IR_OPCODE_AGGREGATE: return S8("aggregate");
        case IR_OPCODE_INDEX: return S8("index");
        case IR_OPCODE_SLICE: return S8("slice");
        case IR_OPCODE_FIELD: return S8("field");
        case IR_OPCODE_ENUM: return S8("enum");
        case IR_OPCODE_CALL: return S8("call");
        case IR_OPCODE_CAST: return S8("cast");
        case IR_OPCODE_ADDRESS_OF: return S8("address_of");
        case IR_OPCODE_DEREFERENCE: return S8("dereference");
        case IR_OPCODE_UNARY: return S8("unary");
        case IR_OPCODE_BINARY: return S8("binary");
        case IR_OPCODE_REVERSE: return S8("reverse");
        case IR_OPCODE_ITERATOR_BEGIN: return S8("iterator_begin");
        case IR_OPCODE_ITERATOR_NEXT: return S8("iterator_next");
        case IR_OPCODE_ITERATOR_VALUE: return S8("iterator_value");
        case IR_OPCODE_BRANCH: return S8("branch");
        case IR_OPCODE_BRANCH_IF: return S8("branch_if");
        case IR_OPCODE_SWITCH: return S8("switch");
        case IR_OPCODE_RETURN: return S8("return");
        case IR_OPCODE_UNREACHABLE: return S8("unreachable");
        case IR_OPCODE_COUNT: break;
    }
    return S8("invalid");
}

String8 ir_print_module(Arena* arena, AnalysisResult* analysis, IrModule* module)
{
    BUSTER_UNUSED(analysis);
    u32 part_capacity = 2;
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        part_capacity += function->block_count + function->instruction_count + 2;
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            part_capacity += function->blocks[block_index].parameter_count;
        }
    }
    String8* parts = arena_allocate(arena, String8, part_capacity);
    u32 part_count = 0;
    parts[part_count++] = string_format(arena, S8("module {S8}\n"), module->name);
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        parts[part_count++] = string_format(
            arena,
            S8("function {S8} state={u32}\n"),
            function->name,
            (u32)function->state);
        if (function->state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            parts[part_count++] = string_format(arena, S8("  block {u32}:\n"), block_index);
            for (IrBlockParameter* parameter = block->first_parameter;
                parameter;
                parameter = parameter->next)
            {
                parts[part_count++] = string_format(
                    arena,
                    S8("    %{u32} = parameter local={u32} type={u32} incoming={u32}\n"),
                    parameter->value.value,
                    parameter->local.value,
                    parameter->type.value,
                    parameter->incoming_count);
            }
            for (IrInstructionId id = block->first_instruction;
                id.value != IR_ID_UNDERLYING_INVALID;
                id = function->instructions[id.value].next)
            {
                IrInstruction* instruction = function->instructions + id.value;
                parts[part_count++] = instruction->result.value == IR_ID_UNDERLYING_INVALID ?
                    string_format(
                        arena,
                        S8("    {S8} operands={u32} targets={u32}\n"),
                        ir_opcode_name(instruction->opcode),
                        instruction->operand_count,
                        instruction->target_count) :
                    string_format(
                        arena,
                        S8("    %{u32} = {S8} type={u32} operands={u32}\n"),
                        instruction->result.value,
                        ir_opcode_name(instruction->opcode),
                        instruction->type.value,
                        instruction->operand_count);
            }
        }
    }
    BUSTER_CHECK(part_count <= part_capacity);
    return string_join_arena(
        arena,
        (SliceString8){ .pointer = parts, .length = part_count },
        false);
}

#if BUSTER_INCLUDE_TESTS
typedef struct IrFixtureTest IrFixtureTest;
struct IrFixtureTest
{
    String8 path;
};

BUSTER_GLOBAL_LOCAL IrFixtureTest ir_fixture_tests[] =
{
    { S8_INITIALIZER("tests/array_slices.bbb") },
    { S8_INITIALIZER("tests/basic_array_literal.bbb") },
    { S8_INITIALIZER("tests/basic_assignment.bbb") },
    { S8_INITIALIZER("tests/basic_binary_literal.bbb") },
    { S8_INITIALIZER("tests/basic_bitwise_not.bbb") },
    { S8_INITIALIZER("tests/basic_boolean_operators.bbb") },
    { S8_INITIALIZER("tests/basic_break.bbb") },
    { S8_INITIALIZER("tests/basic_character_literal.bbb") },
    { S8_INITIALIZER("tests/basic_comment.bbb") },
    { S8_INITIALIZER("tests/basic_continue.bbb") },
    { S8_INITIALIZER("tests/basic_else_if.bbb") },
    { S8_INITIALIZER("tests/basic_enum.bbb") },
    { S8_INITIALIZER("tests/basic_float.bbb") },
    { S8_INITIALIZER("tests/basic_for.bbb") },
    { S8_INITIALIZER("tests/basic_function_call.bbb") },
    { S8_INITIALIZER("tests/basic_hexadecimal_literal.bbb") },
    { S8_INITIALIZER("tests/basic_if_else.bbb") },
    { S8_INITIALIZER("tests/basic_integer_literal_add.bbb") },
    { S8_INITIALIZER("tests/basic_integer_literal_and.bbb") },
    { S8_INITIALIZER("tests/basic_integer_literal_compare.bbb") },
    { S8_INITIALIZER("tests/basic_integer_literal_divide.bbb") },
    { S8_INITIALIZER("tests/basic_integer_literal_mod.bbb") },
    { S8_INITIALIZER("tests/basic_integer_literal_multiply.bbb") },
    { S8_INITIALIZER("tests/basic_integer_literal_or.bbb") },
    { S8_INITIALIZER("tests/basic_integer_literal_precedence.bbb") },
    { S8_INITIALIZER("tests/basic_integer_literal_shift_left.bbb") },
    { S8_INITIALIZER("tests/basic_integer_literal_shift_right.bbb") },
    { S8_INITIALIZER("tests/basic_integer_literal_sub.bbb") },
    { S8_INITIALIZER("tests/basic_integer_literal_xor.bbb") },
    { S8_INITIALIZER("tests/basic_logical_not.bbb") },
    { S8_INITIALIZER("tests/basic_loop.bbb") },
    { S8_INITIALIZER("tests/basic_import.bbb") },
    { S8_INITIALIZER("tests/basic_minimal.bbb") },
    { S8_INITIALIZER("tests/basic_octal_literal.bbb") },
    { S8_INITIALIZER("tests/basic_pointer.bbb") },
    { S8_INITIALIZER("tests/basic_string_literal.bbb") },
    { S8_INITIALIZER("tests/basic_struct.bbb") },
    { S8_INITIALIZER("tests/basic_switch.bbb") },
    { S8_INITIALIZER("tests/basic_type_alias.bbb") },
    { S8_INITIALIZER("tests/basic_unary_minus.bbb") },
    { S8_INITIALIZER("tests/basic_unary_plus.bbb") },
    { S8_INITIALIZER("tests/basic_union.bbb") },
    { S8_INITIALIZER("tests/basic_variable.bbb") },
};

BUSTER_GLOBAL_LOCAL u32 ir_test_opcode_count(IrFunction* function, IrOpcode opcode)
{
    u32 count = 0;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        count += function->instructions[index].opcode == opcode;
    }
    return count;
}

BUSTER_GLOBAL_LOCAL u32 ir_test_parameter_count(IrFunction* function)
{
    u32 count = 0;
    for (u32 index = 0; index < function->block_count; index += 1)
    {
        count += function->blocks[index].parameter_count;
    }
    return count;
}

BUSTER_GLOBAL_LOCAL u32 ir_test_conversion_count(
    IrModule* module,
    AnalysisConversionKind conversion)
{
    u32 count = 0;
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        for (u32 instruction_index = 0;
            instruction_index < function->instruction_count;
            instruction_index += 1)
        {
            count += function->instructions[instruction_index].conversion == conversion;
        }
    }
    return count;
}

UnitTestResult ir_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);
    String8 focused_source = S8(
        "code choose : fn (a: s32, b: s32) s32\n"
        "{\n"
        "    data value: s32 = a;\n"
        "    if (a < b)\n"
        "    {\n"
        "        value += b;\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        value -= b;\n"
        "    }\n"
        "    return @cast(value);\n"
        "}\n");
    TokenizerResult focused_tokens = tokenize(
        arguments->arena,
        focused_source.pointer,
        focused_source.length);
    ParserResult focused_parser = parser_parse(
        arguments->arena,
        expression_arena,
        focused_source,
        focused_tokens);
    BUSTER_TEST(arguments, focused_tokens.error_count == 0);
    BUSTER_TEST(arguments, focused_parser.diagnostic_count == 0);
    AnalysisSourceInput focused_input = {
        .path = S8("focused-ir.bbb"),
        .parser = &focused_parser,
    };
    AnalysisResult focused_analysis = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 900 },
        S8("focused-ir"),
        &focused_input,
        1);
    analysis_resolve_module_interfaces(arguments->arena, &focused_analysis);
    IrModule focused_module = ir_analyze_and_generate_module(arguments->arena, &focused_analysis);
    BUSTER_TEST(arguments, focused_analysis.diagnostic_count == 0);
    u32 explicit_conversion_count = 0;
    AnalysisBody* focused_body = focused_analysis.module.bodies;
    for (AnalysisTypedExpression* expression = focused_body->first_expression;
        expression;
        expression = expression->next)
    {
        for (u32 node_index = 0; node_index < expression->ast.count; node_index += 1)
        {
            explicit_conversion_count +=
                expression->nodes[node_index].conversion == ANALYSIS_CONVERSION_EXPLICIT;
        }
    }
    BUSTER_TEST(arguments, explicit_conversion_count == 1);
    BUSTER_TEST(arguments, focused_module.function_count == 1);
    BUSTER_TEST(arguments, focused_module.lowered_function_count == 1);
    IrFunction* focused_function = focused_module.functions;
    BUSTER_TEST(arguments, focused_function->state == IR_FUNCTION_LOWERED);
    BUSTER_TEST(arguments, focused_function->block_count == 5);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_BRANCH_IF) == 1);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_BINARY) == 3);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_CAST) == 1);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_STORE) == 0);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_LOCAL) == 0);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_LOAD) == 0);
    BUSTER_TEST(arguments, ir_test_parameter_count(focused_function) == 1);
    BUSTER_TEST(arguments, ir_test_opcode_count(focused_function, IR_OPCODE_RETURN) == 1);
    IrValidationResult focused_validation = ir_validate_module(&focused_analysis, &focused_module);
    BUSTER_TEST(arguments, focused_validation.error == IR_VALIDATION_NONE);

    String8 short_source = S8(
        "code side : fn () bool\n"
        "{\n"
        "    return true;\n"
        "}\n"
        "code lazy : fn (condition: bool) bool\n"
        "{\n"
        "    return condition and? side() or? condition;\n"
        "}\n");
    TokenizerResult short_tokens = tokenize(
        arguments->arena,
        short_source.pointer,
        short_source.length);
    ParserResult short_parser = parser_parse(
        arguments->arena,
        expression_arena,
        short_source,
        short_tokens);
    BUSTER_TEST(arguments, short_tokens.error_count == 0);
    BUSTER_TEST(arguments, short_parser.diagnostic_count == 0);
    AnalysisSourceInput short_input = {
        .path = S8("short-circuit-ir.bbb"),
        .parser = &short_parser,
    };
    AnalysisResult short_analysis = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 901 },
        S8("short-circuit-ir"),
        &short_input,
        1);
    analysis_resolve_module_interfaces(arguments->arena, &short_analysis);
    IrModule short_module = ir_analyze_and_generate_module(arguments->arena, &short_analysis);
    BUSTER_TEST(arguments, short_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, short_module.function_count == 2);
    BUSTER_TEST(arguments, short_module.lowered_function_count == 2);
    IrFunction* lazy_function = 0;
    for (u32 function_index = 0; function_index < short_module.function_count; function_index += 1)
    {
        if (string_equal(short_module.functions[function_index].name, S8("lazy")))
        {
            lazy_function = short_module.functions + function_index;
        }
    }
    BUSTER_TEST(arguments, lazy_function != 0);
    if (lazy_function)
    {
        BUSTER_TEST(arguments, lazy_function->block_count == 8);
        BUSTER_TEST(arguments, ir_test_opcode_count(lazy_function, IR_OPCODE_BRANCH_IF) == 2);
        BUSTER_TEST(arguments, ir_test_opcode_count(lazy_function, IR_OPCODE_CALL) == 1);
        BUSTER_TEST(arguments, ir_test_parameter_count(lazy_function) >= 2);
        BUSTER_TEST(arguments, ir_test_opcode_count(lazy_function, IR_OPCODE_BINARY) == 0);
    }
    IrValidationResult short_validation = ir_validate_module(&short_analysis, &short_module);
    BUSTER_TEST(arguments, short_validation.error == IR_VALIDATION_NONE);

    String8 conversion_source = S8(
        "code literal : fn () u8 { return 1; }\n"
        "code widen : fn (value: u8) s32 { return @cast(value); }\n"
        "code narrow : fn (value: s32) u8 { return @cast(value); }\n"
        "code pointer : fn (value: &s32) &u8 { return @cast(value); }\n");
    TokenizerResult conversion_tokens = tokenize(
        arguments->arena,
        conversion_source.pointer,
        conversion_source.length);
    ParserResult conversion_parser = parser_parse(
        arguments->arena,
        expression_arena,
        conversion_source,
        conversion_tokens);
    BUSTER_TEST(arguments, conversion_tokens.error_count == 0);
    BUSTER_TEST(arguments, conversion_parser.diagnostic_count == 0);
    AnalysisSourceInput conversion_input = {
        .path = S8("conversion-ir.bbb"),
        .parser = &conversion_parser,
    };
    AnalysisResult conversion_analysis = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 902 },
        S8("conversion-ir"),
        &conversion_input,
        1);
    analysis_resolve_module_interfaces(arguments->arena, &conversion_analysis);
    IrModule conversion_module = ir_analyze_and_generate_module(
        arguments->arena,
        &conversion_analysis);
    BUSTER_TEST(arguments, conversion_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, conversion_module.lowered_function_count == 4);
    BUSTER_TEST(
        arguments,
        ir_test_conversion_count(&conversion_module, ANALYSIS_CONVERSION_LITERAL) == 1);
    BUSTER_TEST(
        arguments,
        ir_test_conversion_count(&conversion_module, ANALYSIS_CONVERSION_INTEGER_WIDEN) == 1);
    BUSTER_TEST(
        arguments,
        ir_test_conversion_count(&conversion_module, ANALYSIS_CONVERSION_INTEGER_NARROW) == 1);
    BUSTER_TEST(
        arguments,
        ir_test_conversion_count(&conversion_module, ANALYSIS_CONVERSION_POINTER) == 1);
    IrValidationResult conversion_validation = ir_validate_module(
        &conversion_analysis,
        &conversion_module);
    BUSTER_TEST(arguments, conversion_validation.error == IR_VALIDATION_NONE);

    String8 namespace_math_source = S8(
        "code add : fn (a: s32, b: s32) s32\n"
        "{\n"
        "    return a + b;\n"
        "}\n");
    String8 namespace_app_source = S8(
        "import math = \"core/math\";\n"
        "code use : fn () s32\n"
        "{\n"
        "    return math.add(2, 3);\n"
        "}\n");
    TokenizerResult namespace_math_tokens = tokenize(
            arguments->arena,
            namespace_math_source.pointer,
            namespace_math_source.length);
    ParserResult namespace_math_parser = parser_parse(
            arguments->arena,
            expression_arena,
            namespace_math_source,
            namespace_math_tokens);
    TokenizerResult namespace_app_tokens = tokenize(
            arguments->arena,
            namespace_app_source.pointer,
            namespace_app_source.length);
    ParserResult namespace_app_parser = parser_parse(
            arguments->arena,
            expression_arena,
            namespace_app_source,
            namespace_app_tokens);
    AnalysisSourceInput namespace_math_input = {
        .path = S8("math.bbb"),
        .parser = &namespace_math_parser,
    };
    AnalysisSourceInput namespace_app_input = {
        .path = S8("app.bbb"),
        .parser = &namespace_app_parser,
    };
    AnalysisResult namespace_math = analysis_index_module(
            arguments->arena,
            (AnalysisModuleId){ .value = 910 },
            S8("core/math"),
            &namespace_math_input,
            1);
    AnalysisResult namespace_app = analysis_index_module(
            arguments->arena,
            (AnalysisModuleId){ .value = 911 },
            S8("app"),
            &namespace_app_input,
            1);
    AnalysisResult* namespace_modules[] = {
        &namespace_math,
        &namespace_app,
    };
    analysis_resolve_program_interfaces(
            arguments->arena,
            namespace_modules,
            BUSTER_ARRAY_LENGTH(namespace_modules));
    IrModule namespace_module = ir_analyze_and_generate_module(
            arguments->arena,
            &namespace_app);
    BUSTER_TEST(arguments, namespace_app.diagnostic_count == 0);
    BUSTER_TEST(arguments, namespace_module.lowered_function_count == 1);
    BUSTER_TEST(arguments, namespace_module.function_count == 1);
    IrFunction* namespace_use = namespace_module.functions;
    BUSTER_TEST(arguments,
            ir_test_opcode_count(namespace_use, IR_OPCODE_FUNCTION) == 1);
    BUSTER_TEST(arguments,
            ir_test_opcode_count(namespace_use, IR_OPCODE_CALL) == 1);
    bool found_external_reference = false;
    for (u32 instruction_index = 0;
         instruction_index < namespace_use->instruction_count;
         instruction_index += 1)
    {
        IrInstruction* instruction =
                namespace_use->instructions + instruction_index;
        if (instruction->opcode == IR_OPCODE_FUNCTION)
        {
            found_external_reference =
                    instruction->entity.module.value == 910 &&
                    instruction->entity.index.value == 0;
        }
    }
    BUSTER_TEST(arguments, found_external_reference);
    IrValidationResult namespace_validation = ir_validate_module(
            &namespace_app,
            &namespace_module);
    BUSTER_TEST(arguments, namespace_validation.error == IR_VALIDATION_NONE);

    for (u32 fixture_index = 0;
        fixture_index < BUSTER_ARRAY_LENGTH(ir_fixture_tests);
        fixture_index += 1)
    {
        TemporalArena fixture_temporary = arena_begin_temporal(arguments->arena);
        IrFixtureTest fixture = ir_fixture_tests[fixture_index];
        String8 source = BYTE_SLICE_TO_STRING(
            8,
            file_read(arguments->arena, fixture.path, (FileReadOptions){0}));
        BUSTER_TEST(arguments, source.pointer != 0);
        TokenizerResult tokenizer = tokenize(arguments->arena, source.pointer, source.length);
        ParserResult parser = parser_parse(
            arguments->arena,
            expression_arena,
            source,
            tokenizer);
        BUSTER_TEST(arguments, tokenizer.error_count == 0);
        BUSTER_TEST(arguments, parser.diagnostic_count == 0);
        AnalysisSourceInput input = { .path = fixture.path, .parser = &parser };
        AnalysisResult analysis = analysis_index_module(
            arguments->arena,
            (AnalysisModuleId){ .value = 1000 + fixture_index },
            S8("ir-fixture"),
            &input,
            1);
        analysis_resolve_module_interfaces(arguments->arena, &analysis);
        IrModule module = ir_analyze_and_generate_module(arguments->arena, &analysis);
        BUSTER_TEST(arguments, module.function_count == parser.code_count);
        BUSTER_TEST(arguments, module.lowered_function_count + module.rejected_function_count <=
            module.function_count);
        IrValidationResult validation = ir_validate_module(&analysis, &module);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
        if (string_equal(fixture.path, S8("tests/basic_pointer.bbb")))
        {
            BUSTER_TEST(arguments, module.function_count == 1);
            AnalysisLocal* number = 0;
            AnalysisBody* pointer_body = analysis.module.bodies;
            for (u32 local_index = 0; local_index < pointer_body->local_count; local_index += 1)
            {
                if (string_equal(pointer_body->locals[local_index].name, S8("number")))
                {
                    number = pointer_body->locals + local_index;
                }
            }
            BUSTER_TEST(arguments, number != 0);
            if (number)
            {
                BUSTER_TEST(arguments, number->address_taken);
                BUSTER_TEST(arguments, number->requires_storage);
            }
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_LOCAL) >= 1);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_LOAD) >= 1);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_STORE) >= 1);
        }
        else if (string_equal(fixture.path, S8("tests/basic_for.bbb")))
        {
            BUSTER_TEST(arguments, module.function_count == 1);
            BUSTER_TEST(arguments, ir_test_parameter_count(module.functions) > 0);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_LOCAL) == 0);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_LOAD) == 0);
            BUSTER_TEST(arguments, ir_test_opcode_count(module.functions, IR_OPCODE_STORE) == 0);
        }
        for (u32 function_index = 0; function_index < module.function_count; function_index += 1)
        {
            IrFunction* function = module.functions + function_index;
            if (function->state == IR_FUNCTION_LOWERED)
            {
                BUSTER_TEST(arguments, function->block_count >= 2);
                BUSTER_TEST(arguments, function->instruction_count > 0);
                BUSTER_TEST(arguments, function->value_count > 0 ||
                    analysis_type_from_id(&analysis, function->type)->as.function.argument_count == 0);
            }
            else if (function->state == IR_FUNCTION_REJECTED)
            {
                BUSTER_TEST(arguments, ir_entity_has_diagnostic(&analysis, function->entity));
                BUSTER_TEST(arguments, function->instruction_count == 0);
            }
        }
        String8 printed = ir_print_module(arguments->arena, &analysis, &module);
        BUSTER_TEST(arguments, printed.length > 0);
        BUSTER_TEST(arguments, string_starts_with_sequence(printed, S8("module ir-fixture")));
        arena_set_position(fixture_temporary.arena, fixture_temporary.position);
    }
    BUSTER_CHECK(arena_destroy(expression_arena, 1));
    arena_set_position(temporary.arena, temporary.position);
    return result;
}
#endif
