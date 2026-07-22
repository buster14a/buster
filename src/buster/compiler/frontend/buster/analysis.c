#include <buster/compiler/frontend/buster/analysis.h>

#include <buster/file.h>
#include <buster/string.h>

BUSTER_GLOBAL_LOCAL s32 analysis_string_compare(String8 left, String8 right)
{
    u64 common_length = BUSTER_MIN(left.length, right.length);
    for (u64 index = 0; index < common_length; index += 1)
    {
        u8 left_byte = (u8)left.pointer[index];
        u8 right_byte = (u8)right.pointer[index];
        if (left_byte != right_byte)
        {
            return left_byte < right_byte ? -1 : 1;
        }
    }
    if (left.length == right.length)
    {
        return 0;
    }
    return left.length < right.length ? -1 : 1;
}

BUSTER_GLOBAL_LOCAL bool analysis_source_after(AnalysisSource left, AnalysisSource right)
{
    s32 path_order = analysis_string_compare(left.path, right.path);
    if (path_order != 0)
    {
        return path_order > 0;
    }
    return left.original_input_index > right.original_input_index;
}

BUSTER_GLOBAL_LOCAL bool analysis_entity_after(AnalysisEntity left, AnalysisEntity right)
{
    if (left.source.value != right.source.value)
    {
        return left.source.value > right.source.value;
    }
    if (left.range.offset != right.range.offset)
    {
        return left.range.offset > right.range.offset;
    }
    return left.kind > right.kind;
}

BUSTER_GLOBAL_LOCAL void analysis_diagnostic_append(AnalysisResult* result, AnalysisDiagnostic* diagnostic)
{
    if (result->last_diagnostic)
    {
        result->last_diagnostic->next = diagnostic;
    }
    else
    {
        result->first_diagnostic = diagnostic;
    }
    result->last_diagnostic = diagnostic;
    result->diagnostic_count += 1;
}

BUSTER_GLOBAL_LOCAL void analysis_duplicate_diagnostic_push(
    Arena* arena,
    AnalysisResult* result,
    AnalysisEntity* entity,
    AnalysisEntity* previous)
{
    AnalysisDiagnostic* diagnostic = arena_allocate(arena, AnalysisDiagnostic, 1);
    *diagnostic = (AnalysisDiagnostic){
        .message = S8("duplicate declaration in module"),
        .range = entity->range,
        .entity = entity->id,
        .previous_entity = previous->id,
        .source = entity->source,
        .kind = ANALYSIS_DIAGNOSTIC_DUPLICATE_DECLARATION,
        .subject = entity->name,
    };
    AnalysisDiagnosticNote* note = arena_allocate(arena, AnalysisDiagnosticNote, 1);
    *note = (AnalysisDiagnosticNote){
        .message = S8("previous declaration is here"),
        .range = previous->range,
        .entity = previous->id,
        .source = previous->source,
    };
    diagnostic->first_note = note;
    diagnostic->last_note = note;
    analysis_diagnostic_append(result, diagnostic);
}

BUSTER_GLOBAL_LOCAL void analysis_type_diagnostic_push(
    Arena* arena,
    AnalysisResult* result,
    AnalysisEntity* entity,
    ParserSourceRange range,
    AnalysisDiagnosticKind kind,
    String8 subject)
{
    String8 message = S8("unknown type");
    if (kind == ANALYSIS_DIAGNOSTIC_TYPE_ALIAS_CYCLE)
    {
        message = S8("type alias cycle");
    }
    AnalysisDiagnostic* diagnostic = arena_allocate(arena, AnalysisDiagnostic, 1);
    *diagnostic = (AnalysisDiagnostic){
        .message = message,
        .range = range,
        .entity = entity->id,
        .previous_entity = ANALYSIS_ENTITY_ID_INVALID,
        .source = entity->source,
        .kind = kind,
        .subject = subject,
    };
    analysis_diagnostic_append(result, diagnostic);
}

BUSTER_GLOBAL_LOCAL void analysis_entity_diagnostic_push(
    Arena* arena,
    AnalysisResult* result,
    AnalysisEntity* entity,
    ParserSourceRange range,
    AnalysisDiagnosticKind kind,
    String8 subject,
    String8 message)
{
    AnalysisDiagnostic* diagnostic = arena_allocate(arena, AnalysisDiagnostic, 1);
    *diagnostic = (AnalysisDiagnostic){
        .message = message,
        .range = range,
        .entity = entity->id,
        .previous_entity = ANALYSIS_ENTITY_ID_INVALID,
        .source = entity->source,
        .kind = kind,
        .subject = subject,
        .expected_type = ANALYSIS_TYPE_ID_INVALID,
        .actual_type = ANALYSIS_TYPE_ID_INVALID,
    };
    analysis_diagnostic_append(result, diagnostic);
}

AnalysisResult analysis_index_module(
    Arena* result_arena,
    AnalysisModuleId module_id,
    String8 module_name,
    AnalysisSourceInput* inputs,
    u32 input_count)
{
    AnalysisResult result = {0};
    result.module.id = module_id;
    result.module.name = string_duplicate_arena(result_arena, module_name, false);
    result.module.source_count = input_count;
    result.module.sources = arena_allocate(result_arena, AnalysisSource, input_count);

    u32 entity_count = 0;
    for (u32 input_index = 0; input_index < input_count; input_index += 1)
    {
        AnalysisSourceInput* input = inputs + input_index;
        result.module.sources[input_index] = (AnalysisSource){
            .path = string_duplicate_arena(result_arena, input->path, false),
            .parser = input->parser,
            .id = ANALYSIS_SOURCE_ID_INVALID,
            .original_input_index = input_index,
        };
        if (input->parser)
        {
            entity_count += input->parser->type_declaration_count;
            entity_count += input->parser->code_count;
        }
    }

    for (u32 index = 1; index < input_count; index += 1)
    {
        AnalysisSource value = result.module.sources[index];
        u32 insertion = index;
        while (insertion && analysis_source_after(result.module.sources[insertion - 1], value))
        {
            result.module.sources[insertion] = result.module.sources[insertion - 1];
            insertion -= 1;
        }
        result.module.sources[insertion] = value;
    }

    result.module.entity_count = entity_count;
    result.module.entities = arena_allocate(result_arena, AnalysisEntity, entity_count);
    result.module.semantics = arena_allocate(result_arena, AnalysisEntitySemantic, entity_count);
    result.module.bodies = arena_allocate(result_arena, AnalysisBody, entity_count);
    u32 entity_index = 0;
    for (u32 source_index = 0; source_index < input_count; source_index += 1)
    {
        AnalysisSource* source = result.module.sources + source_index;
        source->id = (AnalysisSourceId){ .value = source_index };
        if (!source->parser)
        {
            continue;
        }

        for (AstTypeDeclaration* type = source->parser->first_type_declaration; type; type = type->next)
        {
            result.module.entities[entity_index] = (AnalysisEntity){
                .name = type->name.text,
                .range = type->range,
                .source = source->id,
                .kind = ANALYSIS_ENTITY_TYPE,
                .name_space = ANALYSIS_NAMESPACE_TYPE,
                .ast.type_declaration = type,
            };
            entity_index += 1;
            result.module.type_count += 1;
        }
        for (AstCode* code = source->parser->first_code; code; code = code->next)
        {
            result.module.entities[entity_index] = (AnalysisEntity){
                .name = code->name,
                .range = code->range,
                .source = source->id,
                .kind = ANALYSIS_ENTITY_CODE,
                .name_space = ANALYSIS_NAMESPACE_VALUE,
                .ast.code = code,
            };
            entity_index += 1;
            result.module.code_count += 1;
        }
    }

    BUSTER_CHECK(entity_index == entity_count);
    for (u32 index = 1; index < entity_count; index += 1)
    {
        AnalysisEntity value = result.module.entities[index];
        u32 insertion = index;
        while (insertion && analysis_entity_after(result.module.entities[insertion - 1], value))
        {
            result.module.entities[insertion] = result.module.entities[insertion - 1];
            insertion -= 1;
        }
        result.module.entities[insertion] = value;
    }

    for (u32 index = 0; index < entity_count; index += 1)
    {
        AnalysisEntity* entity = result.module.entities + index;
        result.module.semantics[index] = (AnalysisEntitySemantic){
            .type = ANALYSIS_TYPE_ID_INVALID,
            .state = ANALYSIS_RESOLUTION_UNRESOLVED,
        };
        result.module.bodies[index] = (AnalysisBody){0};
        entity->id = (AnalysisEntityId){
            .module = module_id,
            .index = { .value = index },
        };
        for (u32 previous_index = 0; previous_index < index; previous_index += 1)
        {
            AnalysisEntity* previous = result.module.entities + previous_index;
            if (entity->name_space == previous->name_space && string_equal(entity->name, previous->name))
            {
                analysis_duplicate_diagnostic_push(result_arena, &result, entity, previous);
                break;
            }
        }
    }

    return result;
}

typedef struct AnalysisAstTypeLink AnalysisAstTypeLink;
struct AnalysisAstTypeLink
{
    AnalysisAstTypeLink* previous;
    AstType* type;
};

typedef struct AnalysisAstStatementLink AnalysisAstStatementLink;
struct AnalysisAstStatementLink
{
    AnalysisAstStatementLink* previous;
    AstStatement* statement;
};

BUSTER_GLOBAL_LOCAL void analysis_ast_statement_link_push(
    Arena* arena,
    AnalysisAstStatementLink** top,
    AstStatement* statement)
{
    for (; statement; statement = statement->next)
    {
        AnalysisAstStatementLink* link = arena_allocate(arena, AnalysisAstStatementLink, 1);
        *link = (AnalysisAstStatementLink){ .previous = *top, .statement = statement };
        *top = link;
    }
}

BUSTER_GLOBAL_LOCAL void analysis_ast_type_link_push(
    Arena* arena,
    AnalysisAstTypeLink** top,
    AstType* type)
{
    if (type)
    {
        AnalysisAstTypeLink* link = arena_allocate(arena, AnalysisAstTypeLink, 1);
        *link = (AnalysisAstTypeLink){ .previous = *top, .type = type };
        *top = link;
    }
}

BUSTER_GLOBAL_LOCAL u32 analysis_ast_type_count(Arena* scratch_arena, AnalysisResult* result)
{
    TemporalArena temporary = arena_begin_temporal(scratch_arena);
    AnalysisAstTypeLink* top = 0;
    AnalysisAstStatementLink* statement_top = 0;
    for (u32 index = 0; index < result->module.entity_count; index += 1)
    {
        AnalysisEntity* entity = result->module.entities + index;
        if (entity->kind == ANALYSIS_ENTITY_CODE)
        {
            analysis_ast_type_link_push(scratch_arena, &top, entity->ast.code->type);
            analysis_ast_statement_link_push(
                scratch_arena,
                &statement_top,
                entity->ast.code->body.first_statement);
            continue;
        }

        AstTypeDeclaration* declaration = entity->ast.type_declaration;
        if (declaration->kind == AST_TYPE_DECLARATION_ALIAS)
        {
            analysis_ast_type_link_push(scratch_arena, &top, declaration->alias_type);
        }
        else
        {
            for (AstTypeField* field = declaration->first_field; field; field = field->next)
            {
                analysis_ast_type_link_push(scratch_arena, &top, field->type);
            }
        }
    }

    while (statement_top)
    {
        AstStatement* statement = statement_top->statement;
        statement_top = statement_top->previous;
        switch (statement->id)
        {
            case AST_STATEMENT_DATA:
            {
                analysis_ast_type_link_push(scratch_arena, &top, statement->data_statement.type);
            } break;
            case AST_STATEMENT_IF:
            {
                analysis_ast_statement_link_push(
                    scratch_arena,
                    &statement_top,
                    statement->if_statement.then_block.first_statement);
                if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_BLOCK)
                {
                    analysis_ast_statement_link_push(
                        scratch_arena,
                        &statement_top,
                        statement->if_statement.else_block.first_statement);
                }
                else if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_IF)
                {
                    analysis_ast_statement_link_push(
                        scratch_arena,
                        &statement_top,
                        statement->if_statement.else_if);
                }
            } break;
            case AST_STATEMENT_SWITCH:
            {
                for (AstSwitchCase* switch_case = statement->switch_statement.first_case;
                    switch_case;
                    switch_case = switch_case->next)
                {
                    analysis_ast_statement_link_push(
                        scratch_arena,
                        &statement_top,
                        switch_case->body.first_statement);
                }
            } break;
            case AST_STATEMENT_FOR:
            {
                analysis_ast_type_link_push(scratch_arena, &top, statement->for_statement.type);
                analysis_ast_statement_link_push(
                    scratch_arena,
                    &statement_top,
                    statement->for_statement.body.first_statement);
            } break;
            case AST_STATEMENT_LOOP:
            {
                analysis_ast_statement_link_push(
                    scratch_arena,
                    &statement_top,
                    statement->loop_statement.body.first_statement);
            } break;
            case AST_STATEMENT_RETURN:
            case AST_STATEMENT_EXPRESSION:
            case AST_STATEMENT_ASSIGNMENT:
            case AST_STATEMENT_BREAK:
            case AST_STATEMENT_CONTINUE:
            case AST_STATEMENT_COUNT: break;
        }
    }

    u32 count = 0;
    while (top)
    {
        AstType* type = top->type;
        top = top->previous;
        count += 1;
        switch (type->id)
        {
            case AST_TYPE_NAMED: break;
            case AST_TYPE_POINTER:
            case AST_TYPE_SLICE:
            case AST_TYPE_INFERRED_ARRAY:
            {
                analysis_ast_type_link_push(scratch_arena, &top, type->element_type);
            } break;
            case AST_TYPE_ARRAY:
            {
                analysis_ast_type_link_push(scratch_arena, &top, type->array.element_type);
            } break;
            case AST_TYPE_FUNCTION:
            {
                analysis_ast_type_link_push(scratch_arena, &top, type->function.return_type);
                for (AstTypeArgument* argument = type->function.first_argument; argument; argument = argument->next)
                {
                    analysis_ast_type_link_push(scratch_arena, &top, argument->type);
                }
            } break;
            case AST_TYPE_COUNT: break;
        }
    }
    arena_set_position(temporary.arena, temporary.position);
    return count;
}

BUSTER_GLOBAL_LOCAL void analysis_body_capacity_measure(
    Arena* scratch_arena,
    AstCode* code,
    u32* local_count,
    u32* expression_node_count)
{
    AnalysisAstStatementLink* top = 0;
    analysis_ast_statement_link_push(scratch_arena, &top, code->body.first_statement);
    for (AstTypeArgument* argument = code->type->function.first_argument;
        argument;
        argument = argument->next)
    {
        *local_count += 1;
    }
    while (top)
    {
        AstStatement* statement = top->statement;
        top = top->previous;
        switch (statement->id)
        {
            case AST_STATEMENT_RETURN:
            {
                *expression_node_count += statement->return_statement.expression.count;
            } break;
            case AST_STATEMENT_DATA:
            {
                *local_count += 1;
                *expression_node_count += statement->data_statement.initializer.count;
            } break;
            case AST_STATEMENT_EXPRESSION:
            {
                *expression_node_count += statement->expression_statement.expression.count;
            } break;
            case AST_STATEMENT_ASSIGNMENT:
            {
                *expression_node_count += statement->assignment_statement.target.count;
                *expression_node_count += statement->assignment_statement.value.count;
            } break;
            case AST_STATEMENT_IF:
            {
                *expression_node_count += statement->if_statement.condition.count;
                analysis_ast_statement_link_push(
                    scratch_arena,
                    &top,
                    statement->if_statement.then_block.first_statement);
                if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_BLOCK)
                {
                    analysis_ast_statement_link_push(
                        scratch_arena,
                        &top,
                        statement->if_statement.else_block.first_statement);
                }
                else if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_IF)
                {
                    analysis_ast_statement_link_push(scratch_arena, &top, statement->if_statement.else_if);
                }
            } break;
            case AST_STATEMENT_SWITCH:
            {
                *expression_node_count += statement->switch_statement.expression.count;
                for (AstSwitchCase* switch_case = statement->switch_statement.first_case;
                    switch_case;
                    switch_case = switch_case->next)
                {
                    if (!switch_case->is_else)
                    {
                        *expression_node_count += switch_case->expression.count;
                    }
                    analysis_ast_statement_link_push(
                        scratch_arena,
                        &top,
                        switch_case->body.first_statement);
                }
            } break;
            case AST_STATEMENT_FOR:
            {
                *local_count += 1;
                *expression_node_count += statement->for_statement.iterable.count;
                analysis_ast_statement_link_push(
                    scratch_arena,
                    &top,
                    statement->for_statement.body.first_statement);
            } break;
            case AST_STATEMENT_LOOP:
            {
                if (statement->loop_statement.has_condition)
                {
                    *expression_node_count += statement->loop_statement.condition.count;
                }
                analysis_ast_statement_link_push(
                    scratch_arena,
                    &top,
                    statement->loop_statement.body.first_statement);
            } break;
            case AST_STATEMENT_BREAK:
            case AST_STATEMENT_CONTINUE:
            case AST_STATEMENT_COUNT: break;
        }
    }
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_type_add(
    AnalysisTypeTable* table,
    AnalysisType type)
{
    BUSTER_CHECK(table->count < table->capacity);
    type.id = (AnalysisTypeId){ .value = table->count };
    table->types[table->count] = type;
    table->count += 1;
    return type.id;
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_builtin_add(
    AnalysisTypeTable* table,
    String8 name,
    AnalysisTypeKind kind,
    u32 bit_width,
    bool is_signed)
{
    AnalysisType type = { .name = name, .kind = kind };
    if (kind == ANALYSIS_TYPE_INTEGER)
    {
        type.as.integer.bit_width = bit_width;
        type.as.integer.is_signed = is_signed;
    }
    else if (kind == ANALYSIS_TYPE_FLOAT)
    {
        type.as.float_bit_width = bit_width;
    }
    return analysis_type_add(table, type);
}

BUSTER_GLOBAL_LOCAL void analysis_builtin_types_initialize(AnalysisTypeTable* table)
{
    table->builtin.poison = analysis_builtin_add(table, S8(""), ANALYSIS_TYPE_POISON, 0, false);
    table->builtin.void_type = analysis_builtin_add(table, S8("void"), ANALYSIS_TYPE_VOID, 0, false);
    table->builtin.bool_type = analysis_builtin_add(table, S8("bool"), ANALYSIS_TYPE_BOOL, 0, false);
    table->builtin.u8_type = analysis_builtin_add(table, S8("u8"), ANALYSIS_TYPE_INTEGER, 8, false);
    table->builtin.u16_type = analysis_builtin_add(table, S8("u16"), ANALYSIS_TYPE_INTEGER, 16, false);
    table->builtin.u32_type = analysis_builtin_add(table, S8("u32"), ANALYSIS_TYPE_INTEGER, 32, false);
    table->builtin.u64_type = analysis_builtin_add(table, S8("u64"), ANALYSIS_TYPE_INTEGER, 64, false);
    table->builtin.s8_type = analysis_builtin_add(table, S8("s8"), ANALYSIS_TYPE_INTEGER, 8, true);
    table->builtin.s16_type = analysis_builtin_add(table, S8("s16"), ANALYSIS_TYPE_INTEGER, 16, true);
    table->builtin.s32_type = analysis_builtin_add(table, S8("s32"), ANALYSIS_TYPE_INTEGER, 32, true);
    table->builtin.s64_type = analysis_builtin_add(table, S8("s64"), ANALYSIS_TYPE_INTEGER, 64, true);
    table->builtin.f32_type = analysis_builtin_add(table, S8("f32"), ANALYSIS_TYPE_FLOAT, 32, true);
    table->builtin.f64_type = analysis_builtin_add(table, S8("f64"), ANALYSIS_TYPE_FLOAT, 64, true);
}

BUSTER_GLOBAL_LOCAL bool analysis_type_id_equal(AnalysisTypeId left, AnalysisTypeId right)
{
    return left.value == right.value;
}

AnalysisType* analysis_type_from_id(AnalysisResult* result, AnalysisTypeId id)
{
    BUSTER_CHECK(id.value < result->types.count);
    return result->types.types + id.value;
}

BUSTER_GLOBAL_LOCAL bool analysis_type_matches(AnalysisType* existing, AnalysisType* candidate)
{
    if (existing->kind != candidate->kind)
    {
        return false;
    }
    switch (candidate->kind)
    {
        case ANALYSIS_TYPE_POINTER:
        case ANALYSIS_TYPE_SLICE:
        case ANALYSIS_TYPE_INFERRED_ARRAY:
        case ANALYSIS_TYPE_RANGE:
        {
            return analysis_type_id_equal(existing->as.element_type, candidate->as.element_type);
        }
        case ANALYSIS_TYPE_ARRAY:
        {
            return
                analysis_type_id_equal(existing->as.array.element_type, candidate->as.array.element_type) &&
                existing->as.array.count == candidate->as.array.count;
        }
        case ANALYSIS_TYPE_FUNCTION:
        {
            if (
                existing->as.function.argument_count != candidate->as.function.argument_count ||
                existing->as.function.calling_convention != candidate->as.function.calling_convention ||
                !analysis_type_id_equal(existing->as.function.return_type, candidate->as.function.return_type))
            {
                return false;
            }
            for (u32 index = 0; index < candidate->as.function.argument_count; index += 1)
            {
                if (!analysis_type_id_equal(
                    existing->as.function.argument_types[index],
                    candidate->as.function.argument_types[index]))
                {
                    return false;
                }
            }
            return true;
        }
        case ANALYSIS_TYPE_POISON:
        case ANALYSIS_TYPE_VOID:
        case ANALYSIS_TYPE_BOOL:
        case ANALYSIS_TYPE_INTEGER:
        case ANALYSIS_TYPE_FLOAT:
        case ANALYSIS_TYPE_STRUCT:
        case ANALYSIS_TYPE_UNION:
        case ANALYSIS_TYPE_ENUM:
        case ANALYSIS_TYPE_COUNT: break;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_type_intern(
    Arena* result_arena,
    AnalysisTypeTable* table,
    AnalysisType candidate)
{
    for (u32 index = 0; index < table->count; index += 1)
    {
        if (analysis_type_matches(table->types + index, &candidate))
        {
            return (AnalysisTypeId){ .value = index };
        }
    }
    if (candidate.kind == ANALYSIS_TYPE_FUNCTION && candidate.as.function.argument_count)
    {
        u32 count = candidate.as.function.argument_count;
        AnalysisTypeId* argument_types = arena_allocate(result_arena, AnalysisTypeId, count);
        for (u32 index = 0; index < count; index += 1)
        {
            argument_types[index] = candidate.as.function.argument_types[index];
        }
        candidate.as.function.argument_types = argument_types;
    }
    return analysis_type_add(table, candidate);
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_builtin_type_find(AnalysisTypeTable* table, String8 name)
{
    for (u32 index = 0; index < table->count; index += 1)
    {
        AnalysisType* type = table->types + index;
        if (type->name.length && string_equal(type->name, name))
        {
            return type->id;
        }
    }
    return ANALYSIS_TYPE_ID_INVALID;
}

BUSTER_GLOBAL_LOCAL AnalysisEntity* analysis_named_type_find(AnalysisResult* result, String8 name)
{
    for (u32 index = 0; index < result->module.entity_count; index += 1)
    {
        AnalysisEntity* entity = result->module.entities + index;
        if (entity->name_space == ANALYSIS_NAMESPACE_TYPE && string_equal(entity->name, name))
        {
            return entity;
        }
    }
    return 0;
}

typedef enum AnalysisTypeTaskKind
{
    ANALYSIS_TYPE_TASK_AST,
    ANALYSIS_TYPE_TASK_ENTITY,
    ANALYSIS_TYPE_TASK_FINISH_ELEMENT,
    ANALYSIS_TYPE_TASK_FINISH_ARRAY,
    ANALYSIS_TYPE_TASK_FINISH_FUNCTION,
    ANALYSIS_TYPE_TASK_FINISH_ENTITY,
    ANALYSIS_TYPE_TASK_FINISH_FIELDS,
} AnalysisTypeTaskKind;

typedef struct AnalysisTypeTask AnalysisTypeTask;
struct AnalysisTypeTask
{
    AnalysisTypeTask* previous;
    AnalysisEntity* owner;
    AnalysisTypeId* destination;
    ParserSourceRange use_range;
    AnalysisTypeTaskKind kind;
    union
    {
        AstType* ast;
        AnalysisEntity* entity;
        struct
        {
            AnalysisTypeKind kind;
            AnalysisTypeId element;
        } element;
        struct
        {
            AnalysisTypeId element;
            u64 count;
        } array;
        struct
        {
            AnalysisTypeId* arguments;
            AnalysisTypeId return_type;
            AstCallingConvention calling_convention;
            u32 argument_count;
        } function;
        struct
        {
            AnalysisEntity* entity;
            AnalysisTypeId resolved_type;
        } finish_entity;
    } as;
};

typedef struct AnalysisResolutionContext AnalysisResolutionContext;
struct AnalysisResolutionContext
{
    Arena* result_arena;
    Arena* scratch_arena;
    AnalysisResult* result;
    AnalysisTypeTask* top;
};

BUSTER_GLOBAL_LOCAL AnalysisTypeTask* analysis_type_task_push(
    AnalysisResolutionContext* context,
    AnalysisTypeTask task)
{
    AnalysisTypeTask* result = arena_allocate(context->scratch_arena, AnalysisTypeTask, 1);
    task.previous = context->top;
    *result = task;
    context->top = result;
    return result;
}

BUSTER_GLOBAL_LOCAL void analysis_type_task_ast_push(
    AnalysisResolutionContext* context,
    AnalysisEntity* owner,
    AstType* ast,
    AnalysisTypeId* destination)
{
    analysis_type_task_push(context, (AnalysisTypeTask){
        .owner = owner,
        .destination = destination,
        .use_range = ast ? ast->range : owner->range,
        .kind = ANALYSIS_TYPE_TASK_AST,
        .as.ast = ast,
    });
}

BUSTER_GLOBAL_LOCAL void analysis_type_task_entity_push(
    AnalysisResolutionContext* context,
    AnalysisEntity* owner,
    AnalysisEntity* entity,
    ParserSourceRange use_range,
    AnalysisTypeId* destination)
{
    analysis_type_task_push(context, (AnalysisTypeTask){
        .owner = owner,
        .destination = destination,
        .use_range = use_range,
        .kind = ANALYSIS_TYPE_TASK_ENTITY,
        .as.entity = entity,
    });
}

BUSTER_GLOBAL_LOCAL bool analysis_type_is_poison(AnalysisResolutionContext* context, AnalysisTypeId id)
{
    return analysis_type_id_equal(id, context->result->types.builtin.poison);
}

BUSTER_GLOBAL_LOCAL void analysis_resolve_ast_task(
    AnalysisResolutionContext* context,
    AnalysisTypeTask* task)
{
    AnalysisTypeId poison = context->result->types.builtin.poison;
    AstType* ast = task->as.ast;
    if (!ast)
    {
        *task->destination = poison;
        return;
    }

    switch (ast->id)
    {
        case AST_TYPE_NAMED:
        {
            AnalysisTypeId builtin = analysis_builtin_type_find(&context->result->types, ast->name);
            if (builtin.value != ANALYSIS_ID_UNDERLYING_INVALID)
            {
                *task->destination = builtin;
                return;
            }

            AnalysisEntity* entity = analysis_named_type_find(context->result, ast->name);
            if (!entity)
            {
                analysis_type_diagnostic_push(
                    context->result_arena,
                    context->result,
                    task->owner,
                    ast->range,
                    ANALYSIS_DIAGNOSTIC_UNKNOWN_TYPE,
                    ast->name);
                *task->destination = poison;
                return;
            }

            AstTypeDeclaration* declaration = entity->ast.type_declaration;
            AnalysisEntitySemantic* semantic =
                context->result->module.semantics + entity->id.index.value;
            if (declaration->kind != AST_TYPE_DECLARATION_ALIAS)
            {
                *task->destination = semantic->type;
                return;
            }
            analysis_type_task_entity_push(context, task->owner, entity, ast->range, task->destination);
        } break;
        case AST_TYPE_POINTER:
        case AST_TYPE_SLICE:
        case AST_TYPE_INFERRED_ARRAY:
        {
            AnalysisTypeKind kind = ANALYSIS_TYPE_POINTER;
            if (ast->id == AST_TYPE_SLICE)
            {
                kind = ANALYSIS_TYPE_SLICE;
            }
            else if (ast->id == AST_TYPE_INFERRED_ARRAY)
            {
                kind = ANALYSIS_TYPE_INFERRED_ARRAY;
            }
            AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
                .owner = task->owner,
                .destination = task->destination,
                .use_range = ast->range,
                .kind = ANALYSIS_TYPE_TASK_FINISH_ELEMENT,
                .as.element = { .kind = kind, .element = poison },
            });
            analysis_type_task_ast_push(context, task->owner, ast->element_type, &finish->as.element.element);
        } break;
        case AST_TYPE_ARRAY:
        {
            AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
                .owner = task->owner,
                .destination = task->destination,
                .use_range = ast->range,
                .kind = ANALYSIS_TYPE_TASK_FINISH_ARRAY,
                .as.array = { .element = poison, .count = ast->array.count.value },
            });
            analysis_type_task_ast_push(context, task->owner, ast->array.element_type, &finish->as.array.element);
        } break;
        case AST_TYPE_FUNCTION:
        {
            u32 argument_count = 0;
            for (AstTypeArgument* argument = ast->function.first_argument; argument; argument = argument->next)
            {
                argument_count += 1;
            }
            AnalysisTypeId* arguments = arena_allocate(context->scratch_arena, AnalysisTypeId, argument_count);
            for (u32 index = 0; index < argument_count; index += 1)
            {
                arguments[index] = poison;
            }
            AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
                .owner = task->owner,
                .destination = task->destination,
                .use_range = ast->range,
                .kind = ANALYSIS_TYPE_TASK_FINISH_FUNCTION,
                .as.function = {
                    .arguments = arguments,
                    .return_type = poison,
                    .calling_convention = ast->function.calling_convention,
                    .argument_count = argument_count,
                },
            });
            analysis_type_task_ast_push(
                context,
                task->owner,
                ast->function.return_type,
                &finish->as.function.return_type);
            u32 argument_index = 0;
            for (AstTypeArgument* argument = ast->function.first_argument; argument; argument = argument->next)
            {
                analysis_type_task_ast_push(
                    context,
                    task->owner,
                    argument->type,
                    arguments + argument_index);
                argument_index += 1;
            }
        } break;
        case AST_TYPE_COUNT:
        {
            *task->destination = poison;
        } break;
    }
}

