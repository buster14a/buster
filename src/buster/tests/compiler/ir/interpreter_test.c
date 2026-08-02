#include <buster/tests/compiler/ir/interpreter_test.h>

BUSTER_GLOBAL_LOCAL AnalysisEntity* ir_interpreter_test_entity_find(AnalysisResult* analysis, String8 name)
{
    for (u32 entity_index = 0; entity_index < analysis->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = analysis->module.entities + entity_index;
        if (entity->kind == ANALYSIS_ENTITY_CODE && string_equal(entity->name, name))
        {
            return entity;
        }
    }
    return 0;
}

BUSTER_TEST_F_DECL UnitTestResult ir_interpreter_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);

    String8 scalar_source = S8("type Pair = struct\n"
                               "{\n"
                               "    a: s32,\n"
                               "    b: s32,\n"
                               "}\n"
                               "type Number = union\n"
                               "{\n"
                               "    signed_value: s32,\n"
                               "    unsigned_value: u32,\n"
                               "}\n"
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
                               "    return value;\n"
                               "}\n"
                               "code divide : fn (value: s32) s32\n"
                               "{\n"
                               "    return 12 / value;\n"
                               "}\n"
                               "code float_value : fn () f64\n"
                               "{\n"
                               "    return 1.5 * 2.0;\n"
                               "}\n"
                               "code conversion_value : fn () s32\n"
                               "{\n"
                               "    data small: s8 = -2;\n"
                               "    data widened: s32 = @cast(small);\n"
                               "    data floating: f64 = @cast(widened);\n"
                               "    return @cast(floating);\n"
                               "}\n"
                               "code forever : fn () void\n"
                               "{\n"
                               "    loop\n"
                               "    {\n"
                               "    }\n"
                               "}\n"
                               "code memory : fn () s32\n"
                               "{\n"
                               "    data values: [3]s32 = [ 2, 3, 4 ];\n"
                               "    data pair: Pair = { .a = 5, .b = 7 };\n"
                               "    data pointer: &s32 = &values[1];\n"
                               "    data same_pointer: &s32 = &values[1];\n"
                               "    pointer.& += pair.a;\n"
                               "    data selected: []s32 = values[1..];\n"
                               "    selected[1] += 1;\n"
                               "    data total: s32 = 0;\n"
                               "    for (data value = @reverse(selected))\n"
                               "    {\n"
                               "        total += value;\n"
                               "    }\n"
                               "    if (pointer == same_pointer)\n"
                               "    {\n"
                               "        total += 1;\n"
                               "    }\n"
                               "    return total + pair.b;\n"
                               "}\n"
                               "code range_total : fn () s32\n"
                               "{\n"
                               "    data total: s32 = 0;\n"
                               "    for (data value = 0 .. 4)\n"
                               "    {\n"
                               "        total += value;\n"
                               "    }\n"
                               "    for (data value = @reverse(0 .. 4))\n"
                               "    {\n"
                               "        total += value;\n"
                               "    }\n"
                               "    return total;\n"
                               "}\n"
                               "code vector_value : fn () s32\n"
                               "{\n"
                               "    data left: vector[4]s32 = [ 1, 5, -3, 8 ];\n"
                               "    data right: vector[4]s32 = [ 2, 4, -3, 9 ];\n"
                               "    data sum: vector[4]s32 = left + right;\n"
                               "    data negated: vector[4]s32 = -sum;\n"
                               "    data mask: vector[4]u32 = left < right;\n"
                               "    return negated[1] + @cast(mask[0]);\n"
                               "}\n"
                               "code array_total : fn () s32\n"
                               "{\n"
                               "    data values: [3]s32 = [ 1, 2, 3 ];\n"
                               "    data total: s32 = 0;\n"
                               "    for (data value = values)\n"
                               "    {\n"
                               "        total += value;\n"
                               "    }\n"
                               "    return total;\n"
                               "}\n"
                               "code aggregate_copy : fn () s32\n"
                               "{\n"
                               "    data first: Pair = { .a = 5, .b = 7 };\n"
                               "    data second: Pair = first;\n"
                               "    second.a += 1;\n"
                               "    return first.a * 10 + second.a;\n"
                               "}\n"
                               "code pointer_storage : fn () s32\n"
                               "{\n"
                               "    data value: s32 = 4;\n"
                               "    data pointer: &s32 = &value;\n"
                               "    data pointer_pointer: &&s32 = &pointer;\n"
                               "    pointer_pointer.&.& += 3;\n"
                               "    return value;\n"
                               "}\n"
                               "code union_value : fn () s32\n"
                               "{\n"
                               "    data number: Number = { .signed_value = 9 };\n"
                               "    return number.signed_value;\n"
                               "}\n"
                               "code string_value : fn () s32\n"
                               "{\n"
                               "    data text = \"hello\";\n"
                               "    return @cast(text[1] - 'e');\n"
                               "}\n"
                               "code indexed : fn (index: s32) s32\n"
                               "{\n"
                               "    data values: [1]s32 = [ 9 ];\n"
                               "    return values[index];\n"
                               "}\n"
                               "code variadic_first_two : fn (first: s32, ...) s32\n"
                               "{\n"
                               "    data arguments = @va_start();\n"
                               "    data copy = @va_copy(&arguments);\n"
                               "    data second: s32 = @va_arg(&copy, s32);\n"
                               "    @va_end(&copy);\n"
                               "    @va_end(&arguments);\n"
                               "    return first + second;\n"
                               "}\n"
                               "code variadic_main : fn () s32\n"
                               "{\n"
                               "    return variadic_first_two(20, 22, 99);\n"
                               "}\n"
                               "code main : fn () s32\n"
                               "{\n"
                               "    return choose(3, 4) * 2;\n"
                               "}\n"
                               "code aggregate_return : fn () Pair\n"
                               "{\n"
                               "    data pair: Pair = { .a = 11, .b = 13 };\n"
                               "    return pair;\n"
                               "}\n");
    TokenizerResult scalar_tokens = tokenize(arguments->arena, scalar_source.pointer, scalar_source.length);
    ParserResult scalar_parser = parser_parse(arguments->arena, expression_arena, scalar_source, scalar_tokens);
    BUSTER_TEST(arguments, scalar_tokens.error_count == 0);
    BUSTER_TEST(arguments, scalar_parser.diagnostic_count == 0);
    AnalysisSourceInput scalar_input = {
        .path = S8("interpreter-scalar.bbb"),
        .parser = &scalar_parser,
    };
    AnalysisResult scalar_analysis = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 700}, S8("interpreter-scalar"), &scalar_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &scalar_analysis);
    analysis_analyze_bodies(arguments->arena, &scalar_analysis);
    analysis_compute_layouts(&scalar_analysis, (AnalysisLayoutOptions){
                                                   .pointer_size = 8,
                                                   .pointer_alignment = 8,
                                               });
    AnalysisResult* scalar_modules[] = {&scalar_analysis};
    AnalysisProgram scalar_program_analysis = {
        .module_results = scalar_modules,
        .module_count = BUSTER_ARRAY_LENGTH(scalar_modules),
    };
    IrProgram scalar_program = ir_generate_program(arguments->arena, &scalar_program_analysis);
    BUSTER_TEST(arguments, scalar_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, scalar_program.rejected_function_count == 0);
    IrValidationResult scalar_validation = ir_validate_module(&scalar_analysis, scalar_program.modules);
    BUSTER_TEST(arguments, scalar_validation.error == IR_VALIDATION_NONE);

    AnalysisEntity* main_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("main"));
    BUSTER_TEST(arguments, main_entity != 0);
    if (main_entity)
    {
        IrExecutionTarget main_target =
            ir_interpreter_function_find(&scalar_program_analysis, &scalar_program, main_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID);
        IrInstruction* indirect_test_call = 0;
        if (main_target.function)
        {
            for (u32 instruction_index = 0; instruction_index < main_target.function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = main_target.function->instructions + instruction_index;
                if (instruction->opcode == IR_OPCODE_CALL && instruction->operand_count)
                {
                    indirect_test_call = instruction;
                    break;
                }
            }
        }
        BUSTER_TEST(arguments, indirect_test_call != 0);
        if (indirect_test_call)
        {
            indirect_test_call->entity = ANALYSIS_ENTITY_ID_INVALID;
            indirect_test_call->instantiation = ANALYSIS_INSTANTIATION_ID_INVALID;
        }
        IrExecutionResult executed = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, main_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID,
                                                0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, executed.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, executed.has_value);
        BUSTER_TEST(arguments, executed.bits == 14);

        IrExecutionResult depth_limited =
            ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, main_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0,
                       (IrExecutionOptions){
                           .max_call_depth = 1,
                       });
        BUSTER_TEST(arguments, depth_limited.trap == IR_EXECUTION_TRAP_CALL_DEPTH_LIMIT);
        if (indirect_test_call)
        {
            u32 saved_operand_count = indirect_test_call->operand_count;
            indirect_test_call->operand_count = 0;
            IrExecutionResult malformed =
                ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, main_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0,
                           (IrExecutionOptions){0});
            BUSTER_TEST(arguments, malformed.trap == IR_EXECUTION_TRAP_INVALID_PROGRAM);
            indirect_test_call->operand_count = saved_operand_count;
        }
    }
    AnalysisEntity* aggregate_return_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("aggregate_return"));
    BUSTER_TEST(arguments, aggregate_return_entity != 0);
    if (aggregate_return_entity)
    {
        IrExecutionResult aggregate_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, aggregate_return_entity->id,
                                                         ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, aggregate_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, aggregate_result.value.kind == IR_EXECUTION_VALUE_AGGREGATE);
        if (aggregate_result.value.bytes.pointer && aggregate_result.value.bytes.length == 8)
        {
            s32 fields[2] = {0};
            memcpy(fields, aggregate_result.value.bytes.pointer, sizeof(fields));
            BUSTER_TEST(arguments, fields[0] == 11 && fields[1] == 13);
        }
        else
        {
            BUSTER_TEST(arguments, false);
        }
    }
    AnalysisEntity* variadic_main_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("variadic_main"));
    BUSTER_TEST(arguments, variadic_main_entity != 0);
    if (variadic_main_entity)
    {
        IrExecutionResult executed = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, variadic_main_entity->id,
                                                ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, executed.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, executed.has_value);
        BUSTER_TEST(arguments, executed.bits == 42);
    }

    AnalysisEntity* choose_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("choose"));
    BUSTER_TEST(arguments, choose_entity != 0);
    if (choose_entity)
    {
        IrExecutionArgument choose_less_arguments[] = {
            {.bits = 3},
            {.bits = 4},
        };
        IrExecutionResult chose_less =
            ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, choose_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID, choose_less_arguments,
                       BUSTER_ARRAY_LENGTH(choose_less_arguments), (IrExecutionOptions){0});
        BUSTER_TEST(arguments, chose_less.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, chose_less.bits == 7);

        IrExecutionArgument choose_greater_arguments[] = {
            {.bits = 8},
            {.bits = 3},
        };
        IrExecutionResult chose_greater =
            ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, choose_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID,
                       choose_greater_arguments, BUSTER_ARRAY_LENGTH(choose_greater_arguments), (IrExecutionOptions){0});
        BUSTER_TEST(arguments, chose_greater.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, chose_greater.bits == 5);
    }

    AnalysisEntity* divide_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("divide"));
    BUSTER_TEST(arguments, divide_entity != 0);
    if (divide_entity)
    {
        IrExecutionArgument zero = {0};
        IrExecutionResult divided_by_zero = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, divide_entity->id,
                                                       ANALYSIS_INSTANTIATION_ID_INVALID, &zero, 1, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, divided_by_zero.trap == IR_EXECUTION_TRAP_DIVISION_BY_ZERO);
    }

    AnalysisEntity* float_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("float_value"));
    BUSTER_TEST(arguments, float_entity != 0);
    if (float_entity)
    {
        IrExecutionResult float_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, float_entity->id,
                                                    ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, float_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, ir_interpreter_float_read(float_result.bits, 64) == 3.0);
    }

    AnalysisEntity* conversion_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("conversion_value"));
    BUSTER_TEST(arguments, conversion_entity != 0);
    if (conversion_entity)
    {
        IrExecutionResult conversion_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, conversion_entity->id,
                                                         ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, conversion_result.trap == IR_EXECUTION_TRAP_NONE);
        if (conversion_result.bits != (u64)UINT32_MAX - 1)
        {
            BUSTER_TEST_ERROR(S8("conversion_value returned {u64}, expected {u64}"), conversion_result.bits, (u64)UINT32_MAX - 1);
        }
        BUSTER_TEST(arguments, conversion_result.bits == (u64)UINT32_MAX - 1);
    }

    AnalysisEntity* forever_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("forever"));
    BUSTER_TEST(arguments, forever_entity != 0);
    if (forever_entity)
    {
        IrExecutionResult step_limited =
            ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, forever_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0,
                       (IrExecutionOptions){
                           .max_steps = 32,
                       });
        BUSTER_TEST(arguments, step_limited.trap == IR_EXECUTION_TRAP_STEP_LIMIT);
        BUSTER_TEST(arguments, step_limited.step_count == 32);
    }

    AnalysisEntity* memory_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("memory"));
    BUSTER_TEST(arguments, memory_entity != 0);
    if (memory_entity)
    {
        IrExecutionResult memory_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, memory_entity->id,
                                                     ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, memory_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, memory_result.bits == 21);
    }

    AnalysisEntity* range_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("range_total"));
    BUSTER_TEST(arguments, range_entity != 0);
    if (range_entity)
    {
        IrExecutionResult range_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, range_entity->id,
                                                    ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, range_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, range_result.bits == 12);
    }

    AnalysisEntity* array_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("array_total"));
    BUSTER_TEST(arguments, array_entity != 0);
    if (array_entity)
    {
        IrExecutionResult array_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, array_entity->id,
                                                    ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, array_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, array_result.bits == 6);
    }

    AnalysisEntity* vector_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("vector_value"));
    BUSTER_TEST(arguments, vector_entity != 0);
    if (vector_entity)
    {
        IrExecutionResult vector_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, vector_entity->id,
                                                     ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, vector_result.trap == IR_EXECUTION_TRAP_NONE);
        if (vector_result.bits != (u64)(u32)-10)
        {
            BUSTER_TEST_ERROR(S8("vector_value returned {u64}, expected {u64}"), vector_result.bits, (u64)(u32)-10);
        }
        BUSTER_TEST(arguments, vector_result.bits == (u64)(u32)-10);
    }

    AnalysisEntity* aggregate_copy_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("aggregate_copy"));
    BUSTER_TEST(arguments, aggregate_copy_entity != 0);
    if (aggregate_copy_entity)
    {
        IrExecutionResult aggregate_copy_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, aggregate_copy_entity->id,
                                                             ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, aggregate_copy_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, aggregate_copy_result.bits == 56);
    }

    AnalysisEntity* pointer_storage_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("pointer_storage"));
    BUSTER_TEST(arguments, pointer_storage_entity != 0);
    if (pointer_storage_entity)
    {
        IrExecutionResult pointer_storage_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, pointer_storage_entity->id,
                                                              ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, pointer_storage_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, pointer_storage_result.bits == 7);
    }

    AnalysisEntity* union_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("union_value"));
    BUSTER_TEST(arguments, union_entity != 0);
    if (union_entity)
    {
        IrExecutionResult union_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, union_entity->id,
                                                    ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, union_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, union_result.bits == 9);
    }

    AnalysisEntity* string_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("string_value"));
    BUSTER_TEST(arguments, string_entity != 0);
    if (string_entity)
    {
        IrExecutionResult string_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, string_entity->id,
                                                     ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, string_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, string_result.bits == 0);
    }

    AnalysisEntity* indexed_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("indexed"));
    BUSTER_TEST(arguments, indexed_entity != 0);
    if (indexed_entity)
    {
        IrExecutionArgument outside = {.bits = 2};
        IrExecutionResult bounds_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, indexed_entity->id,
                                                     ANALYSIS_INSTANTIATION_ID_INVALID, &outside, 1, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, bounds_result.trap == IR_EXECUTION_TRAP_OUT_OF_BOUNDS);
    }

    String8 math_source = S8("code identity : fn (value: $T) $T\n"
                             "{\n"
                             "    return value;\n"
                             "}\n");
    String8 app_source = S8("import math = \"core/math\";\n"
                            "code main : fn () s32\n"
                            "{\n"
                            "    return math.identity(40) + 2;\n"
                            "}\n");
    TokenizerResult math_tokens = tokenize(arguments->arena, math_source.pointer, math_source.length);
    ParserResult math_parser = parser_parse(arguments->arena, expression_arena, math_source, math_tokens);
    TokenizerResult app_tokens = tokenize(arguments->arena, app_source.pointer, app_source.length);
    ParserResult app_parser = parser_parse(arguments->arena, expression_arena, app_source, app_tokens);
    BUSTER_TEST(arguments, math_tokens.error_count == 0);
    BUSTER_TEST(arguments, math_parser.diagnostic_count == 0);
    BUSTER_TEST(arguments, app_tokens.error_count == 0);
    BUSTER_TEST(arguments, app_parser.diagnostic_count == 0);
    AnalysisSourceInput math_input = {
        .path = S8("math.bbb"),
        .parser = &math_parser,
    };
    AnalysisSourceInput app_input = {
        .path = S8("app.bbb"),
        .parser = &app_parser,
    };
    AnalysisResult math_analysis = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 710}, S8("core/math"), &math_input, 1);
    AnalysisResult app_analysis = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 711}, S8("app"), &app_input, 1);
    AnalysisResult* cross_modules[] = {
        &app_analysis,
        &math_analysis,
    };
    analysis_resolve_program_interfaces(arguments->arena, cross_modules, BUSTER_ARRAY_LENGTH(cross_modules));
    analysis_analyze_bodies(arguments->arena, &app_analysis);
    analysis_analyze_bodies(arguments->arena, &math_analysis);
    AnalysisProgram cross_analysis = {
        .module_results = cross_modules,
        .module_count = BUSTER_ARRAY_LENGTH(cross_modules),
    };
    IrProgram cross_program = ir_generate_program(arguments->arena, &cross_analysis);
    BUSTER_TEST(arguments, app_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, math_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, math_analysis.instantiation_count == 1);
    AnalysisEntity* cross_main = ir_interpreter_test_entity_find(&app_analysis, S8("main"));
    BUSTER_TEST(arguments, cross_main != 0);
    if (cross_main)
    {
        IrExecutionResult cross_result =
            ir_execute(expression_arena, &cross_analysis, &cross_program, cross_main->id, ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, cross_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, cross_result.has_value);
        BUSTER_TEST(arguments, cross_result.bits == 42);
    }

    BUSTER_CHECK(arena_destroy(expression_arena, 1));
    arena_set_position(temporary.arena, temporary.position);
    return result;
}
