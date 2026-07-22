#include <buster/compiler/frontend/buster/analysis.h>

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
    for (u32 index = 0; index < result->module.entity_count; index += 1)
    {
        AnalysisEntity* entity = result->module.entities + index;
        if (entity->kind == ANALYSIS_ENTITY_CODE)
        {
            analysis_ast_type_link_push(scratch_arena, &top, entity->ast.code->type);
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

void analysis_resolve_module_interfaces(Arena* result_arena, AnalysisResult* result)
{
    BUSTER_CHECK(result->types.types == 0);
    Arena* conflicts[] = { result_arena };
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    u32 ast_type_count = analysis_ast_type_count(scratch.arena, result);
    u32 builtin_count = 13;
    result->types.capacity = builtin_count + result->module.type_count + ast_type_count;
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
    scratch_end(scratch);
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL ParserSourceRange analysis_test_range(u32 offset)
{
    return (ParserSourceRange){ .offset = offset, .length = 1, .line = 1, .column = offset + 1 };
}

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

    arena_set_position(temporary.arena, temporary.position);
    return result;
}
#endif
