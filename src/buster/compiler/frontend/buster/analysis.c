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
    if (left.source != right.source)
    {
        return left.source > right.source;
    }
    if (left.range.offset != right.range.offset)
    {
        return left.range.offset > right.range.offset;
    }
    return left.kind > right.kind;
}

BUSTER_GLOBAL_LOCAL void analysis_diagnostic_push(
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
    };
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
            .id = ANALYSIS_INVALID_ID,
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
    u32 entity_index = 0;
    for (u32 source_index = 0; source_index < input_count; source_index += 1)
    {
        AnalysisSource* source = result.module.sources + source_index;
        source->id = source_index;
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
        entity->id = (AnalysisEntityId){ .module = module_id, .index = index };
        for (u32 previous_index = 0; previous_index < index; previous_index += 1)
        {
            AnalysisEntity* previous = result.module.entities + previous_index;
            if (entity->name_space == previous->name_space && string_equal(entity->name, previous->name))
            {
                analysis_diagnostic_push(result_arena, &result, entity, previous);
                break;
            }
        }
    }

    return result;
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
        arguments->arena, 7, S8("example"), reversed_inputs, BUSTER_ARRAY_LENGTH(reversed_inputs));

    BUSTER_TEST(arguments, indexed.module.source_count == 2);
    BUSTER_STRING_TEST(arguments, indexed.module.sources[0].path, S8("alpha.bbb"));
    BUSTER_STRING_TEST(arguments, indexed.module.sources[1].path, S8("zeta.bbb"));
    BUSTER_TEST(arguments, indexed.module.sources[0].id == 0);
    BUSTER_TEST(arguments, indexed.module.sources[1].id == 1);
    BUSTER_TEST(arguments, indexed.module.entity_count == 4);
    BUSTER_STRING_TEST(arguments, indexed.module.entities[0].name, S8("duplicate"));
    BUSTER_STRING_TEST(arguments, indexed.module.entities[1].name, S8("Shared"));
    BUSTER_STRING_TEST(arguments, indexed.module.entities[2].name, S8("Shared"));
    BUSTER_STRING_TEST(arguments, indexed.module.entities[3].name, S8("duplicate"));
    BUSTER_TEST(arguments, indexed.module.entities[3].id.module == 7);
    BUSTER_TEST(arguments, indexed.module.entities[3].id.index == 3);

    // Type and value namespaces may use the same spelling. Only the repeated
    // value declaration is diagnosed.
    BUSTER_TEST(arguments, indexed.diagnostic_count == 1);
    BUSTER_TEST(arguments, indexed.first_diagnostic != 0);
    BUSTER_TEST(arguments, indexed.first_diagnostic->entity.index == 3);
    BUSTER_TEST(arguments, indexed.first_diagnostic->previous_entity.index == 0);

    AnalysisSourceInput forward_inputs[] = {
        { .path = S8("alpha.bbb"), .parser = &alpha_parser },
        { .path = S8("zeta.bbb"), .parser = &zeta_parser },
    };
    AnalysisResult forward = analysis_index_module(
        arguments->arena, 7, S8("example"), forward_inputs, BUSTER_ARRAY_LENGTH(forward_inputs));
    BUSTER_TEST(arguments, forward.module.entity_count == indexed.module.entity_count);
    for (u32 index = 0; index < indexed.module.entity_count; index += 1)
    {
        BUSTER_STRING_TEST(arguments, forward.module.entities[index].name, indexed.module.entities[index].name);
        BUSTER_TEST(arguments, forward.module.entities[index].id.index == indexed.module.entities[index].id.index);
        BUSTER_TEST(arguments, forward.module.entities[index].source == indexed.module.entities[index].source);
    }

    arena_set_position(temporary.arena, temporary.position);
    return result;
}
#endif