BUSTER_GLOBAL_LOCAL void analysis_resolve_entity_task(
    AnalysisResolutionContext* context,
    AnalysisTypeTask* task)
{
    AnalysisEntity* entity = task->as.entity;
    AnalysisEntitySemantic* semantic = context->result->module.semantics + entity->id.index.value;
    AnalysisTypeId poison = context->result->types.builtin.poison;
    if (semantic->state == ANALYSIS_RESOLUTION_RESOLVED)
    {
        if (task->destination)
        {
            *task->destination = semantic->type;
        }
        return;
    }
    if (semantic->state == ANALYSIS_RESOLUTION_ERROR)
    {
        if (task->destination)
        {
            *task->destination = poison;
        }
        return;
    }
    if (semantic->state == ANALYSIS_RESOLUTION_RESOLVING)
    {
        analysis_type_diagnostic_push(
            context->result_arena,
            context->result,
            task->owner,
            task->use_range,
            ANALYSIS_DIAGNOSTIC_TYPE_ALIAS_CYCLE,
            entity->name);
        semantic->state = ANALYSIS_RESOLUTION_ERROR;
        if (task->destination)
        {
            *task->destination = poison;
        }
        return;
    }

    semantic->state = ANALYSIS_RESOLUTION_RESOLVING;
    if (entity->kind == ANALYSIS_ENTITY_CODE)
    {
        AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
            .owner = entity,
            .destination = task->destination,
            .use_range = entity->range,
            .kind = ANALYSIS_TYPE_TASK_FINISH_ENTITY,
            .as.finish_entity = { .entity = entity, .resolved_type = poison },
        });
        analysis_type_task_ast_push(
            context,
            entity,
            entity->ast.code->type,
            &finish->as.finish_entity.resolved_type);
        return;
    }

    AstTypeDeclaration* declaration = entity->ast.type_declaration;
    if (declaration->kind == AST_TYPE_DECLARATION_ALIAS)
    {
        AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
            .owner = entity,
            .destination = task->destination,
            .use_range = entity->range,
            .kind = ANALYSIS_TYPE_TASK_FINISH_ENTITY,
            .as.finish_entity = { .entity = entity, .resolved_type = poison },
        });
        analysis_type_task_ast_push(
            context,
            entity,
            declaration->alias_type,
            &finish->as.finish_entity.resolved_type);
        return;
    }

    if (declaration->kind == AST_TYPE_DECLARATION_ENUM)
    {
        semantic->state = ANALYSIS_RESOLUTION_RESOLVED;
        if (task->destination)
        {
            *task->destination = semantic->type;
        }
        return;
    }

    semantic->field_count = declaration->field_count;
    semantic->fields = arena_allocate(context->result_arena, AnalysisField, semantic->field_count);
    AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
        .owner = entity,
        .destination = task->destination,
        .use_range = entity->range,
        .kind = ANALYSIS_TYPE_TASK_FINISH_FIELDS,
        .as.entity = entity,
    });
    BUSTER_UNUSED(finish);
    u32 field_index = 0;
    for (AstTypeField* field = declaration->first_field; field; field = field->next)
    {
        BUSTER_CHECK(field_index < semantic->field_count);
        semantic->fields[field_index] = (AnalysisField){
            .name = field->name.text,
            .range = field->range,
            .type = poison,
        };
        analysis_type_task_ast_push(
            context,
            entity,
            field->type,
            &semantic->fields[field_index].type);
        field_index += 1;
    }
    BUSTER_CHECK(field_index == semantic->field_count);
}

BUSTER_GLOBAL_LOCAL void analysis_finish_type_task(
    AnalysisResolutionContext* context,
    AnalysisTypeTask* task)
{
    AnalysisTypeId poison = context->result->types.builtin.poison;
    switch (task->kind)
    {
        case ANALYSIS_TYPE_TASK_FINISH_ELEMENT:
        {
            if (analysis_type_is_poison(context, task->as.element.element))
            {
                *task->destination = poison;
                return;
            }
            *task->destination = analysis_type_intern(
                context->result_arena,
                &context->result->types,
                (AnalysisType){
                    .kind = task->as.element.kind,
                    .as.element_type = task->as.element.element,
                });
        } break;
        case ANALYSIS_TYPE_TASK_FINISH_ARRAY:
        {
            if (analysis_type_is_poison(context, task->as.array.element))
            {
                *task->destination = poison;
                return;
            }
            *task->destination = analysis_type_intern(
                context->result_arena,
                &context->result->types,
                (AnalysisType){
                    .kind = ANALYSIS_TYPE_ARRAY,
                    .as.array = {
                        .element_type = task->as.array.element,
                        .count = task->as.array.count,
                    },
                });
        } break;
        case ANALYSIS_TYPE_TASK_FINISH_FUNCTION:
        {
            bool poisoned = analysis_type_is_poison(context, task->as.function.return_type);
            for (u32 index = 0; index < task->as.function.argument_count; index += 1)
            {
                poisoned = poisoned || analysis_type_is_poison(context, task->as.function.arguments[index]);
            }
            if (poisoned)
            {
                *task->destination = poison;
                return;
            }
            *task->destination = analysis_type_intern(
                context->result_arena,
                &context->result->types,
                (AnalysisType){
                    .kind = ANALYSIS_TYPE_FUNCTION,
                    .as.function = {
                        .argument_types = task->as.function.arguments,
                        .return_type = task->as.function.return_type,
                        .calling_convention = task->as.function.calling_convention,
                        .argument_count = task->as.function.argument_count,
                    },
                });
        } break;
        case ANALYSIS_TYPE_TASK_FINISH_ENTITY:
        {
            AnalysisEntity* entity = task->as.finish_entity.entity;
            AnalysisEntitySemantic* semantic =
                context->result->module.semantics + entity->id.index.value;
            semantic->type = task->as.finish_entity.resolved_type;
            semantic->state = analysis_type_is_poison(context, semantic->type) ?
                ANALYSIS_RESOLUTION_ERROR : ANALYSIS_RESOLUTION_RESOLVED;
            if (task->destination)
            {
                *task->destination = semantic->type;
            }
        } break;
        case ANALYSIS_TYPE_TASK_FINISH_FIELDS:
        {
            AnalysisEntity* entity = task->as.entity;
            AnalysisEntitySemantic* semantic =
                context->result->module.semantics + entity->id.index.value;
            bool poisoned = false;
            for (u32 index = 0; index < semantic->field_count; index += 1)
            {
                poisoned = poisoned || analysis_type_is_poison(context, semantic->fields[index].type);
            }
            semantic->state = poisoned ? ANALYSIS_RESOLUTION_ERROR : ANALYSIS_RESOLUTION_RESOLVED;
            if (task->destination)
            {
                *task->destination = semantic->type;
            }
        } break;
        case ANALYSIS_TYPE_TASK_AST:
        case ANALYSIS_TYPE_TASK_ENTITY: break;
    }
}

BUSTER_GLOBAL_LOCAL void analysis_resolution_run(AnalysisResolutionContext* context)
{
    while (context->top)
    {
        AnalysisTypeTask* task = context->top;
        context->top = task->previous;
        if (task->kind == ANALYSIS_TYPE_TASK_AST)
        {
            analysis_resolve_ast_task(context, task);
        }
        else if (task->kind == ANALYSIS_TYPE_TASK_ENTITY)
        {
            analysis_resolve_entity_task(context, task);
        }
        else
        {
            analysis_finish_type_task(context, task);
        }
    }
}

BUSTER_GLOBAL_LOCAL u32 analysis_node_arity(AstNode* node);
BUSTER_GLOBAL_LOCAL AnalysisConstant analysis_constant_integer_binary(
    AstNodeId operation,
    AnalysisConstant left,
    AnalysisConstant right);

BUSTER_GLOBAL_LOCAL bool analysis_enum_constant_evaluate(
    Arena* scratch_arena,
    AstExpression expression,
    u64* value)
{
    AnalysisConstant* stack = arena_allocate(scratch_arena, AnalysisConstant, expression.count);
    u32 count = 0;
    for (u32 index = 0; index < expression.count; index += 1)
    {
        stack[index] = (AnalysisConstant){0};
    }
    for (u32 index = 0; index < expression.count; index += 1)
    {
        AstNode* node = expression.nodes + index;
        if (node->id == AST_NODE_CONSTANT_INTEGER)
        {
            stack[count] = (AnalysisConstant){
                .integer = node->integer.value,
                .kind = ANALYSIS_CONSTANT_INTEGER,
            };
            count += 1;
        }
        else if ((node->id == AST_NODE_UNARY_MINUS || node->id == AST_NODE_UNARY_PLUS) && count)
        {
            if (node->id == AST_NODE_UNARY_MINUS)
            {
                stack[count - 1].is_negative = !stack[count - 1].is_negative;
            }
        }
        else if (analysis_node_arity(node) == 2 && count >= 2)
        {
            AnalysisConstant right = stack[count - 1];
            AnalysisConstant left = stack[count - 2];
            count -= 2;
            AnalysisConstant folded = analysis_constant_integer_binary(node->id, left, right);
            if (folded.kind != ANALYSIS_CONSTANT_INTEGER)
            {
                return false;
            }
            stack[count] = folded;
            count += 1;
        }
        else
        {
            return false;
        }
    }
    if (count != 1 || stack[0].kind != ANALYSIS_CONSTANT_INTEGER || stack[0].is_negative)
    {
        return false;
    }
    *value = stack[0].integer;
    return true;
}

BUSTER_GLOBAL_LOCAL void analysis_validate_module_declarations(
    Arena* result_arena,
    Arena* scratch_arena,
    AnalysisResult* result)
{
    for (u32 entity_index = 0; entity_index < result->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = result->module.entities + entity_index;
        if (entity->kind == ANALYSIS_ENTITY_CODE)
        {
            AstType* function = entity->ast.code->type;
            for (AstTypeArgument* argument = function->function.first_argument;
                argument;
                argument = argument->next)
            {
                for (AstTypeArgument* previous = function->function.first_argument;
                    previous && previous != argument;
                    previous = previous->next)
                {
                    if (string_equal(previous->name, argument->name))
                    {
                        analysis_entity_diagnostic_push(
                            result_arena,
                            result,
                            entity,
                            argument->range,
                            ANALYSIS_DIAGNOSTIC_DUPLICATE_LOCAL,
                            argument->name,
                            S8("duplicate function argument"));
                        break;
                    }
                }
            }
            continue;
        }
        if (entity->kind != ANALYSIS_ENTITY_TYPE)
        {
            continue;
        }
        AstTypeDeclaration* declaration = entity->ast.type_declaration;
        AnalysisEntitySemantic* semantic = result->module.semantics + entity_index;
        if (declaration->kind == AST_TYPE_DECLARATION_STRUCT ||
            declaration->kind == AST_TYPE_DECLARATION_UNION)
        {
            u32 field_index = 0;
            for (AstTypeField* field = declaration->first_field; field; field = field->next)
            {
                u32 previous_index = 0;
                for (AstTypeField* previous = declaration->first_field;
                    previous && previous != field;
                    previous = previous->next, previous_index += 1)
                {
                    if (string_equal(previous->name.text, field->name.text))
                    {
                        analysis_entity_diagnostic_push(
                            result_arena,
                            result,
                            entity,
                            field->name.range,
                            ANALYSIS_DIAGNOSTIC_DUPLICATE_FIELD,
                            field->name.text,
                            S8("duplicate field declaration"));
                        break;
                    }
                }
                BUSTER_UNUSED(previous_index);
                field_index += 1;
            }
            BUSTER_CHECK(field_index == declaration->field_count);
        }
        else if (declaration->kind == AST_TYPE_DECLARATION_ENUM)
        {
            semantic->enum_member_count = declaration->enum_member_count;
            semantic->enum_members = arena_allocate(
                result_arena,
                AnalysisEnumMember,
                semantic->enum_member_count);
            u64 next_value = 0;
            u32 member_index = 0;
            for (AstEnumMember* member = declaration->first_enum_member;
                member;
                member = member->next, member_index += 1)
            {
                for (AstEnumMember* previous = declaration->first_enum_member;
                    previous && previous != member;
                    previous = previous->next)
                {
                    if (string_equal(previous->name.text, member->name.text))
                    {
                        analysis_entity_diagnostic_push(
                            result_arena,
                            result,
                            entity,
                            member->name.range,
                            ANALYSIS_DIAGNOSTIC_DUPLICATE_ENUM_MEMBER,
                            member->name.text,
                            S8("duplicate enum member"));
                        break;
                    }
                }
                u64 member_value = next_value;
                if (member->has_explicit_value &&
                    !analysis_enum_constant_evaluate(scratch_arena, member->value, &member_value))
                {
                    analysis_entity_diagnostic_push(
                        result_arena,
                        result,
                        entity,
                        member->range,
                        ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT,
                        member->name.text,
                        S8("enum value is not a constant non-negative integer"));
                }
                semantic->enum_members[member_index] = (AnalysisEnumMember){
                    .name = member->name.text,
                    .range = member->range,
                    .value = member_value,
                };
                next_value = member_value + 1;
            }
            BUSTER_CHECK(member_index == semantic->enum_member_count);
        }
    }
}

void analysis_resolve_module_interfaces(Arena* result_arena, AnalysisResult* result)
{
    BUSTER_CHECK(result->types.types == 0);
    Arena* conflicts[] = { result_arena };
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    u32 ast_type_count = analysis_ast_type_count(scratch.arena, result);
    u32 body_expression_node_count = 0;
    u32 ignored_local_count = 0;
    for (u32 index = 0; index < result->module.entity_count; index += 1)
    {
        AnalysisEntity* entity = result->module.entities + index;
        if (entity->kind == ANALYSIS_ENTITY_CODE && entity->ast.code->has_body)
        {
            analysis_body_capacity_measure(
                scratch.arena,
                entity->ast.code,
                &ignored_local_count,
                &body_expression_node_count);
        }
    }
    u32 builtin_count = 13;
    result->types.capacity =
        builtin_count + result->module.type_count + ast_type_count + body_expression_node_count;
    result->types.types = arena_allocate(result_arena, AnalysisType, result->types.capacity);
    analysis_builtin_types_initialize(&result->types);

    for (u32 index = 0; index < result->module.entity_count; index += 1)
    {
        AnalysisEntity* entity = result->module.entities + index;
        if (entity->kind != ANALYSIS_ENTITY_TYPE)
        {
            continue;
        }
        AstTypeDeclaration* declaration = entity->ast.type_declaration;
        if (declaration->kind == AST_TYPE_DECLARATION_ALIAS)
        {
            continue;
        }
        AnalysisTypeKind kind = ANALYSIS_TYPE_STRUCT;
        if (declaration->kind == AST_TYPE_DECLARATION_UNION)
        {
            kind = ANALYSIS_TYPE_UNION;
        }
        else if (declaration->kind == AST_TYPE_DECLARATION_ENUM)
        {
            kind = ANALYSIS_TYPE_ENUM;
        }
        AnalysisTypeId type = analysis_type_add(&result->types, (AnalysisType){
            .kind = kind,
            .as.declaration = entity->id,
        });
        result->module.semantics[index].type = type;
    }

    AnalysisResolutionContext context = {
        .result_arena = result_arena,
        .scratch_arena = scratch.arena,
        .result = result,
    };
    for (u32 index = result->module.entity_count; index > 0; index -= 1)
    {
        AnalysisEntity* entity = result->module.entities + index - 1;
        analysis_type_task_entity_push(&context, entity, entity, entity->range, 0);
    }
    analysis_resolution_run(&context);
    analysis_validate_module_declarations(result_arena, scratch.arena, result);
    scratch_end(scratch);
}

typedef struct AnalysisBinding AnalysisBinding;
struct AnalysisBinding
{
    AnalysisBinding* previous;
    String8 name;
    AnalysisLocalId local;
    u32 scope_depth;
};

typedef struct AnalysisBodyTask AnalysisBodyTask;
struct AnalysisBodyTask
{
    AnalysisBodyTask* previous;
    AstStatement* statement;
    AnalysisBinding* bindings;
    u32 scope_depth;
    u32 loop_depth;
};

typedef struct AnalysisBodyContext AnalysisBodyContext;
struct AnalysisBodyContext
{
    Arena* result_arena;
    Arena* scratch_arena;
    AnalysisResult* result;
    AnalysisEntity* owner;
    AnalysisBody* body;
    AnalysisResolutionContext resolution;
};

typedef struct AnalysisFlow AnalysisFlow;
struct AnalysisFlow
{
    bool can_fall_through;
    bool has_break;
};

typedef struct AnalysisFlowStatement AnalysisFlowStatement;
struct AnalysisFlowStatement
{
    AstStatement* statement;
    AnalysisFlow flow;
};

BUSTER_GLOBAL_LOCAL String8 analysis_diagnostic_message(AnalysisDiagnosticKind kind)
{
    switch (kind)
    {
        case ANALYSIS_DIAGNOSTIC_UNKNOWN_IDENTIFIER: return S8("unknown identifier");
        case ANALYSIS_DIAGNOSTIC_USE_BEFORE_INITIALIZATION:
            return S8("local is used before it is initialized");
        case ANALYSIS_DIAGNOSTIC_DUPLICATE_LOCAL: return S8("duplicate local declaration in scope");
        case ANALYSIS_DIAGNOSTIC_TYPE_MISMATCH: return S8("type mismatch");
        case ANALYSIS_DIAGNOSTIC_EXPECTED_PLACE: return S8("expression is not assignable");
        case ANALYSIS_DIAGNOSTIC_NOT_CALLABLE: return S8("expression is not callable");
        case ANALYSIS_DIAGNOSTIC_ARGUMENT_COUNT: return S8("incorrect argument count");
        case ANALYSIS_DIAGNOSTIC_UNKNOWN_MEMBER: return S8("unknown member");
        case ANALYSIS_DIAGNOSTIC_INVALID_OPERAND: return S8("invalid operand type");
        case ANALYSIS_DIAGNOSTIC_EXPECTED_CONTEXTUAL_TYPE: return S8("expression requires a contextual type");
        case ANALYSIS_DIAGNOSTIC_INVALID_CONTROL_FLOW: return S8("control-flow statement is outside a loop");
        case ANALYSIS_DIAGNOSTIC_DUPLICATE_FIELD: return S8("duplicate field declaration");
        case ANALYSIS_DIAGNOSTIC_DUPLICATE_ENUM_MEMBER: return S8("duplicate enum member");
        case ANALYSIS_DIAGNOSTIC_DUPLICATE_AGGREGATE_FIELD: return S8("duplicate aggregate literal field");
        case ANALYSIS_DIAGNOSTIC_MISSING_AGGREGATE_FIELD: return S8("missing aggregate literal field");
        case ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT: return S8("constant value is not representable by its type");
        case ANALYSIS_DIAGNOSTIC_MISSING_RETURN: return S8("not every control-flow path returns a value");
        case ANALYSIS_DIAGNOSTIC_UNREACHABLE_STATEMENT: return S8("unreachable statement");
        case ANALYSIS_DIAGNOSTIC_DUPLICATE_SWITCH_CASE: return S8("duplicate switch case");
        case ANALYSIS_DIAGNOSTIC_NONEXHAUSTIVE_SWITCH: return S8("switch does not cover every enum member");
        case ANALYSIS_DIAGNOSTIC_DUPLICATE_DECLARATION:
        case ANALYSIS_DIAGNOSTIC_UNKNOWN_TYPE:
        case ANALYSIS_DIAGNOSTIC_TYPE_ALIAS_CYCLE:
        case ANALYSIS_DIAGNOSTIC_COUNT: break;
    }
    return S8("semantic error");
}

BUSTER_GLOBAL_LOCAL void analysis_body_diagnostic_push(
    AnalysisBodyContext* context,
    ParserSourceRange range,
    AnalysisDiagnosticKind kind,
    String8 subject)
{
    AnalysisDiagnostic* diagnostic = arena_allocate(context->result_arena, AnalysisDiagnostic, 1);
    *diagnostic = (AnalysisDiagnostic){
        .message = analysis_diagnostic_message(kind),
        .range = range,
        .entity = context->owner->id,
        .previous_entity = ANALYSIS_ENTITY_ID_INVALID,
        .source = context->owner->source,
        .kind = kind,
        .subject = subject,
        .expected_type = ANALYSIS_TYPE_ID_INVALID,
        .actual_type = ANALYSIS_TYPE_ID_INVALID,
    };
    analysis_diagnostic_append(context->result, diagnostic);
}

BUSTER_GLOBAL_LOCAL void analysis_mismatch_diagnostic_push(
    AnalysisBodyContext* context,
    ParserSourceRange range,
    AnalysisTypeId expected,
    AnalysisTypeId actual)
{
    AnalysisDiagnostic* diagnostic = arena_allocate(context->result_arena, AnalysisDiagnostic, 1);
    *diagnostic = (AnalysisDiagnostic){
        .message = analysis_diagnostic_message(ANALYSIS_DIAGNOSTIC_TYPE_MISMATCH),
        .range = range,
        .entity = context->owner->id,
        .previous_entity = ANALYSIS_ENTITY_ID_INVALID,
        .source = context->owner->source,
        .kind = ANALYSIS_DIAGNOSTIC_TYPE_MISMATCH,
        .expected_type = expected,
        .actual_type = actual,
        .expected_type_name = analysis_type_from_id(context->result, expected)->name,
        .actual_type_name = analysis_type_from_id(context->result, actual)->name,
    };
    analysis_diagnostic_append(context->result, diagnostic);
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_body_type_resolve(
    AnalysisBodyContext* context,
    AstType* ast)
{
    AnalysisTypeId result = context->result->types.builtin.poison;
    analysis_type_task_ast_push(&context->resolution, context->owner, ast, &result);
    analysis_resolution_run(&context->resolution);
    return result;
}

BUSTER_GLOBAL_LOCAL AnalysisConversionKind analysis_explicit_conversion_kind(
    AnalysisResult* result,
    AnalysisTypeId source_id,
    AnalysisTypeId target_id)
{
    AnalysisType* source = analysis_type_from_id(result, source_id);
    AnalysisType* target = analysis_type_from_id(result, target_id);
    if (source->kind == ANALYSIS_TYPE_INTEGER && target->kind == ANALYSIS_TYPE_INTEGER)
    {
        if (source->as.integer.bit_width < target->as.integer.bit_width)
        {
            return ANALYSIS_CONVERSION_INTEGER_WIDEN;
        }
        if (source->as.integer.bit_width > target->as.integer.bit_width)
        {
            return ANALYSIS_CONVERSION_INTEGER_NARROW;
        }
        return ANALYSIS_CONVERSION_EXPLICIT;
    }
    if (source->kind == ANALYSIS_TYPE_FLOAT && target->kind == ANALYSIS_TYPE_FLOAT)
    {
        if (source->as.float_bit_width < target->as.float_bit_width)
        {
            return ANALYSIS_CONVERSION_FLOAT_WIDEN;
        }
        if (source->as.float_bit_width > target->as.float_bit_width)
        {
            return ANALYSIS_CONVERSION_FLOAT_NARROW;
        }
        return ANALYSIS_CONVERSION_EXPLICIT;
    }
    if (source->kind == ANALYSIS_TYPE_POINTER || target->kind == ANALYSIS_TYPE_POINTER)
    {
        return ANALYSIS_CONVERSION_POINTER;
    }
    return ANALYSIS_CONVERSION_EXPLICIT;
}

BUSTER_GLOBAL_LOCAL bool analysis_type_compatible(
    AnalysisResult* result,
    AnalysisTypeId expected,
    AnalysisTypeId actual)
{
    AnalysisTypeId poison = result->types.builtin.poison;
    if (analysis_type_id_equal(expected, poison) || analysis_type_id_equal(actual, poison))
    {
        return true;
    }
    if (analysis_type_id_equal(expected, actual))
    {
        return true;
    }
    AnalysisType* expected_type = analysis_type_from_id(result, expected);
    AnalysisType* actual_type = analysis_type_from_id(result, actual);
    bool expected_array =
        expected_type->kind == ANALYSIS_TYPE_ARRAY || expected_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY;
    bool actual_array =
        actual_type->kind == ANALYSIS_TYPE_ARRAY || actual_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY;
    if (expected_array && actual_array)
    {
        AnalysisTypeId expected_element = expected_type->kind == ANALYSIS_TYPE_ARRAY ?
            expected_type->as.array.element_type : expected_type->as.element_type;
        AnalysisTypeId actual_element = actual_type->kind == ANALYSIS_TYPE_ARRAY ?
            actual_type->as.array.element_type : actual_type->as.element_type;
        if (!analysis_type_id_equal(expected_element, actual_element))
        {
            return false;
        }
        if (expected_type->kind == ANALYSIS_TYPE_ARRAY && actual_type->kind == ANALYSIS_TYPE_ARRAY)
        {
            return expected_type->as.array.count == actual_type->as.array.count;
        }
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool analysis_type_is_numeric(AnalysisResult* result, AnalysisTypeId id)
{
    AnalysisTypeKind kind = analysis_type_from_id(result, id)->kind;
    return kind == ANALYSIS_TYPE_INTEGER || kind == ANALYSIS_TYPE_FLOAT;
}

BUSTER_GLOBAL_LOCAL bool analysis_type_is_integer(AnalysisResult* result, AnalysisTypeId id)
{
    return analysis_type_from_id(result, id)->kind == ANALYSIS_TYPE_INTEGER;
}

BUSTER_GLOBAL_LOCAL AnalysisEntity* analysis_value_entity_find(AnalysisResult* result, String8 name)
{
    for (u32 index = 0; index < result->module.entity_count; index += 1)
    {
        AnalysisEntity* entity = result->module.entities + index;
        if (entity->name_space == ANALYSIS_NAMESPACE_VALUE && string_equal(entity->name, name))
        {
            return entity;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL AnalysisBinding* analysis_binding_find(AnalysisBinding* binding, String8 name)
{
    for (; binding; binding = binding->previous)
    {
        if (string_equal(binding->name, name))
        {
            return binding;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void analysis_body_dependency_add(
    AnalysisBodyContext* context,
    AnalysisEntityId entity)
{
    for (u32 index = 0; index < context->body->dependency_count; index += 1)
    {
        AnalysisEntityId existing = context->body->dependencies[index];
        if (existing.module.value == entity.module.value && existing.index.value == entity.index.value)
        {
            return;
        }
    }
    BUSTER_CHECK(context->body->dependency_count < context->body->dependency_capacity);
    context->body->dependencies[context->body->dependency_count] = entity;
    context->body->dependency_count += 1;
}

BUSTER_GLOBAL_LOCAL AnalysisLocal* analysis_local_add(
    AnalysisBodyContext* context,
    AnalysisBinding** bindings,
    AstIdentifier identifier,
    AnalysisTypeId type,
    AnalysisLocalKind kind,
    u32 scope_depth)
{
    for (AnalysisBinding* binding = *bindings;
        binding && binding->scope_depth == scope_depth;
        binding = binding->previous)
    {
        if (string_equal(binding->name, identifier.text))
        {
            if (kind != ANALYSIS_LOCAL_ARGUMENT)
            {
                analysis_body_diagnostic_push(
                    context,
                    identifier.range,
                    ANALYSIS_DIAGNOSTIC_DUPLICATE_LOCAL,
                    identifier.text);
            }
            break;
        }
    }
    BUSTER_CHECK(context->body->local_count < context->body->local_capacity);
    AnalysisLocalId id = { .value = context->body->local_count };
    AnalysisLocal* local = context->body->locals + context->body->local_count;
    *local = (AnalysisLocal){
        .name = identifier.text,
        .range = identifier.range,
        .type = type,
        .id = id,
        .kind = kind,
        .scope_depth = scope_depth,
        .is_mutable = true,
        .is_initialized = true,
    };
    context->body->local_count += 1;
    AnalysisBinding* binding = arena_allocate(context->scratch_arena, AnalysisBinding, 1);
    *binding = (AnalysisBinding){
        .previous = *bindings,
        .name = identifier.text,
        .local = id,
        .scope_depth = scope_depth,
    };
    *bindings = binding;
    return local;
}

BUSTER_GLOBAL_LOCAL ParserSourceRange analysis_node_range(AnalysisEntity* owner, AstNode* node)
{
    switch (node->id)
    {
        case AST_NODE_IDENTIFIER: return node->identifier.range;
        case AST_NODE_ARRAY_LITERAL: return node->array_literal.range;
        case AST_NODE_ARRAY_INDEX: return node->array_index.range;
        case AST_NODE_ARRAY_SLICE: return node->array_slice.range;
        case AST_NODE_AGGREGATE_LITERAL: return node->aggregate_literal.range;
        case AST_NODE_MEMBER_ACCESS: return node->member_access.range;
        case AST_NODE_ENUM_LITERAL: return node->enum_literal.range;
        case AST_NODE_CALL: return node->call.range;
        case AST_NODE_INTRINSIC_CALL: return node->intrinsic_call.range;
        case AST_NODE_ADDRESS_OF:
        case AST_NODE_DEREFERENCE: return node->pointer_operator.range;
        case AST_NODE_CONSTANT_INTEGER:
        case AST_NODE_CONSTANT_FLOAT:
        case AST_NODE_CONSTANT_CHARACTER:
        case AST_NODE_CONSTANT_STRING:
        case AST_NODE_UNDEFINED:
        case AST_NODE_UNARY_MINUS:
        case AST_NODE_UNARY_PLUS:
        case AST_NODE_UNARY_LOGICAL_NOT:
        case AST_NODE_UNARY_BITWISE_NOT:
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
        case AST_NODE_COUNT: break;
    }
    return owner->range;
}

BUSTER_GLOBAL_LOCAL u32 analysis_node_arity(AstNode* node)
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

BUSTER_GLOBAL_LOCAL bool analysis_constant_integer_fits(
    AnalysisResult* result,
    AnalysisConstant constant,
    AnalysisTypeId type_id)
{
    AnalysisType* type = analysis_type_from_id(result, type_id);
    if (constant.kind != ANALYSIS_CONSTANT_INTEGER || type->kind != ANALYSIS_TYPE_INTEGER)
    {
        return true;
    }
    u32 width = type->as.integer.bit_width;
    if (!type->as.integer.is_signed)
    {
        if (constant.is_negative)
        {
            return false;
        }
        return width == 64 || constant.integer < ((u64)1 << width);
    }
    u64 limit = (u64)1 << (width - 1);
    return constant.is_negative ? constant.integer <= limit : constant.integer < limit;
}

BUSTER_GLOBAL_LOCAL bool analysis_constant_fits(
    AnalysisResult* result,
    AnalysisConstant constant,
    AnalysisTypeId type_id)
{
    if (!analysis_constant_integer_fits(result, constant, type_id))
    {
        return false;
    }
    AnalysisType* type = analysis_type_from_id(result, type_id);
    if (constant.kind == ANALYSIS_CONSTANT_FLOAT && type->kind == ANALYSIS_TYPE_FLOAT)
    {
        if (constant.floating != constant.floating)
        {
            return false;
        }
        f64 magnitude = constant.floating < 0.0 ? -constant.floating : constant.floating;
        return type->as.float_bit_width == 64 || magnitude <= 3.4028234663852886e38;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL AnalysisConstant analysis_constant_cast(
    AnalysisResult* result,
    AnalysisConstant constant,
    AnalysisTypeId target_id)
{
    AnalysisType* target = analysis_type_from_id(result, target_id);
    if (constant.kind == ANALYSIS_CONSTANT_INTEGER && target->kind == ANALYSIS_TYPE_FLOAT)
    {
        f64 value = (f64)constant.integer;
        return (AnalysisConstant){
            .floating = constant.is_negative ? -value : value,
            .kind = ANALYSIS_CONSTANT_FLOAT,
        };
    }
    if (constant.kind == ANALYSIS_CONSTANT_FLOAT && target->kind == ANALYSIS_TYPE_INTEGER)
    {
        f64 minimum = target->as.integer.is_signed ?
            (target->as.integer.bit_width == 64 ? -9223372036854775808.0 :
                -(f64)((u64)1 << (target->as.integer.bit_width - 1))) : 0.0;
        f64 maximum = target->as.integer.is_signed ?
            (target->as.integer.bit_width == 64 ? 9223372036854775808.0 :
                (f64)(((u64)1 << (target->as.integer.bit_width - 1)) - 1)) :
            (target->as.integer.bit_width == 64 ? 18446744073709551616.0 :
                (f64)(((u64)1 << target->as.integer.bit_width) - 1));
        if (constant.floating != constant.floating || constant.floating < minimum ||
            (target->as.integer.bit_width == 64 ? constant.floating >= maximum :
                constant.floating > maximum))
        {
            return (AnalysisConstant){0};
        }
        if (constant.floating < 0.0)
        {
            s64 signed_value = (s64)constant.floating;
            return (AnalysisConstant){
                .integer = (u64)(-(signed_value + 1)) + 1,
                .kind = ANALYSIS_CONSTANT_INTEGER,
                .is_negative = true,
            };
        }
        return (AnalysisConstant){
            .integer = (u64)constant.floating,
            .kind = ANALYSIS_CONSTANT_INTEGER,
        };
    }
    return constant;
}

BUSTER_GLOBAL_LOCAL bool analysis_float_literal_parse(AstFloatLiteral literal, f64* value_out)
{
    String8 spelling = literal.spelling;
    u64 index = literal.base == 16 ? 2 : 0;
    f64 value = 0.0;
    f64 fraction_scale = 1.0;
    bool fraction = false;
    bool saw_digit = false;
    while (index < spelling.length)
    {
        u8 byte = spelling.pointer[index];
        if (byte == '.')
        {
            if (fraction)
            {
                return false;
            }
            fraction = true;
            index += 1;
            continue;
        }
        if (byte == 'e' || byte == 'E' || byte == 'p' || byte == 'P')
        {
            break;
        }
        u32 digit = byte >= '0' && byte <= '9' ? (u32)(byte - '0') :
            byte >= 'a' && byte <= 'f' ? (u32)(byte - 'a') + 10 :
            byte >= 'A' && byte <= 'F' ? (u32)(byte - 'A') + 10 : UINT32_MAX;
        if (digit >= literal.base)
        {
            return false;
        }
        saw_digit = true;
        if (fraction)
        {
            fraction_scale /= (f64)literal.base;
            value += (f64)digit * fraction_scale;
        }
        else
        {
            value = value * (f64)literal.base + (f64)digit;
        }
        index += 1;
    }
    if (!saw_digit)
    {
        return false;
    }
    if (index < spelling.length)
    {
        index += 1;
        bool negative = false;
        if (index < spelling.length &&
            (spelling.pointer[index] == '+' || spelling.pointer[index] == '-'))
        {
            negative = spelling.pointer[index] == '-';
            index += 1;
        }
        u32 exponent = 0;
        bool saw_exponent = false;
        while (index < spelling.length)
        {
            u8 byte = spelling.pointer[index++];
            if (byte < '0' || byte > '9' || exponent > 100000)
            {
                return false;
            }
            saw_exponent = true;
            exponent = exponent * 10 + (u32)(byte - '0');
        }
        if (!saw_exponent)
        {
            return false;
        }
        f64 factor = 1.0;
        f64 base = literal.base == 16 ? 2.0 : 10.0;
        f64 power = base;
        while (exponent)
        {
            if (exponent & 1)
            {
                factor *= power;
            }
            exponent >>= 1;
            if (exponent)
            {
                power *= power;
            }
        }
        value = negative ? value / factor : value * factor;
    }
    *value_out = value;
    return true;
}

BUSTER_GLOBAL_LOCAL AnalysisConstant analysis_constant_float_binary(
    AstNodeId operation,
    AnalysisConstant left,
    AnalysisConstant right)
{
    AnalysisConstant result = {0};
    if (left.kind != ANALYSIS_CONSTANT_FLOAT || right.kind != ANALYSIS_CONSTANT_FLOAT)
    {
        return result;
    }
    f64 value = 0.0;
    switch (operation)
    {
        case AST_NODE_BINARY_PLUS: value = left.floating + right.floating; break;
        case AST_NODE_BINARY_MINUS: value = left.floating - right.floating; break;
        case AST_NODE_BINARY_ASTERISK: value = left.floating * right.floating; break;
        case AST_NODE_BINARY_SLASH:
        {
            if (right.floating == 0.0)
            {
                return result;
            }
            value = left.floating / right.floating;
        } break;
        default: return result;
    }
    result.kind = ANALYSIS_CONSTANT_FLOAT;
    result.floating = value;
    return result;
}

BUSTER_GLOBAL_LOCAL AnalysisConstant analysis_constant_integer_binary(
    AstNodeId operation,
    AnalysisConstant left,
    AnalysisConstant right)
{
    AnalysisConstant result = {0};
    if (left.kind != ANALYSIS_CONSTANT_INTEGER || right.kind != ANALYSIS_CONSTANT_INTEGER ||
        left.integer > (u64)INT64_MAX || right.integer > (u64)INT64_MAX)
    {
        return result;
    }
    s64 left_value = left.is_negative ? -(s64)left.integer : (s64)left.integer;
    s64 right_value = right.is_negative ? -(s64)right.integer : (s64)right.integer;
    s64 value = 0;
    bool known = true;
    switch (operation)
    {
        case AST_NODE_BINARY_PLUS:
        {
            known = right_value >= 0 ? left_value <= INT64_MAX - right_value :
                left_value >= INT64_MIN - right_value;
            if (known) value = left_value + right_value;
        } break;
        case AST_NODE_BINARY_MINUS:
        {
            known = right_value >= 0 ? left_value >= INT64_MIN + right_value :
                left_value <= INT64_MAX + right_value;
            if (known) value = left_value - right_value;
        } break;
        case AST_NODE_BINARY_ASTERISK:
        {
            if (!left_value || !right_value)
            {
                value = 0;
            }
            else if ((left_value == INT64_MIN && right_value == -1) ||
                (right_value == INT64_MIN && left_value == -1))
            {
                known = false;
            }
            else
            {
                value = left_value * right_value;
                known = value / right_value == left_value;
            }
        } break;
        case AST_NODE_BINARY_SLASH:
        {
            known = right_value != 0 && !(left_value == INT64_MIN && right_value == -1);
            if (known) value = left_value / right_value;
        } break;
        case AST_NODE_BINARY_PERCENT:
        {
            known = right_value != 0 && !(left_value == INT64_MIN && right_value == -1);
            if (known) value = left_value % right_value;
        } break;
        case AST_NODE_BINARY_SHIFT_LEFT:
        {
            known = right_value >= 0 && right_value < 64;
            if (known) value = (s64)((u64)left_value << (u32)right_value);
        } break;
        case AST_NODE_BINARY_SHIFT_RIGHT:
        {
            known = right_value >= 0 && right_value < 64;
            if (known) value = left_value >> (u32)right_value;
        } break;
        case AST_NODE_BINARY_AMPERSAND: value = left_value & right_value; break;
        case AST_NODE_BINARY_BAR: value = left_value | right_value; break;
        case AST_NODE_BINARY_CARET: value = left_value ^ right_value; break;
        case AST_NODE_CONSTANT_INTEGER:
        case AST_NODE_CONSTANT_FLOAT:
        case AST_NODE_CONSTANT_CHARACTER:
        case AST_NODE_CONSTANT_STRING:
        case AST_NODE_IDENTIFIER:
        case AST_NODE_UNDEFINED:
        case AST_NODE_ARRAY_LITERAL:
        case AST_NODE_ARRAY_INDEX:
        case AST_NODE_ARRAY_SLICE:
        case AST_NODE_AGGREGATE_LITERAL:
        case AST_NODE_MEMBER_ACCESS:
        case AST_NODE_ENUM_LITERAL:
        case AST_NODE_CALL:
        case AST_NODE_INTRINSIC_CALL:
        case AST_NODE_UNARY_MINUS:
        case AST_NODE_UNARY_PLUS:
        case AST_NODE_UNARY_LOGICAL_NOT:
        case AST_NODE_UNARY_BITWISE_NOT:
        case AST_NODE_ADDRESS_OF:
        case AST_NODE_DEREFERENCE:
        case AST_NODE_BINARY_EQUAL:
        case AST_NODE_BINARY_NOT_EQUAL:
        case AST_NODE_BINARY_LESS:
        case AST_NODE_BINARY_LESS_EQUAL:
        case AST_NODE_BINARY_GREATER:
        case AST_NODE_BINARY_GREATER_EQUAL:
        case AST_NODE_BINARY_BOOLEAN_AND:
        case AST_NODE_BINARY_BOOLEAN_OR:
        case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
        case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
        case AST_NODE_BINARY_RANGE:
        case AST_NODE_COUNT: known = false; break;
    }
    if (known)
    {
        result.kind = ANALYSIS_CONSTANT_INTEGER;
        result.is_negative = value < 0;
        result.integer = value < 0 ? (u64)(-(value + 1)) + 1 : (u64)value;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void analysis_expression_expect(
    AnalysisBodyContext* context,
    AnalysisTypedExpression* expression,
    u32 node_index,
    AnalysisTypeId expected)
{
    typedef struct AnalysisExpectation AnalysisExpectation;
    struct AnalysisExpectation
    {
        AnalysisTypeId type;
        u32 node_index;
    };
    AnalysisExpectation* top = arena_allocate(
        context->scratch_arena,
        AnalysisExpectation,
        expression->ast.count);
    u32 count = 1;
    bool failed = false;
    top[0] = (AnalysisExpectation){ .type = expected, .node_index = node_index };
    while (count)
    {
        AnalysisExpectation expectation = top[count - 1];
        count -= 1;
        u32 current_index = expectation.node_index;
        AnalysisTypeId current_expected = expectation.type;
        AnalysisTypeKind expected_kind = analysis_type_from_id(
            context->result,
            current_expected)->kind;
        AnalysisTypedNode* current = expression->nodes + current_index;
        AstNode* node = expression->ast.nodes + current_index;
        if (!analysis_constant_fits(context->result, current->constant, current_expected))
        {
            analysis_body_diagnostic_push(
                context,
                analysis_node_range(context->owner, node),
                ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT,
                (String8){0});
        }
        bool integer_literal = node->id == AST_NODE_CONSTANT_INTEGER ||
            node->id == AST_NODE_CONSTANT_CHARACTER;
        if ((integer_literal && expected_kind == ANALYSIS_TYPE_INTEGER) ||
            (node->id == AST_NODE_CONSTANT_FLOAT && expected_kind == ANALYSIS_TYPE_FLOAT) ||
            node->id == AST_NODE_UNDEFINED ||
            (node->id == AST_NODE_INTRINSIC_CALL &&
                string_equal(node->intrinsic_call.name.text, S8("cast"))))
        {
            current->type = current_expected;
            if (node->id == AST_NODE_UNDEFINED)
            {
                current->conversion = ANALYSIS_CONVERSION_UNDEFINED;
            }
            else if (node->id == AST_NODE_INTRINSIC_CALL)
            {
                AnalysisTypeId source_type = current_index ?
                    expression->nodes[current_index - 1].type : context->result->types.builtin.poison;
                current->conversion = analysis_explicit_conversion_kind(
                    context->result,
                    source_type,
                    current_expected);
                current->constant = analysis_constant_cast(
                    context->result,
                    current->constant,
                    current_expected);
                if (expression->nodes[current_index - 1].constant.kind != ANALYSIS_CONSTANT_NONE &&
                    current->constant.kind == ANALYSIS_CONSTANT_NONE)
                {
                    analysis_body_diagnostic_push(
                        context,
                        analysis_node_range(context->owner, node),
                        ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT,
                        (String8){0});
                }
            }
            else
            {
                current->conversion = ANALYSIS_CONVERSION_LITERAL;
            }
            continue;
        }
        if (node->id == AST_NODE_ENUM_LITERAL)
        {
            if (expected_kind != ANALYSIS_TYPE_ENUM)
            {
                failed = true;
                continue;
            }
            current->type = current_expected;
            AnalysisType* enum_type = analysis_type_from_id(context->result, current_expected);
            AnalysisEntitySemantic* semantic = context->result->module.semantics +
                enum_type->as.declaration.index.value;
            bool found = false;
            for (u32 member_index = 0; member_index < semantic->enum_member_count; member_index += 1)
            {
                if (string_equal(semantic->enum_members[member_index].name, node->enum_literal.member.text))
                {
                    found = true;
                    current->constant = (AnalysisConstant){
                        .integer = semantic->enum_members[member_index].value,
                        .kind = ANALYSIS_CONSTANT_ENUM,
                    };
                    break;
                }
            }
            if (!found)
            {
                analysis_body_diagnostic_push(
                    context,
                    node->enum_literal.member.range,
                    ANALYSIS_DIAGNOSTIC_UNKNOWN_MEMBER,
                    node->enum_literal.member.text);
            }
            continue;
        }
        if (node->id == AST_NODE_AGGREGATE_LITERAL)
        {
            AnalysisType* aggregate = analysis_type_from_id(context->result, current_expected);
            if (aggregate->kind != ANALYSIS_TYPE_STRUCT && aggregate->kind != ANALYSIS_TYPE_UNION)
            {
                failed = true;
                continue;
            }
            current->type = current_expected;
            AnalysisEntitySemantic* semantic = context->result->module.semantics +
                aggregate->as.declaration.index.value;
            bool* seen = arena_allocate(context->scratch_arena, bool, semantic->field_count);
            for (u32 field_index = 0; field_index < semantic->field_count; field_index += 1)
            {
                seen[field_index] = false;
            }
            u32 arity = node->aggregate_literal.field_count;
            u32* roots = arena_allocate(context->scratch_arena, u32, arity);
            u32 cursor = current_index;
            for (u32 child = arity; child > 0; child -= 1)
            {
                cursor -= 1;
                roots[child - 1] = cursor;
                cursor = expression->nodes[cursor].subtree_start;
            }
            u32 child = 0;
            for (AstAggregateLiteralField* literal_field = node->aggregate_literal.first_field;
                literal_field;
                literal_field = literal_field->next, child += 1)
            {
                AnalysisField* field = 0;
                u32 found_index = 0;
                for (u32 field_index = 0; field_index < semantic->field_count; field_index += 1)
                {
                    if (string_equal(semantic->fields[field_index].name, literal_field->name.text))
                    {
                        field = semantic->fields + field_index;
                        found_index = field_index;
                        break;
                    }
                }
                if (!field)
                {
                    analysis_body_diagnostic_push(
                        context,
                        literal_field->name.range,
                        ANALYSIS_DIAGNOSTIC_UNKNOWN_MEMBER,
                        literal_field->name.text);
                }
                else
                {
                    if (seen[found_index])
                    {
                        analysis_body_diagnostic_push(
                            context,
                            literal_field->name.range,
                            ANALYSIS_DIAGNOSTIC_DUPLICATE_AGGREGATE_FIELD,
                            literal_field->name.text);
                    }
                    seen[found_index] = true;
                    BUSTER_CHECK(count < expression->ast.count);
                    top[count] = (AnalysisExpectation){
                        .type = field->type,
                        .node_index = roots[child],
                    };
                    count += 1;
                }
            }
            if (aggregate->kind == ANALYSIS_TYPE_STRUCT)
            {
                for (u32 field_index = 0; field_index < semantic->field_count; field_index += 1)
                {
                    if (!seen[field_index])
                    {
                        analysis_body_diagnostic_push(
                            context,
                            node->aggregate_literal.range,
                            ANALYSIS_DIAGNOSTIC_MISSING_AGGREGATE_FIELD,
                            semantic->fields[field_index].name);
                    }
                }
            }
            else if (arity != 1)
            {
                analysis_body_diagnostic_push(
                    context,
                    node->aggregate_literal.range,
                    ANALYSIS_DIAGNOSTIC_INVALID_OPERAND,
                    (String8){0});
            }
            continue;
        }
        if (node->id == AST_NODE_ARRAY_LITERAL &&
            (expected_kind == ANALYSIS_TYPE_ARRAY || expected_kind == ANALYSIS_TYPE_INFERRED_ARRAY))
        {
            AnalysisType* array = analysis_type_from_id(context->result, current_expected);
            AnalysisTypeId element = expected_kind == ANALYSIS_TYPE_ARRAY ?
                array->as.array.element_type : array->as.element_type;
            if (expected_kind == ANALYSIS_TYPE_ARRAY &&
                array->as.array.count != node->array_literal.element_count)
            {
                failed = true;
            }
            current->type = analysis_type_intern(
                context->result_arena,
                &context->result->types,
                (AnalysisType){
                    .kind = ANALYSIS_TYPE_ARRAY,
                    .as.array = {
                        .element_type = element,
                        .count = node->array_literal.element_count,
                    },
                });
            u32 cursor = current_index;
            for (u32 child = node->array_literal.element_count; child > 0; child -= 1)
            {
                cursor -= 1;
                BUSTER_CHECK(count < expression->ast.count);
                top[count] = (AnalysisExpectation){ .type = element, .node_index = cursor };
                count += 1;
                cursor = expression->nodes[cursor].subtree_start;
            }
            continue;
        }
        bool numeric_unary = node->id == AST_NODE_UNARY_MINUS || node->id == AST_NODE_UNARY_PLUS;
        bool numeric_binary =
            (node->id >= AST_NODE_BINARY_PLUS && node->id <= AST_NODE_BINARY_PERCENT) ||
            (expected_kind == ANALYSIS_TYPE_INTEGER &&
                ((node->id >= AST_NODE_BINARY_SHIFT_LEFT && node->id <= AST_NODE_BINARY_SHIFT_RIGHT) ||
                    (node->id >= AST_NODE_BINARY_AMPERSAND && node->id <= AST_NODE_BINARY_CARET)));
        if ((expected_kind == ANALYSIS_TYPE_INTEGER || expected_kind == ANALYSIS_TYPE_FLOAT) &&
            (numeric_unary || numeric_binary))
        {
            current->type = current_expected;
            u32 right = current_index - 1;
            BUSTER_CHECK(count < expression->ast.count);
            top[count] = (AnalysisExpectation){ .type = current_expected, .node_index = right };
            count += 1;
            if (numeric_binary)
            {
                u32 left = expression->nodes[right].subtree_start - 1;
                BUSTER_CHECK(count < expression->ast.count);
                top[count] = (AnalysisExpectation){ .type = current_expected, .node_index = left };
                count += 1;
            }
            continue;
        }
        if (!analysis_type_compatible(context->result, current_expected, current->type))
        {
            failed = true;
        }
    }
    AnalysisTypedNode* root = expression->nodes + node_index;
    if (failed || !analysis_type_compatible(context->result, expected, root->type))
    {
        analysis_mismatch_diagnostic_push(
            context,
            analysis_node_range(context->owner, expression->ast.nodes + node_index),
            expected,
            root->type);
    }
}

BUSTER_GLOBAL_LOCAL AnalysisTypedExpression* analysis_expression(
    AnalysisBodyContext* context,
    AnalysisBinding* bindings,
    AstExpression ast,
    AnalysisTypeId expected)
{
    AnalysisTypeId poison = context->result->types.builtin.poison;
    AnalysisTypedExpression* expression = arena_allocate(context->result_arena, AnalysisTypedExpression, 1);
    *expression = (AnalysisTypedExpression){
        .ast = ast,
        .expected_type = expected,
        .type = poison,
    };
    expression->nodes = arena_allocate(context->result_arena, AnalysisTypedNode, ast.count);
    if (context->body->last_expression)
    {
        context->body->last_expression->next = expression;
    }
    else
    {
        context->body->first_expression = expression;
    }
    context->body->last_expression = expression;
    context->body->expression_count += 1;

    u32* operands = arena_allocate(context->scratch_arena, u32, ast.count);
    u32 operand_count = 0;
    for (u32 index = 0; index < ast.count; index += 1)
    {
        AstNode* node = ast.nodes + index;
        u32 arity = analysis_node_arity(node);
        BUSTER_CHECK(arity <= operand_count);
        u32 first_operand = operand_count - arity;
        AnalysisTypeId contextual = index + 1 == ast.count ? expected : ANALYSIS_TYPE_ID_INVALID;
        AnalysisTypedNode typed = {
            .type = poison,
            .category = ANALYSIS_VALUE_CATEGORY_TEMPORARY,
            .local = ANALYSIS_LOCAL_ID_INVALID,
            .entity = ANALYSIS_ENTITY_ID_INVALID,
            .subtree_start = arity ? expression->nodes[operands[first_operand]].subtree_start : index,
        };
        switch (node->id)
        {
            case AST_NODE_CONSTANT_INTEGER:
            {
                typed.type = context->result->types.builtin.s32_type;
                typed.constant = (AnalysisConstant){
                    .integer = node->integer.value,
                    .kind = ANALYSIS_CONSTANT_INTEGER,
                };
            } break;
            case AST_NODE_CONSTANT_FLOAT:
            {
                typed.type = context->result->types.builtin.f32_type;
                f64 value = 0.0;
                if (analysis_float_literal_parse(node->floating, &value))
                {
                    typed.constant = (AnalysisConstant){
                        .floating = value,
                        .kind = ANALYSIS_CONSTANT_FLOAT,
                    };
                }
                else
                {
                    analysis_body_diagnostic_push(
                        context,
                        analysis_node_range(context->owner, node),
                        ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT,
                        node->floating.spelling);
                }
            } break;
            case AST_NODE_CONSTANT_CHARACTER:
            {
                typed.type = context->result->types.builtin.u8_type;
                typed.constant = (AnalysisConstant){
                    .integer = node->character.code_point,
                    .kind = ANALYSIS_CONSTANT_INTEGER,
                };
            } break;
            case AST_NODE_CONSTANT_STRING:
            {
                typed.type = analysis_type_intern(
                    context->result_arena,
                    &context->result->types,
                    (AnalysisType){
                        .kind = ANALYSIS_TYPE_ARRAY,
                        .as.array = {
                            .element_type = context->result->types.builtin.u8_type,
                            .count = node->string.value.length,
                        },
                    });
                AnalysisConstant* bytes = arena_allocate(
                    context->result_arena,
                    AnalysisConstant,
                    node->string.value.length);
                for (u64 byte_index = 0; byte_index < node->string.value.length; byte_index += 1)
                {
                    bytes[byte_index] = (AnalysisConstant){
                        .integer = node->string.value.pointer[byte_index],
                        .kind = ANALYSIS_CONSTANT_INTEGER,
                    };
                }
                typed.constant = (AnalysisConstant){
                    .aggregate = {
                        .elements = bytes,
                        .element_count = (u32)node->string.value.length,
                    },
                    .kind = ANALYSIS_CONSTANT_ARRAY,
                };
            } break;
            case AST_NODE_IDENTIFIER:
            {
                AnalysisBinding* binding = analysis_binding_find(bindings, node->identifier.text);
                if (binding)
                {
                    AnalysisLocal* local = context->body->locals + binding->local.value;
                    typed.type = local->type;
                    typed.category = local->is_mutable ?
                        ANALYSIS_VALUE_CATEGORY_MUTABLE_PLACE : ANALYSIS_VALUE_CATEGORY_IMMUTABLE_PLACE;
                    typed.local = local->id;
                    typed.is_addressable = true;
                }
                else if (string_equal(node->identifier.text, S8("true")) ||
                    string_equal(node->identifier.text, S8("false")))
                {
                    typed.type = context->result->types.builtin.bool_type;
                    typed.constant = (AnalysisConstant){
                        .integer = string_equal(node->identifier.text, S8("true")),
                        .kind = ANALYSIS_CONSTANT_BOOLEAN,
                    };
                }
                else
                {
                    AnalysisEntity* entity = analysis_value_entity_find(context->result, node->identifier.text);
                    if (entity)
                    {
                        typed.type = context->result->module.semantics[entity->id.index.value].type;
                        typed.entity = entity->id;
                        typed.category = ANALYSIS_VALUE_CATEGORY_VALUE;
                        analysis_body_dependency_add(context, entity->id);
                    }
                    else
                    {
                        analysis_body_diagnostic_push(
                            context,
                            node->identifier.range,
                            ANALYSIS_DIAGNOSTIC_UNKNOWN_IDENTIFIER,
                            node->identifier.text);
                    }
                }
            } break;
            case AST_NODE_UNDEFINED:
            case AST_NODE_ENUM_LITERAL: break;
            case AST_NODE_AGGREGATE_LITERAL:
            {
                bool constant = true;
                AnalysisConstant* fields = arena_allocate(
                    context->result_arena,
                    AnalysisConstant,
                    arity);
                for (u32 child = 0; child < arity; child += 1)
                {
                    fields[child] = expression->nodes[operands[first_operand + child]].constant;
                    constant &= fields[child].kind != ANALYSIS_CONSTANT_NONE;
                }
                if (constant)
                {
                    typed.constant = (AnalysisConstant){
                        .aggregate = { .elements = fields, .element_count = arity },
                        .kind = ANALYSIS_CONSTANT_AGGREGATE,
                    };
                }
            } break;
            case AST_NODE_ARRAY_LITERAL:
            {
                AnalysisTypeId element = poison;
                if (arity)
                {
                    element = expression->nodes[operands[first_operand]].type;
                    for (u32 child = 1; child < arity; child += 1)
                    {
                        analysis_expression_expect(
                            context,
                            expression,
                            operands[first_operand + child],
                        element);
                    }
                }
                typed.type = analysis_type_intern(
                    context->result_arena,
                    &context->result->types,
                    (AnalysisType){
                        .kind = ANALYSIS_TYPE_ARRAY,
                        .as.array = { .element_type = element, .count = arity },
                    });
                bool constant = true;
                AnalysisConstant* elements = arena_allocate(
                    context->result_arena,
                    AnalysisConstant,
                    arity);
                for (u32 child = 0; child < arity; child += 1)
                {
                    elements[child] = expression->nodes[operands[first_operand + child]].constant;
                    constant &= elements[child].kind != ANALYSIS_CONSTANT_NONE;
                }
                if (constant)
                {
                    typed.constant = (AnalysisConstant){
                        .aggregate = { .elements = elements, .element_count = arity },
                        .kind = ANALYSIS_CONSTANT_ARRAY,
                    };
                }
            } break;
            case AST_NODE_ARRAY_INDEX:
            {
                AnalysisTypedNode* base = expression->nodes + operands[first_operand];
                AnalysisType* base_type = analysis_type_from_id(context->result, base->type);
                analysis_expression_expect(
                    context,
                    expression,
                    operands[first_operand + 1],
                    context->result->types.builtin.s32_type);
                if (base_type->kind == ANALYSIS_TYPE_ARRAY)
                {
                    typed.type = base_type->as.array.element_type;
                    typed.category = base->category;
                    typed.local = base->local;
                    typed.is_addressable = base->is_addressable;
                    if (base->local.value != ANALYSIS_ID_UNDERLYING_INVALID)
                    {
                        context->body->locals[base->local.value].requires_storage = true;
                    }
                }
                else if (base_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY || base_type->kind == ANALYSIS_TYPE_SLICE)
                {
                    typed.type = base_type->as.element_type;
                    typed.category = base->category;
                    typed.local = base->local;
                    typed.is_addressable = base->is_addressable;
                    if (base->local.value != ANALYSIS_ID_UNDERLYING_INVALID)
                    {
                        context->body->locals[base->local.value].requires_storage = true;
                    }
                }
                else if (base_type->kind != ANALYSIS_TYPE_POISON)
                {
                    analysis_body_diagnostic_push(
                        context,
                        node->array_index.range,
                        ANALYSIS_DIAGNOSTIC_INVALID_OPERAND,
                        (String8){0});
                }
            } break;
            case AST_NODE_ARRAY_SLICE:
            {
                AnalysisTypedNode* base = expression->nodes + operands[first_operand];
                AnalysisType* base_type = analysis_type_from_id(context->result, base->type);
                AnalysisTypeId element = poison;
                if (base_type->kind == ANALYSIS_TYPE_ARRAY)
                {
                    element = base_type->as.array.element_type;
                }
                else if (base_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY || base_type->kind == ANALYSIS_TYPE_SLICE)
                {
                    element = base_type->as.element_type;
                }
                else if (base_type->kind != ANALYSIS_TYPE_POISON)
                {
                    analysis_body_diagnostic_push(
                        context,
                        node->array_slice.range,
                        ANALYSIS_DIAGNOSTIC_INVALID_OPERAND,
                        (String8){0});
                }
                for (u32 child = 1; child < arity; child += 1)
                {
                    analysis_expression_expect(
                        context,
                        expression,
                        operands[first_operand + child],
                        context->result->types.builtin.s32_type);
                }
                typed.type = analysis_type_intern(
                    context->result_arena,
                    &context->result->types,
                    (AnalysisType){ .kind = ANALYSIS_TYPE_SLICE, .as.element_type = element });
                if (base->local.value != ANALYSIS_ID_UNDERLYING_INVALID)
                {
                    context->body->locals[base->local.value].requires_storage = true;
                }
            } break;
            case AST_NODE_MEMBER_ACCESS:
            {
                AnalysisTypedNode* base = expression->nodes + operands[first_operand];
                AnalysisType* base_type = analysis_type_from_id(context->result, base->type);
                if (base_type->kind == ANALYSIS_TYPE_STRUCT || base_type->kind == ANALYSIS_TYPE_UNION)
                {
                    AnalysisEntitySemantic* semantic = context->result->module.semantics +
                        base_type->as.declaration.index.value;
                    for (u32 field_index = 0; field_index < semantic->field_count; field_index += 1)
                    {
                        if (string_equal(semantic->fields[field_index].name, node->member_access.member.text))
                        {
                            typed.type = semantic->fields[field_index].type;
                            typed.category = base->category;
                            typed.local = base->local;
                            typed.is_addressable = base->is_addressable;
                            if (base->local.value != ANALYSIS_ID_UNDERLYING_INVALID)
                            {
                                context->body->locals[base->local.value].requires_storage = true;
                            }
                            break;
                        }
                    }
                    if (analysis_type_is_poison(&context->resolution, typed.type))
                    {
                        analysis_body_diagnostic_push(
                            context,
                            node->member_access.member.range,
                            ANALYSIS_DIAGNOSTIC_UNKNOWN_MEMBER,
                            node->member_access.member.text);
                    }
                }
                else if (base_type->kind != ANALYSIS_TYPE_POISON)
                {
                    analysis_body_diagnostic_push(
                        context,
                        node->member_access.range,
                        ANALYSIS_DIAGNOSTIC_INVALID_OPERAND,
                        node->member_access.member.text);
                }
            } break;
            case AST_NODE_CALL:
            {
                AnalysisTypedNode* callee = expression->nodes + operands[first_operand];
                AnalysisType* callee_type = analysis_type_from_id(context->result, callee->type);
                if (callee_type->kind == ANALYSIS_TYPE_FUNCTION)
                {
                    typed.type = callee_type->as.function.return_type;
                    if (node->call.argument_count != callee_type->as.function.argument_count)
                    {
                        analysis_body_diagnostic_push(
                            context,
                            node->call.range,
                            ANALYSIS_DIAGNOSTIC_ARGUMENT_COUNT,
                            (String8){0});
                    }
                    u32 common = BUSTER_MIN(
                        node->call.argument_count,
                        callee_type->as.function.argument_count);
                    for (u32 argument = 0; argument < common; argument += 1)
                    {
                        AnalysisDiagnostic* previous_diagnostic = context->result->last_diagnostic;
                        analysis_expression_expect(
                            context,
                            expression,
                            operands[first_operand + 1 + argument],
                            callee_type->as.function.argument_types[argument]);
                        AnalysisDiagnostic* argument_diagnostic = previous_diagnostic ?
                            previous_diagnostic->next : context->result->first_diagnostic;
                        while (argument_diagnostic)
                        {
                            argument_diagnostic->argument_index = argument;
                            argument_diagnostic->has_argument_index = true;
                            argument_diagnostic = argument_diagnostic->next;
                        }
                    }
                }
                else if (callee_type->kind != ANALYSIS_TYPE_POISON)
                {
                    analysis_body_diagnostic_push(
                        context,
                        node->call.range,
                        ANALYSIS_DIAGNOSTIC_NOT_CALLABLE,
                        (String8){0});
                }
            } break;
            case AST_NODE_INTRINSIC_CALL:
            {
                if (string_equal(node->intrinsic_call.name.text, S8("reverse")))
                {
                    if (arity == 1)
                    {
                        typed.type = expression->nodes[operands[first_operand]].type;
                    }
                    else
                    {
                        analysis_body_diagnostic_push(
                            context,
                            node->intrinsic_call.range,
                            ANALYSIS_DIAGNOSTIC_ARGUMENT_COUNT,
                            node->intrinsic_call.name.text);
                    }
                }
                else if (string_equal(node->intrinsic_call.name.text, S8("cast")))
                {
                    if (arity == 1)
                    {
                        typed.constant = expression->nodes[operands[first_operand]].constant;
                    }
                    if (arity != 1)
                    {
                        analysis_body_diagnostic_push(
                            context,
                            node->intrinsic_call.range,
                            ANALYSIS_DIAGNOSTIC_ARGUMENT_COUNT,
                            node->intrinsic_call.name.text);
                    }
                    else if (contextual.value != ANALYSIS_ID_UNDERLYING_INVALID)
                    {
                        typed.type = contextual;
                    }
                }
                else
                {
                    analysis_body_diagnostic_push(
                        context,
                        node->intrinsic_call.name.range,
                        ANALYSIS_DIAGNOSTIC_UNKNOWN_IDENTIFIER,
                        node->intrinsic_call.name.text);
                }
            } break;
            case AST_NODE_ADDRESS_OF:
            {
                AnalysisTypedNode* operand = expression->nodes + operands[first_operand];
                if (!operand->is_addressable &&
                    !analysis_type_id_equal(operand->type, poison))
                {
                    analysis_body_diagnostic_push(
                        context,
                        node->pointer_operator.range,
                        ANALYSIS_DIAGNOSTIC_EXPECTED_PLACE,
                        (String8){0});
                }
                typed.type = analysis_type_intern(
                    context->result_arena,
                    &context->result->types,
                    (AnalysisType){ .kind = ANALYSIS_TYPE_POINTER, .as.element_type = operand->type });
                if (operand->local.value != ANALYSIS_ID_UNDERLYING_INVALID)
                {
                    context->body->locals[operand->local.value].address_taken = true;
                    context->body->locals[operand->local.value].requires_storage = true;
                }
            } break;
            case AST_NODE_DEREFERENCE:
            {
                AnalysisTypedNode* operand = expression->nodes + operands[first_operand];
                AnalysisType* operand_type = analysis_type_from_id(context->result, operand->type);
                if (operand_type->kind == ANALYSIS_TYPE_POINTER)
                {
                    typed.type = operand_type->as.element_type;
                    typed.category = ANALYSIS_VALUE_CATEGORY_MUTABLE_PLACE;
                    typed.is_addressable = true;
                }
                else if (operand_type->kind != ANALYSIS_TYPE_POISON)
                {
                    analysis_body_diagnostic_push(
                        context,
                        node->pointer_operator.range,
                        ANALYSIS_DIAGNOSTIC_INVALID_OPERAND,
                        (String8){0});
                }
            } break;
            case AST_NODE_UNARY_MINUS:
            case AST_NODE_UNARY_PLUS:
            {
                AnalysisTypedNode* operand = expression->nodes + operands[first_operand];
                typed.type = operand->type;
                typed.constant = operand->constant;
                if (node->id == AST_NODE_UNARY_MINUS && typed.constant.kind == ANALYSIS_CONSTANT_INTEGER)
                {
                    typed.constant.is_negative = !typed.constant.is_negative;
                }
                else if (node->id == AST_NODE_UNARY_MINUS &&
                    typed.constant.kind == ANALYSIS_CONSTANT_FLOAT)
                {
                    typed.constant.floating = -typed.constant.floating;
                }
                if (!analysis_type_is_numeric(context->result, typed.type) &&
                    !analysis_type_id_equal(typed.type, poison))
                {
                    analysis_body_diagnostic_push(
                        context,
                        analysis_node_range(context->owner, node),
                        ANALYSIS_DIAGNOSTIC_INVALID_OPERAND,
                        (String8){0});
                }
            } break;
            case AST_NODE_UNARY_LOGICAL_NOT:
            {
                analysis_expression_expect(
                    context,
                    expression,
                    operands[first_operand],
                    context->result->types.builtin.bool_type);
                typed.type = context->result->types.builtin.bool_type;
                AnalysisTypedNode* operand = expression->nodes + operands[first_operand];
                if (operand->constant.kind == ANALYSIS_CONSTANT_BOOLEAN)
                {
                    typed.constant = (AnalysisConstant){
                        .integer = !operand->constant.integer,
                        .kind = ANALYSIS_CONSTANT_BOOLEAN,
                    };
                }
            } break;
            case AST_NODE_UNARY_BITWISE_NOT:
            {
                typed.type = expression->nodes[operands[first_operand]].type;
                AnalysisConstant operand_constant = expression->nodes[operands[first_operand]].constant;
                if (operand_constant.kind == ANALYSIS_CONSTANT_INTEGER && !operand_constant.is_negative)
                {
                    AnalysisType* integer_type = analysis_type_from_id(context->result, typed.type);
                    if (integer_type->kind == ANALYSIS_TYPE_INTEGER && integer_type->as.integer.is_signed &&
                        operand_constant.integer <= (u64)INT64_MAX)
                    {
                        s64 value = ~(s64)operand_constant.integer;
                        typed.constant = (AnalysisConstant){
                            .integer = value < 0 ? (u64)(-(value + 1)) + 1 : (u64)value,
                            .kind = ANALYSIS_CONSTANT_INTEGER,
                            .is_negative = value < 0,
                        };
                    }
                    else
                    {
                        u32 width = integer_type->as.integer.bit_width;
                        u64 mask = width == 64 ? UINT64_MAX : ((u64)1 << width) - 1;
                        typed.constant = (AnalysisConstant){
                            .integer = ~operand_constant.integer & mask,
                            .kind = ANALYSIS_CONSTANT_INTEGER,
                        };
                    }
                }
                if (!analysis_type_is_integer(context->result, typed.type) &&
                    !analysis_type_id_equal(typed.type, poison))
                {
                    analysis_body_diagnostic_push(
                        context,
                        analysis_node_range(context->owner, node),
                        ANALYSIS_DIAGNOSTIC_INVALID_OPERAND,
                        (String8){0});
                }
            } break;
            case AST_NODE_BINARY_PLUS:
            case AST_NODE_BINARY_MINUS:
            case AST_NODE_BINARY_ASTERISK:
            case AST_NODE_BINARY_SLASH:
            case AST_NODE_BINARY_PERCENT:
            case AST_NODE_BINARY_SHIFT_LEFT:
            case AST_NODE_BINARY_SHIFT_RIGHT:
            case AST_NODE_BINARY_AMPERSAND:
            case AST_NODE_BINARY_BAR:
            case AST_NODE_BINARY_CARET:
            case AST_NODE_BINARY_EQUAL:
            case AST_NODE_BINARY_NOT_EQUAL:
            case AST_NODE_BINARY_LESS:
            case AST_NODE_BINARY_LESS_EQUAL:
            case AST_NODE_BINARY_GREATER:
            case AST_NODE_BINARY_GREATER_EQUAL:
            case AST_NODE_BINARY_BOOLEAN_AND:
            case AST_NODE_BINARY_BOOLEAN_OR:
            case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
            case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
            case AST_NODE_BINARY_RANGE:
            {
                AnalysisTypeId left = expression->nodes[operands[first_operand]].type;
                AnalysisConstant left_constant = expression->nodes[operands[first_operand]].constant;
                AnalysisConstant right_constant = expression->nodes[operands[first_operand + 1]].constant;
                analysis_expression_expect(context, expression, operands[first_operand + 1], left);
                bool comparison = node->id >= AST_NODE_BINARY_EQUAL && node->id <= AST_NODE_BINARY_GREATER_EQUAL;
                bool equality = node->id == AST_NODE_BINARY_EQUAL || node->id == AST_NODE_BINARY_NOT_EQUAL;
                bool boolean = node->id >= AST_NODE_BINARY_BOOLEAN_AND &&
                    node->id <= AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT;
                bool bitwise = node->id == AST_NODE_BINARY_PERCENT ||
                    (node->id >= AST_NODE_BINARY_SHIFT_LEFT && node->id <= AST_NODE_BINARY_SHIFT_RIGHT) ||
                    (node->id >= AST_NODE_BINARY_AMPERSAND && node->id <= AST_NODE_BINARY_CARET);
                AnalysisTypeKind left_kind = analysis_type_from_id(context->result, left)->kind;
                bool equality_type = left_kind == ANALYSIS_TYPE_INTEGER || left_kind == ANALYSIS_TYPE_FLOAT ||
                    left_kind == ANALYSIS_TYPE_BOOL || left_kind == ANALYSIS_TYPE_POINTER ||
                    left_kind == ANALYSIS_TYPE_ENUM;
                bool valid = boolean ? analysis_type_id_equal(left, context->result->types.builtin.bool_type) :
                    bitwise || node->id == AST_NODE_BINARY_RANGE ? analysis_type_is_integer(context->result, left) :
                    equality ? equality_type : analysis_type_is_numeric(context->result, left);
                if (!valid && !analysis_type_id_equal(left, poison))
                {
                    analysis_body_diagnostic_push(
                        context,
                        analysis_node_range(context->owner, node),
                        ANALYSIS_DIAGNOSTIC_INVALID_OPERAND,
                        (String8){0});
                }
                if (node->id == AST_NODE_BINARY_RANGE)
                {
                    typed.type = analysis_type_intern(
                        context->result_arena,
                        &context->result->types,
                        (AnalysisType){ .kind = ANALYSIS_TYPE_RANGE, .as.element_type = left });
                }
                else
                {
                    typed.type = comparison ? context->result->types.builtin.bool_type : left;
                    if (boolean)
                    {
                        typed.type = context->result->types.builtin.bool_type;
                    }
                }
                if (comparison && left_constant.kind == ANALYSIS_CONSTANT_INTEGER &&
                    right_constant.kind == ANALYSIS_CONSTANT_INTEGER &&
                    left_constant.integer <= (u64)INT64_MAX && right_constant.integer <= (u64)INT64_MAX)
                {
                    s64 left_value = left_constant.is_negative ? -(s64)left_constant.integer :
                        (s64)left_constant.integer;
                    s64 right_value = right_constant.is_negative ? -(s64)right_constant.integer :
                        (s64)right_constant.integer;
                    bool value = false;
                    switch (node->id)
                    {
                        case AST_NODE_BINARY_EQUAL: value = left_value == right_value; break;
                        case AST_NODE_BINARY_NOT_EQUAL: value = left_value != right_value; break;
                        case AST_NODE_BINARY_LESS: value = left_value < right_value; break;
                        case AST_NODE_BINARY_LESS_EQUAL: value = left_value <= right_value; break;
                        case AST_NODE_BINARY_GREATER: value = left_value > right_value; break;
                        case AST_NODE_BINARY_GREATER_EQUAL: value = left_value >= right_value; break;
                        default: break;
                    }
                    typed.constant = (AnalysisConstant){
                        .integer = value,
                        .kind = ANALYSIS_CONSTANT_BOOLEAN,
                    };
                }
                else if (comparison && left_constant.kind == ANALYSIS_CONSTANT_FLOAT &&
                    right_constant.kind == ANALYSIS_CONSTANT_FLOAT)
                {
                    bool value = false;
                    switch (node->id)
                    {
                        case AST_NODE_BINARY_EQUAL:
                            value = left_constant.floating == right_constant.floating;
                            break;
                        case AST_NODE_BINARY_NOT_EQUAL:
                            value = left_constant.floating != right_constant.floating;
                            break;
                        case AST_NODE_BINARY_LESS:
                            value = left_constant.floating < right_constant.floating;
                            break;
                        case AST_NODE_BINARY_LESS_EQUAL:
                            value = left_constant.floating <= right_constant.floating;
                            break;
                        case AST_NODE_BINARY_GREATER:
                            value = left_constant.floating > right_constant.floating;
                            break;
                        case AST_NODE_BINARY_GREATER_EQUAL:
                            value = left_constant.floating >= right_constant.floating;
                            break;
                        default: break;
                    }
                    typed.constant = (AnalysisConstant){
                        .integer = value,
                        .kind = ANALYSIS_CONSTANT_BOOLEAN,
                    };
                }
                else if (boolean && left_constant.kind == ANALYSIS_CONSTANT_BOOLEAN &&
                    right_constant.kind == ANALYSIS_CONSTANT_BOOLEAN)
                {
                    bool is_and = node->id == AST_NODE_BINARY_BOOLEAN_AND ||
                        node->id == AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT;
                    typed.constant = (AnalysisConstant){
                        .integer = is_and ?
                            left_constant.integer && right_constant.integer :
                            left_constant.integer || right_constant.integer,
                        .kind = ANALYSIS_CONSTANT_BOOLEAN,
                    };
                }
                else if (!comparison && !boolean && node->id != AST_NODE_BINARY_RANGE)
                {
                    typed.constant = left_constant.kind == ANALYSIS_CONSTANT_FLOAT ?
                        analysis_constant_float_binary(node->id, left_constant, right_constant) :
                        analysis_constant_integer_binary(node->id, left_constant, right_constant);
                }
            } break;
            case AST_NODE_COUNT: break;
        }
        expression->nodes[index] = typed;
        operand_count = first_operand;
        operands[operand_count] = index;
        operand_count += 1;
    }
    BUSTER_CHECK(operand_count == 1 || ast.count == 0);
    if (ast.count)
    {
        u32 root = operands[0];
        if (expected.value != ANALYSIS_ID_UNDERLYING_INVALID)
        {
            analysis_expression_expect(context, expression, root, expected);
        }
        expression->type = expression->nodes[root].type;
        for (u32 index = 0; index < ast.count; index += 1)
        {
            AstNode* node = ast.nodes + index;
            AnalysisTypedNode* typed = expression->nodes + index;
            bool empty_array = node->id == AST_NODE_ARRAY_LITERAL && !node->array_literal.element_count;
            bool needs_context = node->id == AST_NODE_UNDEFINED ||
                node->id == AST_NODE_ENUM_LITERAL || node->id == AST_NODE_AGGREGATE_LITERAL ||
                (node->id == AST_NODE_INTRINSIC_CALL &&
                    string_equal(node->intrinsic_call.name.text, S8("cast"))) || empty_array;
            bool unresolved = analysis_type_id_equal(typed->type, poison);
            if (empty_array)
            {
                AnalysisType* array_type = analysis_type_from_id(context->result, typed->type);
                unresolved = analysis_type_is_poison(
                    &context->resolution,
                    array_type->as.array.element_type);
            }
            if (needs_context && unresolved)
            {
                analysis_body_diagnostic_push(
                    context,
                    analysis_node_range(context->owner, node),
                    ANALYSIS_DIAGNOSTIC_EXPECTED_CONTEXTUAL_TYPE,
                    (String8){0});
            }
        }
    }
    return expression;
}

BUSTER_GLOBAL_LOCAL AnalysisBodyTask* analysis_body_task_push(
    AnalysisBodyContext* context,
    AnalysisBodyTask** top,
    AstStatement* statement,
    AnalysisBinding* bindings,
    u32 scope_depth,
    u32 loop_depth)
{
    if (!statement)
    {
        return 0;
    }
    AnalysisBodyTask* task = arena_allocate(context->scratch_arena, AnalysisBodyTask, 1);
    *task = (AnalysisBodyTask){
        .previous = *top,
        .statement = statement,
        .bindings = bindings,
        .scope_depth = scope_depth,
        .loop_depth = loop_depth,
    };
    *top = task;
    return task;
}

BUSTER_GLOBAL_LOCAL void analysis_body_task_continue(
    AnalysisBodyContext* context,
    AnalysisBodyTask** top,
    AnalysisBodyTask* task,
    AnalysisBinding* bindings)
{
    analysis_body_task_push(
        context,
        top,
        task->statement->next,
        bindings,
        task->scope_depth,
        task->loop_depth);
}

BUSTER_GLOBAL_LOCAL void analysis_body_statement(
    AnalysisBodyContext* context,
    AnalysisBodyTask** top,
    AnalysisBodyTask* task,
    AnalysisTypeId return_type)
{
    AstStatement* statement = task->statement;
    AnalysisBinding* bindings = task->bindings;
    AnalysisTypeId invalid = ANALYSIS_TYPE_ID_INVALID;
    switch (statement->id)
    {
        case AST_STATEMENT_RETURN:
        {
            analysis_expression(context, bindings, statement->return_statement.expression, return_type);
            analysis_body_task_continue(context, top, task, bindings);
        } break;
        case AST_STATEMENT_DATA:
        {
            AstDataStatement* data = &statement->data_statement;
            AnalysisTypeId declared = data->type ? analysis_body_type_resolve(context, data->type) : invalid;
            AnalysisTypedExpression* initializer = analysis_expression(context, bindings, data->initializer, declared);
            AnalysisTypeId type = data->type ? declared : initializer->type;
            if (data->type)
            {
                AnalysisType* declared_type = analysis_type_from_id(context->result, declared);
                AnalysisType* initializer_type = analysis_type_from_id(context->result, initializer->type);
                if (declared_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY &&
                    initializer_type->kind == ANALYSIS_TYPE_ARRAY &&
                    analysis_type_id_equal(
                        declared_type->as.element_type,
                        initializer_type->as.array.element_type))
                {
                    type = initializer->type;
                }
            }
            AnalysisLocal* local = analysis_local_add(
                context,
                &bindings,
                data->name,
                type,
                ANALYSIS_LOCAL_DATA,
                task->scope_depth);
            local->is_initialized = !(data->initializer.count == 1 &&
                data->initializer.nodes[0].id == AST_NODE_UNDEFINED);
            analysis_body_task_continue(context, top, task, bindings);
        } break;
        case AST_STATEMENT_EXPRESSION:
        {
            analysis_expression(context, bindings, statement->expression_statement.expression, invalid);
            analysis_body_task_continue(context, top, task, bindings);
        } break;
        case AST_STATEMENT_ASSIGNMENT:
        {
            AstAssignmentStatement* assignment = &statement->assignment_statement;
            AnalysisTypedExpression* target = analysis_expression(context, bindings, assignment->target, invalid);
            if (target->ast.count)
            {
                AnalysisTypedNode* root = target->nodes + target->ast.count - 1;
                if (root->category != ANALYSIS_VALUE_CATEGORY_MUTABLE_PLACE &&
                    !analysis_type_id_equal(root->type, context->result->types.builtin.poison))
                {
                    analysis_body_diagnostic_push(
                        context,
                        statement->range,
                        ANALYSIS_DIAGNOSTIC_EXPECTED_PLACE,
                        (String8){0});
                }
            }
            analysis_expression(context, bindings, assignment->value, target->type);
            bool integer_assignment = assignment->operator >= AST_ASSIGNMENT_MODULO_EQUAL;
            bool valid_assignment = integer_assignment ?
                analysis_type_is_integer(context->result, target->type) :
                analysis_type_is_numeric(context->result, target->type);
            if (assignment->operator != AST_ASSIGNMENT_EQUAL &&
                !valid_assignment &&
                !analysis_type_id_equal(target->type, context->result->types.builtin.poison))
            {
                analysis_body_diagnostic_push(
                    context,
                    statement->range,
                    ANALYSIS_DIAGNOSTIC_INVALID_OPERAND,
                    (String8){0});
            }
            analysis_body_task_continue(context, top, task, bindings);
        } break;
        case AST_STATEMENT_IF:
        {
            AstIfStatement* if_statement = &statement->if_statement;
            analysis_expression(
                context,
                bindings,
                if_statement->condition,
                context->result->types.builtin.bool_type);
            analysis_body_task_continue(context, top, task, bindings);
            if (if_statement->alternative == AST_IF_ALTERNATIVE_BLOCK)
            {
                analysis_body_task_push(
                    context,
                    top,
                    if_statement->else_block.first_statement,
                    bindings,
                    task->scope_depth + 1,
                    task->loop_depth);
            }
            else if (if_statement->alternative == AST_IF_ALTERNATIVE_IF)
            {
                analysis_body_task_push(
                    context,
                    top,
                    if_statement->else_if,
                    bindings,
                    task->scope_depth + 1,
                    task->loop_depth);
            }
            analysis_body_task_push(
                context,
                top,
                if_statement->then_block.first_statement,
                bindings,
                task->scope_depth + 1,
                task->loop_depth);
        } break;
        case AST_STATEMENT_SWITCH:
        {
            AstSwitchStatement* switch_statement = &statement->switch_statement;
            AnalysisTypedExpression* switched = analysis_expression(
                context,
                bindings,
                switch_statement->expression,
                invalid);
            analysis_body_task_continue(context, top, task, bindings);
            AstSwitchCase** cases = arena_allocate(
                context->scratch_arena,
                AstSwitchCase*,
                switch_statement->case_count);
            u32 case_count = 0;
            AnalysisConstant* case_constants = arena_allocate(
                context->scratch_arena,
                AnalysisConstant,
                switch_statement->case_count);
            u32 constant_count = 0;
            for (AstSwitchCase* switch_case = switch_statement->first_case;
                switch_case;
                switch_case = switch_case->next)
            {
                if (!switch_case->is_else)
                {
                    AnalysisTypedExpression* case_expression = analysis_expression(
                        context,
                        bindings,
                        switch_case->expression,
                        switched->type);
                    AnalysisConstant constant = case_expression->ast.count ?
                        case_expression->nodes[case_expression->ast.count - 1].constant :
                        (AnalysisConstant){0};
                    if (constant.kind == ANALYSIS_CONSTANT_NONE)
                    {
                        analysis_body_diagnostic_push(
                            context,
                            switch_case->range,
                            ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT,
                            (String8){0});
                    }
                    else
                    {
                        bool duplicate = false;
                        for (u32 index = 0; index < constant_count; index += 1)
                        {
                            if (case_constants[index].kind == constant.kind &&
                                case_constants[index].integer == constant.integer &&
                                case_constants[index].is_negative == constant.is_negative)
                            {
                                duplicate = true;
                                analysis_body_diagnostic_push(
                                    context,
                                    switch_case->range,
                                    ANALYSIS_DIAGNOSTIC_DUPLICATE_SWITCH_CASE,
                                    (String8){0});
                                break;
                            }
                        }
                        if (!duplicate)
                        {
                            case_constants[constant_count] = constant;
                            constant_count += 1;
                        }
                    }
                }
                cases[case_count] = switch_case;
                case_count += 1;
            }
            AnalysisType* switched_type = analysis_type_from_id(context->result, switched->type);
            if (switched_type->kind == ANALYSIS_TYPE_ENUM && !switch_statement->else_case)
            {
                AnalysisEntitySemantic* enum_semantic = context->result->module.semantics +
                    switched_type->as.declaration.index.value;
                if (constant_count < enum_semantic->enum_member_count)
                {
                    analysis_body_diagnostic_push(
                        context,
                        statement->range,
                        ANALYSIS_DIAGNOSTIC_NONEXHAUSTIVE_SWITCH,
                        (String8){0});
                }
            }
            for (u32 index = case_count; index > 0; index -= 1)
            {
                analysis_body_task_push(
                    context,
                    top,
                    cases[index - 1]->body.first_statement,
                    bindings,
                    task->scope_depth + 1,
                    task->loop_depth);
            }
        } break;
        case AST_STATEMENT_FOR:
        {
            AstForStatement* for_statement = &statement->for_statement;
            AnalysisTypedExpression* iterable = analysis_expression(
                context,
                bindings,
                for_statement->iterable,
                invalid);
            AnalysisType* iterable_type = analysis_type_from_id(context->result, iterable->type);
            AnalysisTypeId element = context->result->types.builtin.poison;
            if (iterable_type->kind == ANALYSIS_TYPE_RANGE ||
                iterable_type->kind == ANALYSIS_TYPE_SLICE ||
                iterable_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY)
            {
                element = iterable_type->as.element_type;
            }
            else if (iterable_type->kind == ANALYSIS_TYPE_ARRAY)
            {
                element = iterable_type->as.array.element_type;
            }
            else if (iterable_type->kind != ANALYSIS_TYPE_POISON)
            {
                analysis_body_diagnostic_push(
                    context,
                    statement->range,
                    ANALYSIS_DIAGNOSTIC_INVALID_OPERAND,
                    for_statement->name.text);
            }
            AnalysisTypeId declared = for_statement->type ?
                analysis_body_type_resolve(context, for_statement->type) : element;
            if (for_statement->type && !analysis_type_compatible(context->result, declared, element))
            {
                analysis_body_diagnostic_push(
                    context,
                    for_statement->name.range,
                    ANALYSIS_DIAGNOSTIC_TYPE_MISMATCH,
                    for_statement->name.text);
            }
            AnalysisBinding* body_bindings = bindings;
            analysis_local_add(
                context,
                &body_bindings,
                for_statement->name,
                declared,
                ANALYSIS_LOCAL_FOR,
                task->scope_depth + 1);
            analysis_body_task_continue(context, top, task, bindings);
            analysis_body_task_push(
                context,
                top,
                for_statement->body.first_statement,
                body_bindings,
                task->scope_depth + 1,
                task->loop_depth + 1);
        } break;
        case AST_STATEMENT_LOOP:
        {
            AstLoopStatement* loop = &statement->loop_statement;
            if (loop->has_condition)
            {
                analysis_expression(
                    context,
                    bindings,
                    loop->condition,
                    context->result->types.builtin.bool_type);
            }
            analysis_body_task_continue(context, top, task, bindings);
            analysis_body_task_push(
                context,
                top,
                loop->body.first_statement,
                bindings,
                task->scope_depth + 1,
                task->loop_depth + 1);
        } break;
        case AST_STATEMENT_BREAK:
        case AST_STATEMENT_CONTINUE:
        {
            if (!task->loop_depth)
            {
                analysis_body_diagnostic_push(
                    context,
                    statement->range,
                    ANALYSIS_DIAGNOSTIC_INVALID_CONTROL_FLOW,
                    (String8){0});
            }
            analysis_body_task_continue(context, top, task, bindings);
        } break;
        case AST_STATEMENT_COUNT: break;
    }
}

BUSTER_GLOBAL_LOCAL AnalysisFlow* analysis_flow_find(
    AnalysisFlowStatement* statements,
    u32 statement_count,
    AstStatement* statement)
{
    for (u32 index = 0; index < statement_count; index += 1)
    {
        if (statements[index].statement == statement)
        {
            return &statements[index].flow;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL AnalysisFlow analysis_flow_block(
    AnalysisFlowStatement* statements,
    u32 statement_count,
    AstBlock* block)
{
    AnalysisFlow result = { .can_fall_through = true };
    for (AstStatement* statement = block->first_statement;
        statement && result.can_fall_through;
        statement = statement->next)
    {
        AnalysisFlow* flow = analysis_flow_find(statements, statement_count, statement);
        BUSTER_CHECK(flow);
        result.has_break = result.has_break || flow->has_break;
        result.can_fall_through = flow->can_fall_through;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool analysis_switch_is_exhaustive(
    AnalysisBodyContext* context,
    AstSwitchStatement* switch_statement)
{
    if (switch_statement->else_case)
    {
        return true;
    }
    AnalysisTypedExpression* switched = 0;
    for (AnalysisTypedExpression* expression = context->body->first_expression;
        expression;
        expression = expression->next)
    {
        if (expression->ast.nodes == switch_statement->expression.nodes)
        {
            switched = expression;
            break;
        }
    }
    if (!switched)
    {
        return false;
    }
    AnalysisType* type = analysis_type_from_id(context->result, switched->type);
    if (type->kind != ANALYSIS_TYPE_ENUM)
    {
        return false;
    }
    AnalysisEntitySemantic* semantic = context->result->module.semantics +
        type->as.declaration.index.value;
    bool* covered = arena_allocate(context->scratch_arena, bool, semantic->enum_member_count);
    for (u32 index = 0; index < semantic->enum_member_count; index += 1)
    {
        covered[index] = false;
    }
    for (AstSwitchCase* switch_case = switch_statement->first_case;
        switch_case;
        switch_case = switch_case->next)
    {
        if (switch_case->is_else)
        {
            continue;
        }
        AnalysisTypedExpression* case_expression = 0;
        for (AnalysisTypedExpression* expression = context->body->first_expression;
            expression;
            expression = expression->next)
        {
            if (expression->ast.nodes == switch_case->expression.nodes)
            {
                case_expression = expression;
                break;
            }
        }
        if (!case_expression || !case_expression->ast.count)
        {
            continue;
        }
        AnalysisConstant constant = case_expression->nodes[case_expression->ast.count - 1].constant;
        for (u32 member_index = 0; member_index < semantic->enum_member_count; member_index += 1)
        {
            if (constant.kind == ANALYSIS_CONSTANT_ENUM &&
                constant.integer == semantic->enum_members[member_index].value)
            {
                covered[member_index] = true;
            }
        }
    }
    for (u32 index = 0; index < semantic->enum_member_count; index += 1)
    {
        if (!covered[index])
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void analysis_control_flow(
    AnalysisBodyContext* context,
    AnalysisTypeId return_type)
{
    AstBlock* root = &context->owner->ast.code->body;
    u32 maximum = 1;
    AnalysisAstStatementLink* count_top = 0;
    analysis_ast_statement_link_push(context->scratch_arena, &count_top, root->first_statement);
    while (count_top)
    {
        AstStatement* statement = count_top->statement;
        count_top = count_top->previous;
        maximum += 1;
        switch (statement->id)
        {
            case AST_STATEMENT_IF:
            {
                maximum += 1;
                analysis_ast_statement_link_push(
                    context->scratch_arena,
                    &count_top,
                    statement->if_statement.then_block.first_statement);
                if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_BLOCK)
                {
                    maximum += 1;
                    analysis_ast_statement_link_push(
                        context->scratch_arena,
                        &count_top,
                        statement->if_statement.else_block.first_statement);
                }
                else if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_IF)
                {
                    analysis_ast_statement_link_push(
                        context->scratch_arena,
                        &count_top,
                        statement->if_statement.else_if);
                }
            } break;
            case AST_STATEMENT_SWITCH:
            {
                for (AstSwitchCase* switch_case = statement->switch_statement.first_case;
                    switch_case;
                    switch_case = switch_case->next)
                {
                    maximum += 1;
                    analysis_ast_statement_link_push(
                        context->scratch_arena,
                        &count_top,
                        switch_case->body.first_statement);
                }
            } break;
            case AST_STATEMENT_FOR:
            {
                maximum += 1;
                analysis_ast_statement_link_push(
                    context->scratch_arena,
                    &count_top,
                    statement->for_statement.body.first_statement);
            } break;
            case AST_STATEMENT_LOOP:
            {
                maximum += 1;
                analysis_ast_statement_link_push(
                    context->scratch_arena,
                    &count_top,
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

    AnalysisFlowStatement* statements = arena_allocate(
        context->scratch_arena,
        AnalysisFlowStatement,
        maximum);
    AstBlock** blocks = arena_allocate(context->scratch_arena, AstBlock*, maximum);
    u32 statement_count = 0;
    u32 block_count = 1;
    blocks[0] = root;
    AnalysisAstStatementLink* top = 0;
    analysis_ast_statement_link_push(context->scratch_arena, &top, root->first_statement);
    while (top)
    {
        AstStatement* statement = top->statement;
        top = top->previous;
        statements[statement_count] = (AnalysisFlowStatement){
            .statement = statement,
            .flow = { .can_fall_through = true },
        };
        statement_count += 1;
        switch (statement->id)
        {
            case AST_STATEMENT_IF:
            {
                blocks[block_count] = &statement->if_statement.then_block;
                block_count += 1;
                analysis_ast_statement_link_push(
                    context->scratch_arena,
                    &top,
                    statement->if_statement.then_block.first_statement);
                if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_BLOCK)
                {
                    blocks[block_count] = &statement->if_statement.else_block;
                    block_count += 1;
                    analysis_ast_statement_link_push(
                        context->scratch_arena,
                        &top,
                        statement->if_statement.else_block.first_statement);
                }
                else if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_IF)
                {
                    analysis_ast_statement_link_push(
                        context->scratch_arena,
                        &top,
                        statement->if_statement.else_if);
                }
            } break;
            case AST_STATEMENT_SWITCH:
            {
                for (AstSwitchCase* switch_case = statement->switch_statement.first_case;
                    switch_case;
                    switch_case = switch_case->next)
                {
                    blocks[block_count] = &switch_case->body;
                    block_count += 1;
                    analysis_ast_statement_link_push(
                        context->scratch_arena,
                        &top,
                        switch_case->body.first_statement);
                }
            } break;
            case AST_STATEMENT_FOR:
            {
                blocks[block_count] = &statement->for_statement.body;
                block_count += 1;
                analysis_ast_statement_link_push(
                    context->scratch_arena,
                    &top,
                    statement->for_statement.body.first_statement);
            } break;
            case AST_STATEMENT_LOOP:
            {
                blocks[block_count] = &statement->loop_statement.body;
                block_count += 1;
                analysis_ast_statement_link_push(
                    context->scratch_arena,
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
    BUSTER_CHECK(statement_count < maximum);
    BUSTER_CHECK(block_count <= maximum);

    for (u32 pass = 0; pass < statement_count + 1; pass += 1)
    {
        for (u32 index = statement_count; index > 0; index -= 1)
        {
            AstStatement* statement = statements[index - 1].statement;
            AnalysisFlow flow = { .can_fall_through = true };
            switch (statement->id)
            {
                case AST_STATEMENT_RETURN:
                case AST_STATEMENT_CONTINUE:
                {
                    flow.can_fall_through = false;
                } break;
                case AST_STATEMENT_BREAK:
                {
                    flow.can_fall_through = false;
                    flow.has_break = true;
                } break;
                case AST_STATEMENT_IF:
                {
                    AnalysisFlow then_flow = analysis_flow_block(
                        statements,
                        statement_count,
                        &statement->if_statement.then_block);
                    AnalysisFlow else_flow = { .can_fall_through = true };
                    if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_BLOCK)
                    {
                        else_flow = analysis_flow_block(
                            statements,
                            statement_count,
                            &statement->if_statement.else_block);
                    }
                    else if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_IF)
                    {
                        AnalysisFlow* nested = analysis_flow_find(
                            statements,
                            statement_count,
                            statement->if_statement.else_if);
                        BUSTER_CHECK(nested);
                        else_flow = *nested;
                    }
                    flow.can_fall_through = then_flow.can_fall_through || else_flow.can_fall_through;
                    flow.has_break = then_flow.has_break || else_flow.has_break;
                } break;
                case AST_STATEMENT_SWITCH:
                {
                    flow.can_fall_through = !analysis_switch_is_exhaustive(
                        context,
                        &statement->switch_statement);
                    for (AstSwitchCase* switch_case = statement->switch_statement.first_case;
                        switch_case;
                        switch_case = switch_case->next)
                    {
                        AnalysisFlow case_flow = analysis_flow_block(
                            statements,
                            statement_count,
                            &switch_case->body);
                        flow.can_fall_through = flow.can_fall_through || case_flow.can_fall_through;
                        flow.has_break = flow.has_break || case_flow.has_break;
                    }
                } break;
                case AST_STATEMENT_LOOP:
                {
                    AnalysisFlow loop_flow = analysis_flow_block(
                        statements,
                        statement_count,
                        &statement->loop_statement.body);
                    flow.can_fall_through = statement->loop_statement.has_condition || loop_flow.has_break;
                } break;
                case AST_STATEMENT_FOR:
                case AST_STATEMENT_DATA:
                case AST_STATEMENT_EXPRESSION:
                case AST_STATEMENT_ASSIGNMENT: break;
                case AST_STATEMENT_COUNT: break;
            }
            statements[index - 1].flow = flow;
        }
    }

    for (u32 block_index = 0; block_index < block_count; block_index += 1)
    {
        bool reachable = true;
        for (AstStatement* statement = blocks[block_index]->first_statement;
            statement;
            statement = statement->next)
        {
            if (!reachable)
            {
                context->body->has_unreachable = true;
                analysis_body_diagnostic_push(
                    context,
                    statement->range,
                    ANALYSIS_DIAGNOSTIC_UNREACHABLE_STATEMENT,
                    (String8){0});
            }
            AnalysisFlow* flow = analysis_flow_find(statements, statement_count, statement);
            BUSTER_CHECK(flow);
            reachable = reachable && flow->can_fall_through;
        }
    }
    AnalysisFlow root_flow = analysis_flow_block(statements, statement_count, root);
    context->body->can_fall_through = root_flow.can_fall_through;
    if (analysis_type_from_id(context->result, return_type)->kind != ANALYSIS_TYPE_VOID &&
        root_flow.can_fall_through)
    {
        analysis_body_diagnostic_push(
            context,
            context->owner->ast.code->body.range,
            ANALYSIS_DIAGNOSTIC_MISSING_RETURN,
            context->owner->name);
    }
}

typedef struct AnalysisInitializationLoop AnalysisInitializationLoop;
typedef struct AnalysisInitializationBreak AnalysisInitializationBreak;
struct AnalysisInitializationBreak
{
    AnalysisInitializationBreak* next;
    bool* state;
};

struct AnalysisInitializationLoop
{
    AnalysisInitializationBreak* first_break;
    AnalysisInitializationBreak* last_break;
    bool* input_state;
    bool include_input;
    u8 reserved[7];
};

typedef struct AnalysisInitializationPath AnalysisInitializationPath;
struct AnalysisInitializationPath
{
    bool* state;
    AnalysisInitializationLoop* loop;
    bool falls_through;
    u8 reserved[7];
};

typedef enum AnalysisInitializationTaskKind
{
    ANALYSIS_INITIALIZATION_TASK_STATEMENT,
    ANALYSIS_INITIALIZATION_TASK_MERGE,
    ANALYSIS_INITIALIZATION_TASK_SWITCH_MERGE,
    ANALYSIS_INITIALIZATION_TASK_LOOP_MERGE,
} AnalysisInitializationTaskKind;

typedef struct AnalysisInitializationTask AnalysisInitializationTask;
struct AnalysisInitializationTask
{
    AnalysisInitializationTask* previous;
    AstStatement* statement;
    AnalysisInitializationPath* path;
    AnalysisInitializationPath* left;
    AnalysisInitializationPath* right;
    AnalysisInitializationLoop* loop;
    AnalysisInitializationPath** paths;
    bool* input_state;
    u32 path_count;
    bool include_input;
    u8 reserved[3];
    AnalysisInitializationTaskKind kind;
};

BUSTER_GLOBAL_LOCAL bool* analysis_initialization_state_copy(
    AnalysisBodyContext* context,
    bool* source)
{
    bool* result = arena_allocate(context->scratch_arena, bool, context->body->local_count);
    memcpy(result, source, sizeof(*result) * context->body->local_count);
    return result;
}

BUSTER_GLOBAL_LOCAL AnalysisTypedExpression* analysis_expression_find(
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

BUSTER_GLOBAL_LOCAL void analysis_initialization_diagnostic_push(
    AnalysisBodyContext* context,
    AnalysisLocal* local,
    ParserSourceRange use_range)
{
    analysis_body_diagnostic_push(
        context,
        use_range,
        ANALYSIS_DIAGNOSTIC_USE_BEFORE_INITIALIZATION,
        local->name);
    AnalysisDiagnosticNote* note = arena_allocate(
        context->result_arena,
        AnalysisDiagnosticNote,
        1);
    *note = (AnalysisDiagnosticNote){
        .message = S8("local is declared uninitialized here"),
        .range = local->range,
        .entity = context->owner->id,
        .source = context->owner->source,
    };
    context->result->last_diagnostic->first_note = note;
    context->result->last_diagnostic->last_note = note;
}

BUSTER_GLOBAL_LOCAL void analysis_initialization_expression(
    AnalysisBodyContext* context,
    AstExpression ast,
    bool* state,
    AnalysisLocalId ignored_place)
{
    AnalysisTypedExpression* expression = analysis_expression_find(context->body, ast);
    if (!expression)
    {
        return;
    }
    if (ignored_place.value == ANALYSIS_ID_UNDERLYING_INVALID && expression->ast.count > 1 &&
        expression->ast.nodes[expression->ast.count - 1].id == AST_NODE_ADDRESS_OF)
    {
        ignored_place = expression->nodes[expression->ast.count - 2].local;
    }
    bool* reported = arena_allocate(context->scratch_arena, bool, context->body->local_count);
    memset(reported, 0, sizeof(*reported) * context->body->local_count);
    for (u32 index = 0; index < expression->ast.count; index += 1)
    {
        AstNode* node = expression->ast.nodes + index;
        AnalysisTypedNode* typed = expression->nodes + index;
        if (node->id != AST_NODE_IDENTIFIER ||
            typed->local.value == ANALYSIS_ID_UNDERLYING_INVALID ||
            typed->local.value == ignored_place.value)
        {
            continue;
        }
        BUSTER_CHECK(typed->local.value < context->body->local_count);
        if (!state[typed->local.value] && !reported[typed->local.value])
        {
            reported[typed->local.value] = true;
            analysis_initialization_diagnostic_push(
                context,
                context->body->locals + typed->local.value,
                node->identifier.range);
        }
    }
}

BUSTER_GLOBAL_LOCAL AnalysisLocal* analysis_initialization_declaration_local(
    AnalysisBodyContext* context,
    AstDataStatement* data)
{
    for (u32 index = 0; index < context->body->local_count; index += 1)
    {
        AnalysisLocal* local = context->body->locals + index;
        if (local->kind == ANALYSIS_LOCAL_DATA && local->range.offset == data->name.range.offset &&
            string_equal(local->name, data->name.text))
        {
            return local;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void analysis_initialization_task_push(
    AnalysisBodyContext* context,
    AnalysisInitializationTask** top,
    AnalysisInitializationTask task)
{
    AnalysisInitializationTask* pushed = arena_allocate(
        context->scratch_arena,
        AnalysisInitializationTask,
        1);
    *pushed = task;
    pushed->previous = *top;
    *top = pushed;
}

BUSTER_GLOBAL_LOCAL void analysis_definite_initialization(AnalysisBodyContext* context)
{
    bool* initial = arena_allocate(context->scratch_arena, bool, context->body->local_count);
    memset(initial, 0, sizeof(*initial) * context->body->local_count);
    for (u32 local_index = 0; local_index < context->body->local_count; local_index += 1)
    {
        AnalysisLocal* local = context->body->locals + local_index;
        initial[local_index] = local->kind == ANALYSIS_LOCAL_ARGUMENT;
    }
    AnalysisInitializationPath root = { .state = initial, .falls_through = true };
    AnalysisInitializationTask* top = 0;
    analysis_initialization_task_push(
        context,
        &top,
        (AnalysisInitializationTask){
            .statement = context->owner->ast.code->body.first_statement,
            .path = &root,
            .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
        });
    while (top)
    {
        AnalysisInitializationTask* task = top;
        top = task->previous;
        if (task->kind == ANALYSIS_INITIALIZATION_TASK_MERGE)
        {
            task->path->falls_through = task->left->falls_through || task->right->falls_through;
            if (task->path->falls_through)
            {
                for (u32 local = 0; local < context->body->local_count; local += 1)
                {
                    bool initialized = true;
                    if (task->left->falls_through)
                    {
                        initialized &= task->left->state[local];
                    }
                    if (task->right->falls_through)
                    {
                        initialized &= task->right->state[local];
                    }
                    task->path->state[local] = initialized;
                }
            }
            continue;
        }
        if (task->kind == ANALYSIS_INITIALIZATION_TASK_SWITCH_MERGE)
        {
            bool any_falls_through = task->include_input;
            for (u32 path_index = 0; path_index < task->path_count; path_index += 1)
            {
                any_falls_through |= task->paths[path_index]->falls_through;
            }
            task->path->falls_through = any_falls_through;
            for (u32 local = 0; local < context->body->local_count; local += 1)
            {
                bool initialized = true;
                if (task->include_input)
                {
                    initialized &= task->input_state[local];
                }
                for (u32 path_index = 0; path_index < task->path_count; path_index += 1)
                {
                    if (task->paths[path_index]->falls_through)
                    {
                        initialized &= task->paths[path_index]->state[local];
                    }
                }
                task->path->state[local] = initialized;
            }
            continue;
        }
        if (task->kind == ANALYSIS_INITIALIZATION_TASK_LOOP_MERGE)
        {
            bool falls_through = task->loop->include_input || task->loop->first_break != 0;
            task->path->falls_through = falls_through;
            for (u32 local = 0; local < context->body->local_count; local += 1)
            {
                bool initialized = true;
                if (task->loop->include_input)
                {
                    initialized &= task->loop->input_state[local];
                }
                for (AnalysisInitializationBreak* loop_break = task->loop->first_break;
                    loop_break;
                    loop_break = loop_break->next)
                {
                    initialized &= loop_break->state[local];
                }
                task->path->state[local] = initialized;
            }
            continue;
        }
        if (!task->statement || !task->path->falls_through)
        {
            continue;
        }
        AstStatement* statement = task->statement;
        AnalysisInitializationTask continuation = {
            .statement = statement->next,
            .path = task->path,
            .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
        };
        switch (statement->id)
        {
            case AST_STATEMENT_RETURN:
            {
                analysis_initialization_expression(
                    context,
                    statement->return_statement.expression,
                    task->path->state,
                    ANALYSIS_LOCAL_ID_INVALID);
                task->path->falls_through = false;
            } break;
            case AST_STATEMENT_DATA:
            {
                AstDataStatement* data = &statement->data_statement;
                bool undefined = data->initializer.count == 1 &&
                    data->initializer.nodes[0].id == AST_NODE_UNDEFINED;
                if (!undefined)
                {
                    analysis_initialization_expression(
                        context,
                        data->initializer,
                        task->path->state,
                        ANALYSIS_LOCAL_ID_INVALID);
                }
                AnalysisLocal* local = analysis_initialization_declaration_local(context, data);
                BUSTER_CHECK(local);
                task->path->state[local->id.value] = !undefined;
                analysis_initialization_task_push(context, &top, continuation);
            } break;
            case AST_STATEMENT_EXPRESSION:
            {
                analysis_initialization_expression(
                    context,
                    statement->expression_statement.expression,
                    task->path->state,
                    ANALYSIS_LOCAL_ID_INVALID);
                analysis_initialization_task_push(context, &top, continuation);
            } break;
            case AST_STATEMENT_ASSIGNMENT:
            {
                AstAssignmentStatement* assignment = &statement->assignment_statement;
                AnalysisTypedExpression* target = analysis_expression_find(
                    context->body,
                    assignment->target);
                AnalysisLocalId place = ANALYSIS_LOCAL_ID_INVALID;
                if (target && target->ast.count)
                {
                    place = target->nodes[target->ast.count - 1].local;
                }
                analysis_initialization_expression(
                    context,
                    assignment->target,
                    task->path->state,
                    assignment->operator == AST_ASSIGNMENT_EQUAL ? place : ANALYSIS_LOCAL_ID_INVALID);
                analysis_initialization_expression(
                    context,
                    assignment->value,
                    task->path->state,
                    ANALYSIS_LOCAL_ID_INVALID);
                if (assignment->operator == AST_ASSIGNMENT_EQUAL && target && target->ast.count &&
                    target->ast.nodes[target->ast.count - 1].id == AST_NODE_IDENTIFIER &&
                    place.value != ANALYSIS_ID_UNDERLYING_INVALID)
                {
                    task->path->state[place.value] = true;
                }
                analysis_initialization_task_push(context, &top, continuation);
            } break;
            case AST_STATEMENT_IF:
            {
                AstIfStatement* conditional = &statement->if_statement;
                analysis_initialization_expression(
                    context,
                    conditional->condition,
                    task->path->state,
                    ANALYSIS_LOCAL_ID_INVALID);
                AnalysisInitializationPath* then_path = arena_allocate(
                    context->scratch_arena,
                    AnalysisInitializationPath,
                    1);
                *then_path = (AnalysisInitializationPath){
                    .state = analysis_initialization_state_copy(context, task->path->state),
                    .loop = task->path->loop,
                    .falls_through = true,
                };
                AnalysisInitializationPath* else_path = arena_allocate(
                    context->scratch_arena,
                    AnalysisInitializationPath,
                    1);
                *else_path = (AnalysisInitializationPath){
                    .state = analysis_initialization_state_copy(context, task->path->state),
                    .loop = task->path->loop,
                    .falls_through = true,
                };
                analysis_initialization_task_push(context, &top, continuation);
                analysis_initialization_task_push(
                    context,
                    &top,
                    (AnalysisInitializationTask){
                        .path = task->path,
                        .left = then_path,
                        .right = else_path,
                        .kind = ANALYSIS_INITIALIZATION_TASK_MERGE,
                    });
                if (conditional->alternative != AST_IF_ALTERNATIVE_NONE)
                {
                    AstStatement* alternative = conditional->alternative == AST_IF_ALTERNATIVE_BLOCK ?
                        conditional->else_block.first_statement : conditional->else_if;
                    analysis_initialization_task_push(
                        context,
                        &top,
                        (AnalysisInitializationTask){
                            .statement = alternative,
                            .path = else_path,
                            .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
                        });
                }
                analysis_initialization_task_push(
                    context,
                    &top,
                    (AnalysisInitializationTask){
                        .statement = conditional->then_block.first_statement,
                        .path = then_path,
                        .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
                    });
            } break;
            case AST_STATEMENT_SWITCH:
            {
                AstSwitchStatement* switch_statement = &statement->switch_statement;
                analysis_initialization_expression(
                    context,
                    switch_statement->expression,
                    task->path->state,
                    ANALYSIS_LOCAL_ID_INVALID);
                AnalysisInitializationPath** paths = arena_allocate(
                    context->scratch_arena,
                    AnalysisInitializationPath*,
                    switch_statement->case_count);
                AstSwitchCase** cases = arena_allocate(
                    context->scratch_arena,
                    AstSwitchCase*,
                    switch_statement->case_count);
                u32 case_count = 0;
                for (AstSwitchCase* switch_case = switch_statement->first_case;
                    switch_case;
                    switch_case = switch_case->next)
                {
                    cases[case_count] = switch_case;
                    paths[case_count] = arena_allocate(
                        context->scratch_arena,
                        AnalysisInitializationPath,
                        1);
                    *paths[case_count] = (AnalysisInitializationPath){
                        .state = analysis_initialization_state_copy(context, task->path->state),
                        .loop = task->path->loop,
                        .falls_through = true,
                    };
                    if (!switch_case->is_else)
                    {
                        analysis_initialization_expression(
                            context,
                            switch_case->expression,
                            task->path->state,
                            ANALYSIS_LOCAL_ID_INVALID);
                    }
                    case_count += 1;
                }
                analysis_initialization_task_push(context, &top, continuation);
                analysis_initialization_task_push(
                    context,
                    &top,
                    (AnalysisInitializationTask){
                        .path = task->path,
                        .paths = paths,
                        .input_state = task->path->state,
                        .path_count = case_count,
                        .include_input = !switch_statement->else_case,
                        .kind = ANALYSIS_INITIALIZATION_TASK_SWITCH_MERGE,
                    });
                for (u32 case_index = case_count; case_index > 0; case_index -= 1)
                {
                    analysis_initialization_task_push(
                        context,
                        &top,
                        (AnalysisInitializationTask){
                            .statement = cases[case_index - 1]->body.first_statement,
                            .path = paths[case_index - 1],
                            .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
                        });
                }
            } break;
            case AST_STATEMENT_FOR:
            {
                analysis_initialization_expression(
                    context,
                    statement->for_statement.iterable,
                    task->path->state,
                    ANALYSIS_LOCAL_ID_INVALID);
                AnalysisInitializationPath* body_path = arena_allocate(
                    context->scratch_arena,
                    AnalysisInitializationPath,
                    1);
                *body_path = (AnalysisInitializationPath){
                    .state = analysis_initialization_state_copy(context, task->path->state),
                    .falls_through = true,
                };
                AnalysisInitializationLoop* loop = arena_allocate(
                    context->scratch_arena,
                    AnalysisInitializationLoop,
                    1);
                *loop = (AnalysisInitializationLoop){
                    .input_state = task->path->state,
                    .include_input = true,
                };
                body_path->loop = loop;
                for (u32 local = 0; local < context->body->local_count; local += 1)
                {
                    if (context->body->locals[local].kind == ANALYSIS_LOCAL_FOR &&
                        context->body->locals[local].range.offset ==
                            statement->for_statement.name.range.offset)
                    {
                        body_path->state[local] = true;
                    }
                }
                analysis_initialization_task_push(context, &top, continuation);
                analysis_initialization_task_push(
                    context,
                    &top,
                    (AnalysisInitializationTask){
                        .path = task->path,
                        .loop = loop,
                        .kind = ANALYSIS_INITIALIZATION_TASK_LOOP_MERGE,
                    });
                analysis_initialization_task_push(
                    context,
                    &top,
                    (AnalysisInitializationTask){
                        .statement = statement->for_statement.body.first_statement,
                        .path = body_path,
                        .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
                    });
            } break;
            case AST_STATEMENT_LOOP:
            {
                if (statement->loop_statement.has_condition)
                {
                    analysis_initialization_expression(
                        context,
                        statement->loop_statement.condition,
                        task->path->state,
                        ANALYSIS_LOCAL_ID_INVALID);
                }
                AnalysisInitializationPath* body_path = arena_allocate(
                    context->scratch_arena,
                    AnalysisInitializationPath,
                    1);
                *body_path = (AnalysisInitializationPath){
                    .state = analysis_initialization_state_copy(context, task->path->state),
                    .falls_through = true,
                };
                AnalysisInitializationLoop* loop = arena_allocate(
                    context->scratch_arena,
                    AnalysisInitializationLoop,
                    1);
                *loop = (AnalysisInitializationLoop){
                    .input_state = task->path->state,
                    .include_input = statement->loop_statement.has_condition,
                };
                body_path->loop = loop;
                analysis_initialization_task_push(context, &top, continuation);
                analysis_initialization_task_push(
                    context,
                    &top,
                    (AnalysisInitializationTask){
                        .path = task->path,
                        .loop = loop,
                        .kind = ANALYSIS_INITIALIZATION_TASK_LOOP_MERGE,
                    });
                analysis_initialization_task_push(
                    context,
                    &top,
                    (AnalysisInitializationTask){
                        .statement = statement->loop_statement.body.first_statement,
                        .path = body_path,
                        .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
                    });
            } break;
            case AST_STATEMENT_BREAK:
            {
                if (task->path->loop)
                {
                    AnalysisInitializationBreak* loop_break = arena_allocate(
                        context->scratch_arena,
                        AnalysisInitializationBreak,
                        1);
                    *loop_break = (AnalysisInitializationBreak){
                        .state = analysis_initialization_state_copy(context, task->path->state),
                    };
                    if (task->path->loop->last_break)
                    {
                        task->path->loop->last_break->next = loop_break;
                    }
                    else
                    {
                        task->path->loop->first_break = loop_break;
                    }
                    task->path->loop->last_break = loop_break;
                }
                task->path->falls_through = false;
            } break;
            case AST_STATEMENT_CONTINUE:
            {
                task->path->falls_through = false;
            } break;
            case AST_STATEMENT_COUNT: break;
        }
    }
}

void analysis_analyze_bodies_with_consumer(
    Arena* result_arena,
    AnalysisResult* result,
    AnalysisBodyConsumer* consumer,
    void* user_data)
{
    BUSTER_CHECK(result->types.types != 0);
    Arena* conflicts[] = { result_arena };
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    for (u32 entity_index = 0; entity_index < result->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = result->module.entities + entity_index;
        if (entity->kind != ANALYSIS_ENTITY_CODE || !entity->ast.code->has_body)
        {
            continue;
        }
        AnalysisBody* body = result->module.bodies + entity_index;
        BUSTER_CHECK(!body->analyzed);
        u32 expression_node_count = 0;
        analysis_body_capacity_measure(
            scratch.arena,
            entity->ast.code,
            &body->local_capacity,
            &expression_node_count);
        BUSTER_UNUSED(expression_node_count);
        body->locals = arena_allocate(result_arena, AnalysisLocal, body->local_capacity);
        body->dependency_capacity = expression_node_count;
        body->dependencies = arena_allocate(
            result_arena,
            AnalysisEntityId,
            body->dependency_capacity);
        AnalysisBodyContext context = {
            .result_arena = result_arena,
            .scratch_arena = scratch.arena,
            .result = result,
            .owner = entity,
            .body = body,
        };
        context.resolution = (AnalysisResolutionContext){
            .result_arena = result_arena,
            .scratch_arena = scratch.arena,
            .result = result,
        };
        AnalysisEntitySemantic* semantic = result->module.semantics + entity_index;
        AnalysisType* function = analysis_type_from_id(result, semantic->type);
        if (function->kind != ANALYSIS_TYPE_FUNCTION)
        {
            body->analyzed = true;
            if (consumer)
            {
                consumer(result_arena, scratch.arena, result, entity_index, user_data);
            }
            continue;
        }

        AnalysisBinding* bindings = 0;
        u32 argument_index = 0;
        for (AstTypeArgument* argument = entity->ast.code->type->function.first_argument;
            argument;
            argument = argument->next)
        {
            AstIdentifier identifier = { .text = argument->name, .range = argument->range };
            analysis_local_add(
                &context,
                &bindings,
                identifier,
                function->as.function.argument_types[argument_index],
                ANALYSIS_LOCAL_ARGUMENT,
                0);
            argument_index += 1;
        }
        BUSTER_CHECK(argument_index == function->as.function.argument_count);

        AnalysisBodyTask* top = 0;
        analysis_body_task_push(
            &context,
            &top,
            entity->ast.code->body.first_statement,
            bindings,
            0,
            0);
        while (top)
        {
            AnalysisBodyTask* task = top;
            top = task->previous;
            analysis_body_statement(
                &context,
                &top,
                task,
                function->as.function.return_type);
        }
        analysis_control_flow(&context, function->as.function.return_type);
        analysis_definite_initialization(&context);
        body->analyzed = true;
        if (consumer)
        {
            consumer(result_arena, scratch.arena, result, entity_index, user_data);
        }
    }
    scratch_end(scratch);
}

void analysis_analyze_bodies(Arena* result_arena, AnalysisResult* result)
{
    analysis_analyze_bodies_with_consumer(result_arena, result, 0, 0);
}

BUSTER_GLOBAL_LOCAL u64 analysis_layout_align(u64 value, u32 alignment)
{
    BUSTER_CHECK(alignment && !(alignment & (alignment - 1)));
    return (value + alignment - 1) & ~((u64)alignment - 1);
}

void analysis_compute_layouts(AnalysisResult* result, AnalysisLayoutOptions options)
{
    BUSTER_CHECK(options.pointer_size && options.pointer_alignment);
    for (u32 index = 0; index < result->types.count; index += 1)
    {
        result->types.types[index].layout = (AnalysisTypeLayout){0};
    }
    for (u32 pass = 0; pass < result->types.count + 1; pass += 1)
    {
        bool progressed = false;
        for (u32 index = 0; index < result->types.count; index += 1)
        {
            AnalysisType* type = result->types.types + index;
            if (type->layout.state == ANALYSIS_LAYOUT_RESOLVED ||
                type->layout.state == ANALYSIS_LAYOUT_ERROR)
            {
                continue;
            }
            AnalysisTypeLayout layout = { .state = ANALYSIS_LAYOUT_RESOLVED };
            bool ready = true;
            switch (type->kind)
            {
                case ANALYSIS_TYPE_POISON:
                {
                    layout.state = ANALYSIS_LAYOUT_ERROR;
                } break;
                case ANALYSIS_TYPE_VOID:
                {
                    layout.alignment = 1;
                    layout.abi_class = ANALYSIS_ABI_CLASS_NONE;
                } break;
                case ANALYSIS_TYPE_BOOL:
                {
                    layout.size = 1;
                    layout.alignment = 1;
                    layout.abi_class = ANALYSIS_ABI_CLASS_INTEGER;
                } break;
                case ANALYSIS_TYPE_INTEGER:
                {
                    layout.size = type->as.integer.bit_width / 8;
                    layout.alignment = (u32)layout.size;
                    layout.abi_class = ANALYSIS_ABI_CLASS_INTEGER;
                } break;
                case ANALYSIS_TYPE_FLOAT:
                {
                    layout.size = type->as.float_bit_width / 8;
                    layout.alignment = (u32)layout.size;
                    layout.abi_class = ANALYSIS_ABI_CLASS_FLOAT;
                } break;
                case ANALYSIS_TYPE_POINTER:
                case ANALYSIS_TYPE_FUNCTION:
                {
                    layout.size = options.pointer_size;
                    layout.alignment = options.pointer_alignment;
                    layout.abi_class = ANALYSIS_ABI_CLASS_POINTER;
                } break;
                case ANALYSIS_TYPE_SLICE:
                {
                    layout.size = (u64)options.pointer_size * 2;
                    layout.alignment = options.pointer_alignment;
                    layout.abi_class = ANALYSIS_ABI_CLASS_AGGREGATE;
                } break;
                case ANALYSIS_TYPE_INFERRED_ARRAY:
                {
                    layout.state = ANALYSIS_LAYOUT_ERROR;
                } break;
                case ANALYSIS_TYPE_ARRAY:
                {
                    AnalysisTypeLayout element = analysis_type_from_id(result, type->as.array.element_type)->layout;
                    if (element.state == ANALYSIS_LAYOUT_RESOLVED)
                    {
                        layout.size = element.size * type->as.array.count;
                        layout.alignment = element.alignment;
                        layout.abi_class = layout.size <= 16 ?
                            ANALYSIS_ABI_CLASS_AGGREGATE : ANALYSIS_ABI_CLASS_MEMORY;
                    }
                    else if (element.state == ANALYSIS_LAYOUT_ERROR)
                    {
                        layout.state = ANALYSIS_LAYOUT_ERROR;
                    }
                    else
                    {
                        ready = false;
                    }
                } break;
                case ANALYSIS_TYPE_RANGE:
                {
                    AnalysisTypeLayout element = analysis_type_from_id(result, type->as.element_type)->layout;
                    if (element.state == ANALYSIS_LAYOUT_RESOLVED)
                    {
                        layout.size = element.size * 2;
                        layout.alignment = element.alignment;
                        layout.abi_class = ANALYSIS_ABI_CLASS_AGGREGATE;
                    }
                    else if (element.state == ANALYSIS_LAYOUT_ERROR)
                    {
                        layout.state = ANALYSIS_LAYOUT_ERROR;
                    }
                    else
                    {
                        ready = false;
                    }
                } break;
                case ANALYSIS_TYPE_STRUCT:
                case ANALYSIS_TYPE_UNION:
                {
                    AnalysisEntitySemantic* semantic = result->module.semantics +
                        type->as.declaration.index.value;
                    u64 size = 0;
                    u32 alignment = 1;
                    for (u32 field_index = 0; field_index < semantic->field_count; field_index += 1)
                    {
                        AnalysisTypeLayout field_layout = analysis_type_from_id(
                            result,
                            semantic->fields[field_index].type)->layout;
                        if (field_layout.state == ANALYSIS_LAYOUT_ERROR)
                        {
                            layout.state = ANALYSIS_LAYOUT_ERROR;
                            break;
                        }
                        if (field_layout.state != ANALYSIS_LAYOUT_RESOLVED)
                        {
                            ready = false;
                            break;
                        }
                        alignment = BUSTER_MAX(alignment, field_layout.alignment);
                        if (type->kind == ANALYSIS_TYPE_STRUCT)
                        {
                            size = analysis_layout_align(size, field_layout.alignment);
                            semantic->fields[field_index].offset = size;
                            size += field_layout.size;
                        }
                        else
                        {
                            semantic->fields[field_index].offset = 0;
                            size = BUSTER_MAX(size, field_layout.size);
                        }
                    }
                    if (ready && layout.state != ANALYSIS_LAYOUT_ERROR)
                    {
                        layout.size = analysis_layout_align(size, alignment);
                        layout.alignment = alignment;
                        layout.abi_class = layout.size <= 16 ?
                            ANALYSIS_ABI_CLASS_AGGREGATE : ANALYSIS_ABI_CLASS_MEMORY;
                    }
                } break;
                case ANALYSIS_TYPE_ENUM:
                {
                    layout.size = 4;
                    layout.alignment = 4;
                    layout.abi_class = ANALYSIS_ABI_CLASS_INTEGER;
                } break;
                case ANALYSIS_TYPE_COUNT: layout.state = ANALYSIS_LAYOUT_ERROR; break;
            }
            if (ready)
            {
                type->layout = layout;
                progressed = true;
            }
        }
        if (!progressed)
        {
            break;
        }
    }
    for (u32 index = 0; index < result->types.count; index += 1)
    {
        if (result->types.types[index].layout.state == ANALYSIS_LAYOUT_UNRESOLVED)
        {
            result->types.types[index].layout.state = ANALYSIS_LAYOUT_ERROR;
        }
    }
}

BUSTER_GLOBAL_LOCAL AnalysisAbiConvention analysis_abi_convention(
    AnalysisType* function,
    Target target)
{
    if (function->as.function.calling_convention == AST_CALLING_CONVENTION_SYSTEMV)
    {
        return ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64;
    }
    if (function->as.function.calling_convention == AST_CALLING_CONVENTION_WIN64)
    {
        return ANALYSIS_ABI_CONVENTION_WIN64_X86_64;
    }
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        return target.os == OPERATING_SYSTEM_WINDOWS ? ANALYSIS_ABI_CONVENTION_WIN64_X86_64 :
            ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64;
    }
    return target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS ?
        ANALYSIS_ABI_CONVENTION_APPLE_AARCH64 : ANALYSIS_ABI_CONVENTION_AAPCS64;
}

BUSTER_GLOBAL_LOCAL bool analysis_abi_homogeneous_float(
    AnalysisResult* result,
    AnalysisTypeId type_id,
    AnalysisTypeId* element_out,
    u32* count_out)
{
    AnalysisType* type = analysis_type_from_id(result, type_id);
    AnalysisTypeId element = ANALYSIS_TYPE_ID_INVALID;
    u32 count = 0;
    if (type->kind == ANALYSIS_TYPE_ARRAY)
    {
        AnalysisType* child = analysis_type_from_id(result, type->as.array.element_type);
        if (child->kind != ANALYSIS_TYPE_FLOAT || !type->as.array.count || type->as.array.count > 4)
        {
            return false;
        }
        element = child->id;
        count = (u32)type->as.array.count;
    }
    else if (type->kind == ANALYSIS_TYPE_STRUCT)
    {
        AnalysisEntitySemantic* semantic = result->module.semantics + type->as.declaration.index.value;
        if (!semantic->field_count || semantic->field_count > 4)
        {
            return false;
        }
        for (u32 field_index = 0; field_index < semantic->field_count; field_index += 1)
        {
            AnalysisType* field = analysis_type_from_id(result, semantic->fields[field_index].type);
            if (field->kind != ANALYSIS_TYPE_FLOAT ||
                (count && field->id.value != element.value))
            {
                return false;
            }
            element = field->id;
            count += 1;
        }
    }
    else
    {
        return false;
    }
    *element_out = element;
    *count_out = count;
    return true;
}

BUSTER_GLOBAL_LOCAL AnalysisAbiValue analysis_abi_value_classify(
    AnalysisResult* result,
    AnalysisTypeId type_id,
    AnalysisAbiConvention convention,
    bool is_result)
{
    AnalysisAbiValue value = {0};
    AnalysisType* type = analysis_type_from_id(result, type_id);
    AnalysisTypeLayout layout = type->layout;
    if (type->kind == ANALYSIS_TYPE_VOID)
    {
        return value;
    }
    if (type->kind == ANALYSIS_TYPE_FLOAT)
    {
        value.part_count = 1;
        value.parts[0] = (AnalysisAbiPart){
            .abi_class = ANALYSIS_ABI_CLASS_FLOAT,
            .location = ANALYSIS_ABI_LOCATION_REGISTER,
            .size = (u32)layout.size,
        };
        return value;
    }
    bool scalar = type->kind == ANALYSIS_TYPE_BOOL || type->kind == ANALYSIS_TYPE_INTEGER ||
        type->kind == ANALYSIS_TYPE_POINTER || type->kind == ANALYSIS_TYPE_FUNCTION ||
        type->kind == ANALYSIS_TYPE_ENUM;
    if (scalar)
    {
        value.part_count = 1;
        value.parts[0] = (AnalysisAbiPart){
            .abi_class = type->kind == ANALYSIS_TYPE_POINTER || type->kind == ANALYSIS_TYPE_FUNCTION ?
                ANALYSIS_ABI_CLASS_POINTER : ANALYSIS_ABI_CLASS_INTEGER,
            .location = ANALYSIS_ABI_LOCATION_REGISTER,
            .size = (u32)layout.size,
        };
        return value;
    }
    if (convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64)
    {
        if (layout.size == 1 || layout.size == 2 || layout.size == 4 || layout.size == 8)
        {
            value.part_count = 1;
            value.parts[0] = (AnalysisAbiPart){
                .abi_class = ANALYSIS_ABI_CLASS_INTEGER,
                .location = ANALYSIS_ABI_LOCATION_REGISTER,
                .size = (u32)layout.size,
            };
        }
        else
        {
            value.indirect = true;
            value.part_count = 1;
            value.parts[0] = (AnalysisAbiPart){
                .abi_class = ANALYSIS_ABI_CLASS_POINTER,
                .location = ANALYSIS_ABI_LOCATION_INDIRECT,
                .size = 8,
            };
        }
        return value;
    }
    if (convention == ANALYSIS_ABI_CONVENTION_AAPCS64 ||
        convention == ANALYSIS_ABI_CONVENTION_APPLE_AARCH64)
    {
        AnalysisTypeId homogeneous_type = ANALYSIS_TYPE_ID_INVALID;
        u32 homogeneous_count = 0;
        if (analysis_abi_homogeneous_float(result, type_id, &homogeneous_type, &homogeneous_count))
        {
            AnalysisType* element = analysis_type_from_id(result, homogeneous_type);
            value.part_count = homogeneous_count;
            for (u32 part = 0; part < homogeneous_count; part += 1)
            {
                value.parts[part] = (AnalysisAbiPart){
                    .abi_class = ANALYSIS_ABI_CLASS_FLOAT,
                    .location = ANALYSIS_ABI_LOCATION_REGISTER,
                    .size = (u32)element->layout.size,
                };
            }
        }
        else if (layout.size <= 16)
        {
            value.part_count = (u32)((layout.size + 7) / 8);
            for (u32 part = 0; part < value.part_count; part += 1)
            {
                value.parts[part] = (AnalysisAbiPart){
                    .abi_class = ANALYSIS_ABI_CLASS_INTEGER,
                    .location = ANALYSIS_ABI_LOCATION_REGISTER,
                    .size = (u32)BUSTER_MIN((u64)8, layout.size - (u64)part * 8),
                };
            }
        }
        else
        {
            value.indirect = true;
            value.part_count = 1;
            value.parts[0] = (AnalysisAbiPart){
                .abi_class = ANALYSIS_ABI_CLASS_POINTER,
                .location = ANALYSIS_ABI_LOCATION_INDIRECT,
                .size = 8,
            };
        }
        return value;
    }
    if (layout.size > 16)
    {
        value.indirect = is_result;
        value.part_count = 1;
        value.parts[0] = (AnalysisAbiPart){
            .abi_class = is_result ? ANALYSIS_ABI_CLASS_POINTER : ANALYSIS_ABI_CLASS_MEMORY,
            .location = is_result ? ANALYSIS_ABI_LOCATION_INDIRECT : ANALYSIS_ABI_LOCATION_STACK,
            .size = is_result ? 8 : (u32)layout.size,
        };
        return value;
    }
    value.part_count = (u32)((layout.size + 7) / 8);
    for (u32 part = 0; part < value.part_count; part += 1)
    {
        value.parts[part] = (AnalysisAbiPart){
            .abi_class = ANALYSIS_ABI_CLASS_INTEGER,
            .location = ANALYSIS_ABI_LOCATION_REGISTER,
            .size = (u32)BUSTER_MIN((u64)8, layout.size - (u64)part * 8),
        };
    }
    return value;
}

AnalysisFunctionAbi analysis_classify_function_abi(
    Arena* result_arena,
    AnalysisResult* result,
    AnalysisTypeId function_type_id,
    Target target)
{
    AnalysisType* function = analysis_type_from_id(result, function_type_id);
    BUSTER_CHECK(function->kind == ANALYSIS_TYPE_FUNCTION);
    AnalysisFunctionAbi abi = {
        .convention = analysis_abi_convention(function, target),
        .argument_count = function->as.function.argument_count,
    };
    abi.arguments = arena_allocate(result_arena, AnalysisAbiValue, abi.argument_count);
    abi.result = analysis_abi_value_classify(
        result,
        function->as.function.return_type,
        abi.convention,
        true);
    for (u32 part = 0; part < abi.result.part_count; part += 1)
    {
        abi.result.parts[part].register_index = part;
    }
    u32 integer_register = 0;
    u32 float_register = 0;
    u32 windows_slot = 0;
    u32 stack_offset = abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64 ? 32 : 0;
    u32 integer_limit = abi.convention == ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64 ? 6 :
        abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64 ? 4 : 8;
    u32 float_limit = abi.convention == ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64 ? 8 :
        abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64 ? 4 : 8;
    if (abi.result.indirect && abi.convention != ANALYSIS_ABI_CONVENTION_WIN64_X86_64)
    {
        integer_register += 1;
    }
    for (u32 argument = 0; argument < abi.argument_count; argument += 1)
    {
        AnalysisTypeId argument_type = function->as.function.argument_types[argument];
        AnalysisTypeLayout layout = analysis_type_from_id(result, argument_type)->layout;
        AnalysisAbiValue value = analysis_abi_value_classify(
            result,
            argument_type,
            abi.convention,
            false);
        if (abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64)
        {
            AnalysisAbiPart* part = value.parts;
            if (windows_slot < 4)
            {
                part->register_index = windows_slot;
            }
            else
            {
                part->location = ANALYSIS_ABI_LOCATION_STACK;
                part->stack_offset = stack_offset;
                stack_offset += 8;
            }
            windows_slot += 1;
        }
        else
        {
            u32 required_integer = 0;
            u32 required_float = 0;
            for (u32 part = 0; part < value.part_count; part += 1)
            {
                required_float += value.parts[part].abi_class == ANALYSIS_ABI_CLASS_FLOAT;
                required_integer += value.parts[part].abi_class != ANALYSIS_ABI_CLASS_FLOAT;
            }
            bool registers_fit = integer_register + required_integer <= integer_limit &&
                float_register + required_float <= float_limit &&
                value.parts[0].location != ANALYSIS_ABI_LOCATION_STACK;
            if (registers_fit)
            {
                for (u32 part = 0; part < value.part_count; part += 1)
                {
                    if (value.parts[part].abi_class == ANALYSIS_ABI_CLASS_FLOAT)
                    {
                        value.parts[part].register_index = float_register++;
                    }
                    else
                    {
                        value.parts[part].register_index = integer_register++;
                    }
                }
            }
            else
            {
                u32 alignment = BUSTER_MAX(layout.alignment, 8);
                stack_offset = (u32)analysis_layout_align(stack_offset, alignment);
                for (u32 part = 0; part < value.part_count; part += 1)
                {
                    value.parts[part].location = ANALYSIS_ABI_LOCATION_STACK;
                    value.parts[part].stack_offset = stack_offset + part * 8;
                }
                stack_offset += (u32)analysis_layout_align(layout.size, 8);
            }
        }
        abi.arguments[argument] = value;
    }
    abi.stack_size = (u32)analysis_layout_align(stack_offset, 16);
    return abi;
}

void analysis_build_jobs(Arena* result_arena, AnalysisResult* result)
{
    BUSTER_CHECK(result->jobs == 0);
    result->job_count = result->module.entity_count + result->module.type_count + result->module.code_count;
    result->jobs = arena_allocate(result_arena, AnalysisJob, result->job_count);
    u32 job_index = 0;
    for (u32 entity_index = 0; entity_index < result->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = result->module.entities + entity_index;
        result->jobs[job_index] = (AnalysisJob){
            .entity = entity->id,
            .id = { .value = job_index },
            .kind = ANALYSIS_JOB_INTERFACE,
        };
        job_index += 1;
    }
    for (u32 entity_index = 0; entity_index < result->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = result->module.entities + entity_index;
        if (entity->kind != ANALYSIS_ENTITY_TYPE)
        {
            continue;
        }
        AnalysisJobId* dependency = arena_allocate(result_arena, AnalysisJobId, 1);
        AnalysisDependencyKind* dependency_kind = arena_allocate(
            result_arena,
            AnalysisDependencyKind,
            1);
        dependency[0] = (AnalysisJobId){ .value = entity_index };
        dependency_kind[0] = ANALYSIS_DEPENDENCY_INTERFACE;
        result->jobs[job_index] = (AnalysisJob){
            .dependencies = dependency,
            .dependency_kinds = dependency_kind,
            .entity = entity->id,
            .id = { .value = job_index },
            .kind = ANALYSIS_JOB_LAYOUT,
            .dependency_count = 1,
        };
        job_index += 1;
    }
    for (u32 entity_index = 0; entity_index < result->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = result->module.entities + entity_index;
        if (entity->kind != ANALYSIS_ENTITY_CODE)
        {
            continue;
        }
        AnalysisBody* body = result->module.bodies + entity_index;
        u32 dependency_capacity = body->dependency_count + 1;
        AnalysisJobId* dependencies = arena_allocate(
            result_arena,
            AnalysisJobId,
            dependency_capacity);
        AnalysisDependencyKind* dependency_kinds = arena_allocate(
            result_arena,
            AnalysisDependencyKind,
            dependency_capacity);
        u32 dependency_count = 0;
        dependencies[dependency_count] = (AnalysisJobId){ .value = entity_index };
        dependency_kinds[dependency_count] = ANALYSIS_DEPENDENCY_INTERFACE;
        dependency_count += 1;
        for (u32 body_dependency_index = 0;
            body_dependency_index < body->dependency_count;
            body_dependency_index += 1)
        {
            AnalysisEntityId dependency_entity = body->dependencies[body_dependency_index];
            if (dependency_entity.module.value != result->module.id.value)
            {
                continue;
            }
            bool duplicate = false;
            for (u32 existing = 0; existing < dependency_count; existing += 1)
            {
                duplicate |= dependencies[existing].value == dependency_entity.index.value;
            }
            if (!duplicate)
            {
                dependencies[dependency_count] = (AnalysisJobId){
                    .value = dependency_entity.index.value,
                };
                dependency_kinds[dependency_count] = ANALYSIS_DEPENDENCY_INTERFACE;
                dependency_count += 1;
            }
        }
        result->jobs[job_index] = (AnalysisJob){
            .dependencies = dependencies,
            .dependency_kinds = dependency_kinds,
            .entity = entity->id,
            .id = { .value = job_index },
            .kind = ANALYSIS_JOB_BODY,
            .dependency_count = dependency_count,
        };
        job_index += 1;
    }
    BUSTER_CHECK(job_index == result->job_count);
}

typedef struct AnalysisScheduleWorker AnalysisScheduleWorker;
struct AnalysisScheduleWorker
{
    AnalysisResult* result;
    AnalysisJobId* ready;
    AnalysisJobCallback* callback;
    void* user_data;
    u32 ready_count;
    u32 worker_index;
    u32 worker_count;
};

BUSTER_GLOBAL_LOCAL void analysis_schedule_worker(void* raw_worker)
{
    AnalysisScheduleWorker* worker = (AnalysisScheduleWorker*)raw_worker;
    for (u32 index = worker->worker_index; index < worker->ready_count; index += worker->worker_count)
    {
        worker->callback(
            worker->result->jobs + worker->ready[index].value,
            worker->worker_index,
            worker->user_data);
    }
}

AnalysisScheduleResult analysis_execute_jobs(
    Arena* result_arena,
    AnalysisResult* result,
    u32 worker_count,
    AnalysisJobCallback* callback,
    void* user_data)
{
    AnalysisScheduleResult schedule = {0};
    schedule.execution_order = arena_allocate(result_arena, AnalysisJobId, result->job_count);
    bool* complete = arena_allocate(result_arena, bool, result->job_count);
    memset(complete, 0, sizeof(*complete) * result->job_count);
    AnalysisJobId* ready = arena_allocate(result_arena, AnalysisJobId, result->job_count);
    worker_count = BUSTER_MAX(worker_count, 1);
    while (schedule.execution_count < result->job_count)
    {
        u32 ready_count = 0;
        for (u32 job_index = 0; job_index < result->job_count; job_index += 1)
        {
            if (complete[job_index])
            {
                continue;
            }
            AnalysisJob* job = result->jobs + job_index;
            bool dependencies_complete = true;
            for (u32 dependency = 0; dependency < job->dependency_count; dependency += 1)
            {
                AnalysisJobId dependency_id = job->dependencies[dependency];
                dependencies_complete &= dependency_id.value < result->job_count &&
                    complete[dependency_id.value];
            }
            if (dependencies_complete)
            {
                ready[ready_count++] = job->id;
            }
        }
        if (!ready_count)
        {
            schedule.has_cycle = true;
            break;
        }
        u32 active_workers = BUSTER_MIN(worker_count, ready_count);
        if (callback && active_workers > 1)
        {
            AnalysisScheduleWorker* workers = arena_allocate(
                result_arena,
                AnalysisScheduleWorker,
                active_workers);
            OsThreadHandle** handles = arena_allocate(result_arena, OsThreadHandle*, active_workers);
            for (u32 worker_index = 0; worker_index < active_workers; worker_index += 1)
            {
                workers[worker_index] = (AnalysisScheduleWorker){
                    .result = result,
                    .ready = ready,
                    .callback = callback,
                    .user_data = user_data,
                    .ready_count = ready_count,
                    .worker_index = worker_index,
                    .worker_count = active_workers,
                };
                handles[worker_index] = os_thread_create((ThreadCreateOptions){
                    .callback = analysis_schedule_worker,
                    .argument = workers + worker_index,
                });
            }
            for (u32 worker_index = 0; worker_index < active_workers; worker_index += 1)
            {
                BUSTER_CHECK(os_thread_join(handles[worker_index]));
            }
        }
        else if (callback)
        {
            AnalysisScheduleWorker worker = {
                .result = result,
                .ready = ready,
                .callback = callback,
                .user_data = user_data,
                .ready_count = ready_count,
                .worker_count = 1,
            };
            analysis_schedule_worker(&worker);
        }
        for (u32 index = 0; index < ready_count; index += 1)
        {
            complete[ready[index].value] = true;
            schedule.execution_order[schedule.execution_count++] = ready[index];
        }
        schedule.wave_count += 1;
    }
    return schedule;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL ParserSourceRange analysis_test_range(u32 offset)
{
    return (ParserSourceRange){ .offset = offset, .length = 1, .line = 1, .column = offset + 1 };
}

BUSTER_GLOBAL_LOCAL bool analysis_test_has_diagnostic(
    AnalysisResult* result,
    AnalysisDiagnosticKind kind)
{
    for (AnalysisDiagnostic* diagnostic = result->first_diagnostic;
        diagnostic;
        diagnostic = diagnostic->next)
    {
        if (diagnostic->kind == kind)
        {
            return true;
        }
    }
    return false;
}

typedef struct AnalysisFixtureTest AnalysisFixtureTest;
struct AnalysisFixtureTest
{
    String8 path;
    u32 expected_diagnostic_count;
};

BUSTER_GLOBAL_LOCAL AnalysisFixtureTest analysis_fixture_tests[] =
{
    { S8_INITIALIZER("tests/array_slices.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_array_literal.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_assignment.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_binary_literal.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_bitwise_not.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_boolean_operators.bbb"), 1 },
    { S8_INITIALIZER("tests/basic_break.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_character_literal.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_comment.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_continue.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_else_if.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_enum.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_float.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_for.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_function_call.bbb"), 1 },
    { S8_INITIALIZER("tests/basic_hexadecimal_literal.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_if_else.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_integer_literal_add.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_integer_literal_and.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_integer_literal_compare.bbb"), 6 },
    { S8_INITIALIZER("tests/basic_integer_literal_divide.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_integer_literal_mod.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_integer_literal_multiply.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_integer_literal_or.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_integer_literal_precedence.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_integer_literal_shift_left.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_integer_literal_shift_right.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_integer_literal_sub.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_integer_literal_xor.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_logical_not.bbb"), 2 },
    { S8_INITIALIZER("tests/basic_loop.bbb"), 1 },
    { S8_INITIALIZER("tests/basic_minimal.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_octal_literal.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_pointer.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_string_literal.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_struct.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_switch.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_type_alias.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_unary_minus.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_unary_plus.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_union.bbb"), 0 },
    { S8_INITIALIZER("tests/basic_variable.bbb"), 0 },
};

UnitTestResult analysis_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);

    AstTypeDeclaration alpha_type = {
        .name = { .text = S8("Shared"), .range = {0} },
        .range = analysis_test_range(20),
    };
    AstCode alpha_code = {
        .name = S8("duplicate"),
        .range = analysis_test_range(10),
    };
    ParserResult alpha_parser = {
        .first_code = &alpha_code,
        .last_code = &alpha_code,
        .first_type_declaration = &alpha_type,
        .last_type_declaration = &alpha_type,
        .code_count = 1,
        .type_declaration_count = 1,
    };

    AstCode zeta_shared_code = {
        .name = S8("Shared"),
        .range = analysis_test_range(5),
    };
    AstCode zeta_duplicate_code = {
        .name = S8("duplicate"),
        .range = analysis_test_range(30),
    };
    zeta_shared_code.next = &zeta_duplicate_code;
    ParserResult zeta_parser = {
        .first_code = &zeta_shared_code,
        .last_code = &zeta_duplicate_code,
        .code_count = 2,
    };

    AnalysisSourceInput reversed_inputs[] = {
        { .path = S8("zeta.bbb"), .parser = &zeta_parser },
        { .path = S8("alpha.bbb"), .parser = &alpha_parser },
    };
    AnalysisResult indexed = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 7 },
        S8("example"),
        reversed_inputs,
        BUSTER_ARRAY_LENGTH(reversed_inputs));

    BUSTER_TEST(arguments, indexed.module.source_count == 2);
    BUSTER_STRING_TEST(arguments, indexed.module.sources[0].path, S8("alpha.bbb"));
    BUSTER_STRING_TEST(arguments, indexed.module.sources[1].path, S8("zeta.bbb"));
    BUSTER_TEST(arguments, indexed.module.sources[0].id.value == 0);
    BUSTER_TEST(arguments, indexed.module.sources[1].id.value == 1);
    BUSTER_TEST(arguments, indexed.module.entity_count == 4);
    BUSTER_STRING_TEST(arguments, indexed.module.entities[0].name, S8("duplicate"));
    BUSTER_STRING_TEST(arguments, indexed.module.entities[1].name, S8("Shared"));
    BUSTER_STRING_TEST(arguments, indexed.module.entities[2].name, S8("Shared"));
    BUSTER_STRING_TEST(arguments, indexed.module.entities[3].name, S8("duplicate"));
    BUSTER_TEST(arguments, indexed.module.entities[3].id.module.value == 7);
    BUSTER_TEST(arguments, indexed.module.entities[3].id.index.value == 3);

    // Type and value namespaces may use the same spelling. Only the repeated
    // value declaration is diagnosed.
    BUSTER_TEST(arguments, indexed.diagnostic_count == 1);
    BUSTER_TEST(arguments, indexed.first_diagnostic != 0);
    BUSTER_TEST(arguments, indexed.first_diagnostic->entity.index.value == 3);
    BUSTER_TEST(arguments, indexed.first_diagnostic->previous_entity.index.value == 0);

    AnalysisSourceInput forward_inputs[] = {
        { .path = S8("alpha.bbb"), .parser = &alpha_parser },
        { .path = S8("zeta.bbb"), .parser = &zeta_parser },
    };
    AnalysisResult forward = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 7 },
        S8("example"),
        forward_inputs,
        BUSTER_ARRAY_LENGTH(forward_inputs));
    BUSTER_TEST(arguments, forward.module.entity_count == indexed.module.entity_count);
    for (u32 index = 0; index < indexed.module.entity_count; index += 1)
    {
        BUSTER_STRING_TEST(arguments, forward.module.entities[index].name, indexed.module.entities[index].name);
        BUSTER_TEST(
            arguments,
            forward.module.entities[index].id.index.value == indexed.module.entities[index].id.index.value);
        BUSTER_TEST(
            arguments,
            forward.module.entities[index].source.value == indexed.module.entities[index].source.value);
    }

    AstType named_b = {
        .range = analysis_test_range(1),
        .id = AST_TYPE_NAMED,
        .name = S8("B"),
    };
    AstType named_s32 = {
        .range = analysis_test_range(11),
        .id = AST_TYPE_NAMED,
        .name = S8("s32"),
    };
    AstType named_a = {
        .range = analysis_test_range(21),
        .id = AST_TYPE_NAMED,
        .name = S8("A"),
    };
    AstType pointer_a = {
        .range = analysis_test_range(20),
        .id = AST_TYPE_POINTER,
        .element_type = &named_a,
    };
    AstType named_node = {
        .range = analysis_test_range(32),
        .id = AST_TYPE_NAMED,
        .name = S8("Node"),
    };
    AstType pointer_node = {
        .range = analysis_test_range(31),
        .id = AST_TYPE_POINTER,
        .element_type = &named_node,
    };
    AstType missing = {
        .range = analysis_test_range(41),
        .id = AST_TYPE_NAMED,
        .name = S8("Missing"),
    };
    AstType named_d = {
        .range = analysis_test_range(51),
        .id = AST_TYPE_NAMED,
        .name = S8("D"),
    };
    AstType named_c = {
        .range = analysis_test_range(61),
        .id = AST_TYPE_NAMED,
        .name = S8("C"),
    };

    AstTypeDeclaration alias_a = {
        .alias_type = &named_b,
        .name = { .text = S8("A") },
        .range = analysis_test_range(0),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeDeclaration alias_b = {
        .alias_type = &named_s32,
        .name = { .text = S8("B") },
        .range = analysis_test_range(10),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeDeclaration alias_pointer = {
        .alias_type = &pointer_a,
        .name = { .text = S8("Pointer") },
        .range = analysis_test_range(20),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeField node_next = {
        .name = { .text = S8("next") },
        .type = &pointer_node,
        .range = analysis_test_range(31),
    };
    AstTypeDeclaration node = {
        .first_field = &node_next,
        .last_field = &node_next,
        .name = { .text = S8("Node") },
        .range = analysis_test_range(30),
        .kind = AST_TYPE_DECLARATION_STRUCT,
        .field_count = 1,
    };
    AstTypeDeclaration unknown = {
        .alias_type = &missing,
        .name = { .text = S8("Unknown") },
        .range = analysis_test_range(40),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeDeclaration alias_c = {
        .alias_type = &named_d,
        .name = { .text = S8("C") },
        .range = analysis_test_range(50),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeDeclaration alias_d = {
        .alias_type = &named_c,
        .name = { .text = S8("D") },
        .range = analysis_test_range(60),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    alias_a.next = &alias_b;
    alias_b.next = &alias_pointer;
    alias_pointer.next = &node;
    node.next = &unknown;
    unknown.next = &alias_c;
    alias_c.next = &alias_d;

    AstType named_pointer = {
        .range = analysis_test_range(72),
        .id = AST_TYPE_NAMED,
        .name = S8("Pointer"),
    };
    AstType function_return = {
        .range = analysis_test_range(73),
        .id = AST_TYPE_NAMED,
        .name = S8("A"),
    };
    AstTypeArgument function_argument = {
        .name = S8("value"),
        .type = &named_pointer,
        .range = analysis_test_range(72),
    };
    AstType function_type = {
        .range = analysis_test_range(71),
        .id = AST_TYPE_FUNCTION,
        .function = {
            .first_argument = &function_argument,
            .last_argument = &function_argument,
            .return_type = &function_return,
            .calling_convention = AST_CALLING_CONVENTION_C,
            .argument_count = 1,
        },
    };
    AstCode function = {
        .name = S8("consume"),
        .type = &function_type,
        .range = analysis_test_range(70),
    };
    ParserResult resolution_parser = {
        .first_code = &function,
        .last_code = &function,
        .first_type_declaration = &alias_a,
        .last_type_declaration = &alias_d,
        .code_count = 1,
        .type_declaration_count = 7,
    };
    AnalysisSourceInput resolution_input = {
        .path = S8("types.bbb"),
        .parser = &resolution_parser,
    };
    AnalysisResult resolved = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 9 },
        S8("types"),
        &resolution_input,
        1);
    analysis_resolve_module_interfaces(arguments->arena, &resolved);

    BUSTER_TEST(arguments, resolved.types.count > 13);
    BUSTER_TEST(
        arguments,
        resolved.module.semantics[0].type.value == resolved.types.builtin.s32_type.value);
    BUSTER_TEST(
        arguments,
        resolved.module.semantics[1].type.value == resolved.types.builtin.s32_type.value);
    AnalysisType* pointer_type = analysis_type_from_id(&resolved, resolved.module.semantics[2].type);
    BUSTER_TEST(arguments, pointer_type->kind == ANALYSIS_TYPE_POINTER);
    BUSTER_TEST(arguments, pointer_type->as.element_type.value == resolved.types.builtin.s32_type.value);

    AnalysisType* node_type = analysis_type_from_id(&resolved, resolved.module.semantics[3].type);
    BUSTER_TEST(arguments, node_type->kind == ANALYSIS_TYPE_STRUCT);
    BUSTER_TEST(arguments, resolved.module.semantics[3].state == ANALYSIS_RESOLUTION_RESOLVED);
    BUSTER_TEST(arguments, resolved.module.semantics[3].field_count == 1);
    AnalysisType* next_type = analysis_type_from_id(&resolved, resolved.module.semantics[3].fields[0].type);
    BUSTER_TEST(arguments, next_type->kind == ANALYSIS_TYPE_POINTER);
    BUSTER_TEST(arguments, next_type->as.element_type.value == node_type->id.value);

    BUSTER_TEST(arguments, resolved.module.semantics[4].state == ANALYSIS_RESOLUTION_ERROR);
    BUSTER_TEST(arguments, resolved.module.semantics[5].state == ANALYSIS_RESOLUTION_ERROR);
    BUSTER_TEST(arguments, resolved.module.semantics[6].state == ANALYSIS_RESOLUTION_ERROR);
    AnalysisType* code_type = analysis_type_from_id(&resolved, resolved.module.semantics[7].type);
    BUSTER_TEST(arguments, code_type->kind == ANALYSIS_TYPE_FUNCTION);
    BUSTER_TEST(arguments, code_type->as.function.argument_count == 1);
    BUSTER_TEST(
        arguments,
        code_type->as.function.argument_types[0].value == resolved.module.semantics[2].type.value);
    BUSTER_TEST(
        arguments,
        code_type->as.function.return_type.value == resolved.types.builtin.s32_type.value);

    BUSTER_TEST(arguments, resolved.diagnostic_count == 2);
    BUSTER_TEST(arguments, resolved.first_diagnostic->kind == ANALYSIS_DIAGNOSTIC_UNKNOWN_TYPE);
    BUSTER_STRING_TEST(arguments, resolved.first_diagnostic->subject, S8("Missing"));
    BUSTER_TEST(arguments, resolved.last_diagnostic->kind == ANALYSIS_DIAGNOSTIC_TYPE_ALIAS_CYCLE);
    analysis_compute_layouts(
        &resolved,
        (AnalysisLayoutOptions){ .pointer_size = 8, .pointer_alignment = 8 });
    BUSTER_TEST(arguments, node_type->layout.state == ANALYSIS_LAYOUT_RESOLVED);
    BUSTER_TEST(arguments, node_type->layout.size == 8);
    BUSTER_TEST(arguments, node_type->layout.alignment == 8);
    BUSTER_TEST(arguments, resolved.module.semantics[3].fields[0].offset == 0);
    BUSTER_TEST(arguments, code_type->layout.abi_class == ANALYSIS_ABI_CLASS_POINTER);
    AnalysisFunctionAbi systemv_abi = analysis_classify_function_abi(
        arguments->arena,
        &resolved,
        code_type->id,
        (Target){ .cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX });
    BUSTER_TEST(arguments, systemv_abi.convention == ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64);
    BUSTER_TEST(arguments, systemv_abi.argument_count == 1);
    BUSTER_TEST(arguments, systemv_abi.arguments[0].parts[0].register_index == 0);
    BUSTER_TEST(arguments, systemv_abi.result.parts[0].register_index == 0);
    BUSTER_TEST(arguments, systemv_abi.stack_size == 0);
    AnalysisFunctionAbi windows_abi = analysis_classify_function_abi(
        arguments->arena,
        &resolved,
        code_type->id,
        (Target){ .cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS });
    BUSTER_TEST(arguments, windows_abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64);
    BUSTER_TEST(arguments, windows_abi.stack_size == 32);
    AnalysisFunctionAbi aapcs_abi = analysis_classify_function_abi(
        arguments->arena,
        &resolved,
        code_type->id,
        (Target){ .cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX });
    BUSTER_TEST(arguments, aapcs_abi.convention == ANALYSIS_ABI_CONVENTION_AAPCS64);
    BUSTER_TEST(arguments, aapcs_abi.arguments[0].parts[0].register_index == 0);
    analysis_build_jobs(arguments->arena, &resolved);
    BUSTER_TEST(arguments, resolved.job_count == 16);
    BUSTER_TEST(arguments, resolved.jobs[0].kind == ANALYSIS_JOB_INTERFACE);
    BUSTER_TEST(arguments, resolved.jobs[8].kind == ANALYSIS_JOB_LAYOUT);
    BUSTER_TEST(arguments, resolved.jobs[15].kind == ANALYSIS_JOB_BODY);
    BUSTER_TEST(arguments, resolved.jobs[15].dependency_count == 1);
    BUSTER_TEST(
        arguments,
        resolved.jobs[15].dependency_kinds[0] == ANALYSIS_DEPENDENCY_INTERFACE);
    AnalysisScheduleResult schedule = analysis_execute_jobs(
        arguments->arena,
        &resolved,
        4,
        0,
        0);
    BUSTER_TEST(arguments, !schedule.has_cycle);
    BUSTER_TEST(arguments, schedule.execution_count == resolved.job_count);
    BUSTER_TEST(arguments, schedule.wave_count == 2);
    for (u32 execution_index = 0;
        execution_index < schedule.execution_count;
        execution_index += 1)
    {
        AnalysisJob* scheduled = resolved.jobs + schedule.execution_order[execution_index].value;
        for (u32 dependency_index = 0;
            dependency_index < scheduled->dependency_count;
            dependency_index += 1)
        {
            bool dependency_precedes = false;
            for (u32 previous = 0; previous < execution_index; previous += 1)
            {
                dependency_precedes |= schedule.execution_order[previous].value ==
                    scheduled->dependencies[dependency_index].value;
            }
            BUSTER_TEST(arguments, dependency_precedes);
        }
    }

    String8 body_source = S8(
        "code add : fn (a: s32, b: s32) s32\n"
        "{\n"
        "    data total = a + b;\n"
        "    if (a < b)\n"
        "    {\n"
        "        data a: s32 = b;\n"
        "        total += a;\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        total += 1;\n"
        "    }\n"
        "    for (data i = 0 .. 2)\n"
        "    {\n"
        "        total += i;\n"
        "    }\n"
        "    loop (total < 100)\n"
        "    {\n"
        "        total += 1;\n"
        "        break;\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "code call : fn () s32\n"
        "{\n"
        "    return add(1, 2);\n"
        "}\n");
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);
    TokenizerResult body_tokens = tokenize(
        arguments->arena,
        body_source.pointer,
        body_source.length);
    ParserResult body_parser = parser_parse(
        arguments->arena,
        expression_arena,
        body_source,
        body_tokens);
    BUSTER_TEST(arguments, body_tokens.error_count == 0);
    BUSTER_TEST(arguments, body_parser.diagnostic_count == 0);
    AnalysisSourceInput body_input = {
        .path = S8("semantic-body.bbb"),
        .parser = &body_parser,
    };
    AnalysisResult body_result = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 10 },
        S8("semantic-body"),
        &body_input,
        1);
    analysis_resolve_module_interfaces(arguments->arena, &body_result);
    analysis_analyze_bodies(arguments->arena, &body_result);
    BUSTER_TEST(arguments, body_result.diagnostic_count == 0);
    BUSTER_TEST(arguments, body_result.module.code_count == 2);
    for (u32 index = 0; index < body_result.module.entity_count; index += 1)
    {
        AnalysisEntity* entity = body_result.module.entities + index;
        AnalysisBody* body = body_result.module.bodies + index;
        BUSTER_TEST(arguments, body->analyzed);
        if (string_equal(entity->name, S8("add")))
        {
            BUSTER_TEST(arguments, body->local_count == 5);
            BUSTER_TEST(arguments, body->locals[0].kind == ANALYSIS_LOCAL_ARGUMENT);
            BUSTER_TEST(arguments, body->locals[0].is_mutable);
            BUSTER_TEST(arguments, body->locals[0].is_initialized);
            BUSTER_TEST(arguments, body->locals[2].kind == ANALYSIS_LOCAL_DATA);
            BUSTER_TEST(arguments, body->locals[2].is_mutable);
            BUSTER_TEST(arguments, body->locals[2].is_initialized);
            BUSTER_TEST(arguments, body->locals[3].scope_depth == 1);
            BUSTER_TEST(arguments, body->locals[4].kind == ANALYSIS_LOCAL_FOR);
            BUSTER_TEST(arguments, body->expression_count == 14);
        }
        else
        {
            BUSTER_STRING_TEST(arguments, entity->name, S8("call"));
            BUSTER_TEST(arguments, body->local_count == 0);
            BUSTER_TEST(arguments, body->expression_count == 1);
            BUSTER_TEST(
                arguments,
                body->first_expression->type.value == body_result.types.builtin.s32_type.value);
        }
    }

    String8 constant_source = S8(
        "code constants : fn () f64\n"
        "{\n"
        "    data sum: f64 = 1.25 + 2.75;\n"
        "    data hexadecimal: f64 = 0x1.fp+2;\n"
        "    data integer: s32 = @cast(3.75);\n"
        "    return sum + hexadecimal + @cast(integer);\n"
        "}\n");
    TokenizerResult constant_tokens = tokenize(
        arguments->arena,
        constant_source.pointer,
        constant_source.length);
    ParserResult constant_parser = parser_parse(
        arguments->arena,
        expression_arena,
        constant_source,
        constant_tokens);
    BUSTER_TEST(arguments, constant_tokens.error_count == 0);
    BUSTER_TEST(arguments, constant_parser.diagnostic_count == 0);
    AnalysisSourceInput constant_input = {
        .path = S8("semantic-constants.bbb"),
        .parser = &constant_parser,
    };
    AnalysisResult constant_result = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 12 },
        S8("semantic-constants"),
        &constant_input,
        1);
    analysis_resolve_module_interfaces(arguments->arena, &constant_result);
    analysis_analyze_bodies(arguments->arena, &constant_result);
    BUSTER_TEST(arguments, constant_result.diagnostic_count == 0);
    AnalysisTypedExpression* constant_expression =
        constant_result.module.bodies[0].first_expression;
    BUSTER_TEST(arguments, constant_expression != 0);
    if (constant_expression)
    {
        AnalysisConstant folded = constant_expression->nodes[constant_expression->ast.count - 1].constant;
        BUSTER_TEST(arguments, folded.kind == ANALYSIS_CONSTANT_FLOAT);
        BUSTER_TEST(arguments, folded.floating == 4.0);
        constant_expression = constant_expression->next;
    }
    BUSTER_TEST(arguments, constant_expression != 0);
    if (constant_expression)
    {
        AnalysisConstant hexadecimal =
            constant_expression->nodes[constant_expression->ast.count - 1].constant;
        BUSTER_TEST(arguments, hexadecimal.kind == ANALYSIS_CONSTANT_FLOAT);
        BUSTER_TEST(arguments, hexadecimal.floating == 7.75);
        constant_expression = constant_expression->next;
    }
    BUSTER_TEST(arguments, constant_expression != 0);
    if (constant_expression)
    {
        AnalysisConstant cast = constant_expression->nodes[constant_expression->ast.count - 1].constant;
        BUSTER_TEST(arguments, cast.kind == ANALYSIS_CONSTANT_INTEGER);
        BUSTER_TEST(arguments, cast.integer == 3 && !cast.is_negative);
    }

    String8 initialization_source = S8(
        "code initialization : fn (condition: bool) s32\n"
        "{\n"
        "    data complete: s32 = undefined;\n"
        "    if (condition) { complete = 1; } else { complete = 2; }\n"
        "    data partial: s32 = undefined;\n"
        "    if (condition) { partial = 1; }\n"
        "    data bad: s32 = partial;\n"
        "    data compound: s32 = undefined;\n"
        "    compound += 1;\n"
        "    data addressed: s32 = undefined;\n"
        "    data pointer: &s32 = &addressed;\n"
        "    addressed = 3;\n"
        "    data through_break: s32 = undefined;\n"
        "    loop\n"
        "    {\n"
        "        if (condition) { through_break = 4; break; }\n"
        "        else { through_break = 5; break; }\n"
        "    }\n"
        "    return complete + addressed + through_break;\n"
        "}\n");
    TokenizerResult initialization_tokens = tokenize(
        arguments->arena,
        initialization_source.pointer,
        initialization_source.length);
    ParserResult initialization_parser = parser_parse(
        arguments->arena,
        expression_arena,
        initialization_source,
        initialization_tokens);
    BUSTER_TEST(arguments, initialization_tokens.error_count == 0);
    BUSTER_TEST(arguments, initialization_parser.diagnostic_count == 0);
    AnalysisSourceInput initialization_input = {
        .path = S8("semantic-initialization.bbb"),
        .parser = &initialization_parser,
    };
    AnalysisResult initialization_result = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 13 },
        S8("semantic-initialization"),
        &initialization_input,
        1);
    analysis_resolve_module_interfaces(arguments->arena, &initialization_result);
    analysis_analyze_bodies(arguments->arena, &initialization_result);
    BUSTER_TEST(arguments, initialization_result.diagnostic_count == 2);
    for (AnalysisDiagnostic* diagnostic = initialization_result.first_diagnostic;
        diagnostic;
        diagnostic = diagnostic->next)
    {
        BUSTER_TEST(
            arguments,
            diagnostic->kind == ANALYSIS_DIAGNOSTIC_USE_BEFORE_INITIALIZATION);
        BUSTER_TEST(arguments, diagnostic->first_note != 0);
        if (diagnostic->first_note)
        {
            BUSTER_STRING_TEST(
                arguments,
                diagnostic->first_note->message,
                S8("local is declared uninitialized here"));
        }
    }

    String8 error_source = S8(
        "code broken : fn (value: s32) s32\n"
        "{\n"
        "    data value: s32 = 0;\n"
        "    data result: s32 = missing;\n"
        "    1 = result;\n"
        "    break;\n"
        "    return result;\n"
        "}\n");
    TokenizerResult error_tokens = tokenize(
        arguments->arena,
        error_source.pointer,
        error_source.length);
    ParserResult error_parser = parser_parse(
        arguments->arena,
        expression_arena,
        error_source,
        error_tokens);
    BUSTER_TEST(arguments, error_parser.diagnostic_count == 0);
    AnalysisSourceInput error_input = {
        .path = S8("semantic-errors.bbb"),
        .parser = &error_parser,
    };
    AnalysisResult error_result = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 11 },
        S8("semantic-errors"),
        &error_input,
        1);
    analysis_resolve_module_interfaces(arguments->arena, &error_result);
    analysis_analyze_bodies(arguments->arena, &error_result);
    BUSTER_TEST(arguments, error_result.diagnostic_count == 5);
    AnalysisDiagnosticKind expected_diagnostics[] = {
        ANALYSIS_DIAGNOSTIC_DUPLICATE_LOCAL,
        ANALYSIS_DIAGNOSTIC_UNKNOWN_IDENTIFIER,
        ANALYSIS_DIAGNOSTIC_EXPECTED_PLACE,
        ANALYSIS_DIAGNOSTIC_INVALID_CONTROL_FLOW,
        ANALYSIS_DIAGNOSTIC_UNREACHABLE_STATEMENT,
    };
    AnalysisDiagnostic* diagnostic = error_result.first_diagnostic;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected_diagnostics); index += 1)
    {
        BUSTER_TEST(arguments, diagnostic != 0);
        BUSTER_TEST(arguments, diagnostic->kind == expected_diagnostics[index]);
        diagnostic = diagnostic->next;
    }
    BUSTER_TEST(arguments, diagnostic == 0);
    BUSTER_CHECK(arena_destroy(expression_arena, 1));

    Arena* fixture_expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(fixture_expression_arena);
    String8 rule_source = S8(
        "type Duplicate = struct { value: s32, value: u32, }\n"
        "type Pair = struct { a: s32, b: s32, }\n"
        "type Choice = enum { a, a, b = 1 + 2, }\n"
        "code rules : fn (same: s32, same: s32) s32\n"
        "{\n"
        "    data small: u8 = 300;\n"
        "    data pair: Pair = { .a = 1, .a = 2 };\n"
        "    data choice: Choice = .a;\n"
        "    switch (choice)\n"
        "    {\n"
        "        .a => {},\n"
        "        .a => {},\n"
        "    }\n"
        "}\n");
    TokenizerResult rule_tokenizer = tokenize(
        arguments->arena,
        rule_source.pointer,
        rule_source.length);
    ParserResult rule_parser = parser_parse(
        arguments->arena,
        fixture_expression_arena,
        rule_source,
        rule_tokenizer);
    BUSTER_TEST(arguments, rule_parser.diagnostic_count == 0);
    AnalysisSourceInput rule_input = { .path = S8("semantic-rules.bbb"), .parser = &rule_parser };
    AnalysisResult rule_result = analysis_index_module(
        arguments->arena,
        (AnalysisModuleId){ .value = 12 },
        S8("semantic-rules"),
        &rule_input,
        1);
    analysis_resolve_module_interfaces(arguments->arena, &rule_result);
    analysis_analyze_bodies(arguments->arena, &rule_result);
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_FIELD));
    BUSTER_TEST(
        arguments,
        analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_ENUM_MEMBER));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_LOCAL));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT));
    BUSTER_TEST(
        arguments,
        analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_AGGREGATE_FIELD));
    BUSTER_TEST(
        arguments,
        analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_MISSING_AGGREGATE_FIELD));
    BUSTER_TEST(
        arguments,
        analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_SWITCH_CASE));
    BUSTER_TEST(
        arguments,
        analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_NONEXHAUSTIVE_SWITCH));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_MISSING_RETURN));

    for (u32 fixture_index = 0;
        fixture_index < BUSTER_ARRAY_LENGTH(analysis_fixture_tests);
        fixture_index += 1)
    {
        TemporalArena fixture_temporary = arena_begin_temporal(arguments->arena);
        AnalysisFixtureTest fixture = analysis_fixture_tests[fixture_index];
        String8 source = BYTE_SLICE_TO_STRING(
            8,
            file_read(arguments->arena, fixture.path, (FileReadOptions){0}));
        BUSTER_TEST(arguments, source.pointer != 0);
        TokenizerResult tokenizer = tokenize(arguments->arena, source.pointer, source.length);
        ParserResult parser = parser_parse(
            arguments->arena,
            fixture_expression_arena,
            source,
            tokenizer);
        BUSTER_TEST(arguments, tokenizer.error_count == 0);
        BUSTER_TEST(arguments, parser.diagnostic_count == 0);
        AnalysisSourceInput input = { .path = fixture.path, .parser = &parser };
        AnalysisResult fixture_result = analysis_index_module(
            arguments->arena,
            (AnalysisModuleId){ .value = 100 + fixture_index },
            S8("fixture"),
            &input,
            1);
        analysis_resolve_module_interfaces(arguments->arena, &fixture_result);
        analysis_analyze_bodies(arguments->arena, &fixture_result);
        analysis_compute_layouts(
            &fixture_result,
            (AnalysisLayoutOptions){ .pointer_size = 8, .pointer_alignment = 8 });
        analysis_build_jobs(arguments->arena, &fixture_result);
        BUSTER_TEST(
            arguments,
            fixture_result.diagnostic_count == fixture.expected_diagnostic_count);
        for (u32 entity_index = 0;
            entity_index < fixture_result.module.entity_count;
            entity_index += 1)
        {
            AnalysisEntity* fixture_entity = fixture_result.module.entities + entity_index;
            if (fixture_entity->kind == ANALYSIS_ENTITY_CODE && fixture_entity->ast.code->has_body)
            {
                BUSTER_TEST(arguments, fixture_result.module.bodies[entity_index].analyzed);
            }
        }
        arena_set_position(fixture_temporary.arena, fixture_temporary.position);
    }
    BUSTER_CHECK(arena_destroy(fixture_expression_arena, 1));

    arena_set_position(temporary.arena, temporary.position);
    return result;
}
#endif
