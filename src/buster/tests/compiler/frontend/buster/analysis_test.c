#include <buster/tests/compiler/frontend/buster/analysis_test.h>

BUSTER_GLOBAL_LOCAL ParserSourceRange analysis_test_range(u32 offset)
{
    return (ParserSourceRange){.offset = offset, .length = 1, .line = 1, .column = offset + 1};
}

BUSTER_GLOBAL_LOCAL bool analysis_test_has_diagnostic(AnalysisResult* result, AnalysisDiagnosticKind kind)
{
    for (AnalysisDiagnostic* diagnostic = result->first_diagnostic; diagnostic; diagnostic = diagnostic->next)
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

BUSTER_GLOBAL_LOCAL AnalysisFixtureTest analysis_fixture_tests[] = {
    {S8_INITIALIZER("tests/basic_vector.bbb"), 0},
    {S8_INITIALIZER("tests/basic_vector_error.bbb"), 1},
    {S8_INITIALIZER("tests/basic_code_non_function_type_error.bbb"), 2},
    {S8_INITIALIZER("tests/basic_return_without_value_error.bbb"), 1},
    {S8_INITIALIZER("tests/basic_variadic.bbb"), 0},
    {S8_INITIALIZER("tests/basic_variadic_error.bbb"), 1},
    {S8_INITIALIZER("tests/array_slices.bbb"), 0},
    {S8_INITIALIZER("tests/basic_array_literal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_assignment.bbb"), 0},
    {S8_INITIALIZER("tests/basic_binary_literal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_bitwise_not.bbb"), 0},
    {S8_INITIALIZER("tests/basic_boolean_operators.bbb"), 1},
    {S8_INITIALIZER("tests/basic_break.bbb"), 0},
    {S8_INITIALIZER("tests/basic_character_literal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_comment.bbb"), 0},
    {S8_INITIALIZER("tests/basic_compile_time.bbb"), 0},
    {S8_INITIALIZER("tests/compile_time_argument_error.bbb"), 1},
    {S8_INITIALIZER("tests/modules/core/math.bbb"), 0},
    {S8_INITIALIZER("tests/modules/system/platform.bbb"), 0},
    {S8_INITIALIZER("tests/basic_continue.bbb"), 0},
    {S8_INITIALIZER("tests/basic_else_if.bbb"), 0},
    {S8_INITIALIZER("tests/basic_enum.bbb"), 0},
    {S8_INITIALIZER("tests/basic_float.bbb"), 0},
    {S8_INITIALIZER("tests/basic_for.bbb"), 0},
    {S8_INITIALIZER("tests/basic_function_call.bbb"), 1},
    {S8_INITIALIZER("tests/basic_hexadecimal_literal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_if_else.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_add.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_and.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_compare.bbb"), 6},
    {S8_INITIALIZER("tests/basic_integer_literal_divide.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_mod.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_multiply.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_or.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_precedence.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_shift_left.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_shift_right.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_sub.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_xor.bbb"), 0},
    {S8_INITIALIZER("tests/basic_logical_not.bbb"), 2},
    {S8_INITIALIZER("tests/basic_loop.bbb"), 1},
    {S8_INITIALIZER("tests/basic_import.bbb"), 0},
    {S8_INITIALIZER("tests/basic_minimal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_octal_literal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_pointer.bbb"), 0},
    {S8_INITIALIZER("tests/basic_string_literal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_struct.bbb"), 0},
    {S8_INITIALIZER("tests/basic_switch.bbb"), 0},
    {S8_INITIALIZER("tests/basic_type_alias.bbb"), 0},
    {S8_INITIALIZER("tests/basic_unary_minus.bbb"), 0},
    {S8_INITIALIZER("tests/basic_unary_plus.bbb"), 0},
    {S8_INITIALIZER("tests/basic_union.bbb"), 0},
    {S8_INITIALIZER("tests/basic_variable.bbb"), 0},
};

BUSTER_TEST_F_DECL UnitTestResult analysis_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);

    AstTypeDeclaration alpha_type = {
        .name = {.text = S8("Shared"), .range = {0}},
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
        {.path = S8("zeta.bbb"), .parser = &zeta_parser},
        {.path = S8("alpha.bbb"), .parser = &alpha_parser},
    };
    AnalysisResult indexed =
        analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 7}, S8("example"), reversed_inputs, BUSTER_ARRAY_LENGTH(reversed_inputs));

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
        {.path = S8("alpha.bbb"), .parser = &alpha_parser},
        {.path = S8("zeta.bbb"), .parser = &zeta_parser},
    };
    AnalysisResult forward =
        analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 7}, S8("example"), forward_inputs, BUSTER_ARRAY_LENGTH(forward_inputs));
    BUSTER_TEST(arguments, forward.module.entity_count == indexed.module.entity_count);
    for (u32 index = 0; index < indexed.module.entity_count; index += 1)
    {
        BUSTER_STRING_TEST(arguments, forward.module.entities[index].name, indexed.module.entities[index].name);
        BUSTER_TEST(arguments, forward.module.entities[index].id.index.value == indexed.module.entities[index].id.index.value);
        BUSTER_TEST(arguments, forward.module.entities[index].source.value == indexed.module.entities[index].source.value);
    }
    String8 indexed_summary = analysis_serialize_module_interface(arguments->arena, &indexed);
    String8 forward_summary = analysis_serialize_module_interface(arguments->arena, &forward);
    BUSTER_STRING_TEST(arguments, indexed_summary, forward_summary);

    AstImport app_missing_import = {
        .name_space = {.text = S8("missing"), .range = analysis_test_range(10)},
        .path = S8("does/not/exist"),
        .range = analysis_test_range(10),
        .path_range = analysis_test_range(20),
    };
    AstImport app_math_import = {
        .next = &app_missing_import,
        .name_space = {.text = S8("math"), .range = analysis_test_range(0)},
        .path = S8("core/math"),
        .range = analysis_test_range(0),
        .path_range = analysis_test_range(5),
    };
    ParserResult app_parser = {
        .first_import = &app_math_import,
        .last_import = &app_missing_import,
        .import_count = 2,
    };
    AstCode sum_code = {
        .name = S8("sum"),
        .range = analysis_test_range(0),
    };
    ParserResult math_parser = {
        .first_code = &sum_code,
        .last_code = &sum_code,
        .code_count = 1,
    };
    AstImport a_import = {
        .name_space = {.text = S8("b"), .range = analysis_test_range(0)},
        .path = S8("cycle/b"),
        .range = analysis_test_range(0),
    };
    AstImport b_import = {
        .name_space = {.text = S8("a"), .range = analysis_test_range(0)},
        .path = S8("cycle/a"),
        .range = analysis_test_range(0),
    };
    ParserResult a_parser = {
        .first_import = &a_import,
        .last_import = &a_import,
        .import_count = 1,
    };
    ParserResult b_parser = {
        .first_import = &b_import,
        .last_import = &b_import,
        .import_count = 1,
    };
    AnalysisSourceInput app_input = {.path = S8("app.bbb"), .parser = &app_parser};
    AnalysisSourceInput math_input = {.path = S8("math.bbb"), .parser = &math_parser};
    AnalysisSourceInput a_input = {.path = S8("a.bbb"), .parser = &a_parser};
    AnalysisSourceInput b_input = {.path = S8("b.bbb"), .parser = &b_parser};
    AnalysisResult app_module = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 20}, S8("app"), &app_input, 1);
    AnalysisResult math_module = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 21}, S8("core/math"), &math_input, 1);
    AnalysisResult a_module = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 22}, S8("cycle/a"), &a_input, 1);
    AnalysisResult b_module = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 23}, S8("cycle/b"), &b_input, 1);
    AnalysisResult* import_modules[] = {
        &app_module,
        &math_module,
        &a_module,
        &b_module,
    };
    analysis_resolve_imports(arguments->arena, import_modules, BUSTER_ARRAY_LENGTH(import_modules));
    BUSTER_TEST(arguments, app_module.module.import_count == 2);
    BUSTER_TEST(arguments, app_module.module.imports[0].state == ANALYSIS_IMPORT_RESOLVED);
    BUSTER_TEST(arguments, app_module.module.imports[0].target == &math_module);
    BUSTER_TEST(arguments, app_module.module.imports[1].state == ANALYSIS_IMPORT_MISSING);
    BUSTER_TEST(arguments, app_module.diagnostic_count == 1);
    BUSTER_TEST(arguments, app_module.first_diagnostic != 0 && app_module.first_diagnostic->kind == ANALYSIS_DIAGNOSTIC_MISSING_IMPORTED_MODULE);
    AnalysisEntity* qualified_sum = analysis_find_qualified_entity(&app_module, S8("math"), S8("sum"), ANALYSIS_NAMESPACE_VALUE);
    BUSTER_TEST(arguments, qualified_sum == math_module.module.entities);
    BUSTER_TEST(arguments, b_module.module.imports[0].state == ANALYSIS_IMPORT_CYCLE);
    BUSTER_TEST(arguments, b_module.diagnostic_count == 1);
    BUSTER_TEST(arguments, b_module.first_diagnostic != 0 && b_module.first_diagnostic->kind == ANALYSIS_DIAGNOSTIC_IMPORT_CYCLE);

    AstImport duplicate_second = {
        .name_space = {.text = S8("same"), .range = analysis_test_range(10)},
        .path = S8("two"),
        .range = analysis_test_range(10),
    };
    AstImport duplicate_first = {
        .next = &duplicate_second,
        .name_space = {.text = S8("same"), .range = analysis_test_range(0)},
        .path = S8("one"),
        .range = analysis_test_range(0),
    };
    ParserResult duplicate_import_parser = {
        .first_import = &duplicate_first,
        .last_import = &duplicate_second,
        .import_count = 2,
    };
    AnalysisSourceInput duplicate_import_input = {
        .path = S8("duplicate-import.bbb"),
        .parser = &duplicate_import_parser,
    };
    AnalysisResult duplicate_import_module =
        analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 24}, S8("duplicate-import"), &duplicate_import_input, 1);
    BUSTER_TEST(arguments, duplicate_import_module.diagnostic_count == 1);
    BUSTER_TEST(arguments, duplicate_import_module.first_diagnostic != 0 &&
                               duplicate_import_module.first_diagnostic->kind == ANALYSIS_DIAGNOSTIC_DUPLICATE_IMPORT_NAMESPACE);
    if (duplicate_import_module.first_diagnostic)
    {
        BUSTER_TEST(arguments, duplicate_import_module.first_diagnostic->first_note != 0);
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
        .name = {.text = S8("A")},
        .range = analysis_test_range(0),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeDeclaration alias_b = {
        .alias_type = &named_s32,
        .name = {.text = S8("B")},
        .range = analysis_test_range(10),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeDeclaration alias_pointer = {
        .alias_type = &pointer_a,
        .name = {.text = S8("Pointer")},
        .range = analysis_test_range(20),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeField node_next = {
        .name = {.text = S8("next")},
        .type = &pointer_node,
        .range = analysis_test_range(31),
    };
    AstTypeDeclaration node = {
        .first_field = &node_next,
        .last_field = &node_next,
        .name = {.text = S8("Node")},
        .range = analysis_test_range(30),
        .kind = AST_TYPE_DECLARATION_STRUCT,
        .field_count = 1,
    };
    AstTypeDeclaration unknown = {
        .alias_type = &missing,
        .name = {.text = S8("Unknown")},
        .range = analysis_test_range(40),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeDeclaration alias_c = {
        .alias_type = &named_d,
        .name = {.text = S8("C")},
        .range = analysis_test_range(50),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeDeclaration alias_d = {
        .alias_type = &named_c,
        .name = {.text = S8("D")},
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
        .function =
            {
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
    AnalysisResult resolved = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 9}, S8("types"), &resolution_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &resolved);

    BUSTER_TEST(arguments, resolved.types.count > 13);
    BUSTER_TEST(arguments, resolved.module.semantics[0].type.value == resolved.types.builtin.s32_type.value);
    BUSTER_TEST(arguments, resolved.module.semantics[1].type.value == resolved.types.builtin.s32_type.value);
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
    BUSTER_TEST(arguments, code_type->as.function.argument_types[0].value == resolved.module.semantics[2].type.value);
    BUSTER_TEST(arguments, code_type->as.function.return_type.value == resolved.types.builtin.s32_type.value);

    BUSTER_TEST(arguments, resolved.diagnostic_count == 2);
    BUSTER_TEST(arguments, resolved.first_diagnostic->kind == ANALYSIS_DIAGNOSTIC_UNKNOWN_TYPE);
    BUSTER_STRING_TEST(arguments, resolved.first_diagnostic->subject, S8("Missing"));
    BUSTER_TEST(arguments, resolved.last_diagnostic->kind == ANALYSIS_DIAGNOSTIC_TYPE_ALIAS_CYCLE);
    analysis_compute_layouts(&resolved, (AnalysisLayoutOptions){.pointer_size = 8, .pointer_alignment = 8});
    BUSTER_TEST(arguments, node_type->layout.state == ANALYSIS_LAYOUT_RESOLVED);
    BUSTER_TEST(arguments, node_type->layout.size == 8);
    BUSTER_TEST(arguments, node_type->layout.alignment == 8);
    BUSTER_TEST(arguments, resolved.module.semantics[3].fields[0].offset == 0);
    BUSTER_TEST(arguments, code_type->layout.abi_class == ANALYSIS_ABI_CLASS_POINTER);
    AnalysisFunctionAbi systemv_abi =
        analysis_classify_function_abi(arguments->arena, &resolved, code_type->id, (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX});
    BUSTER_TEST(arguments, systemv_abi.convention == ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64);
    BUSTER_TEST(arguments, systemv_abi.argument_count == 1);
    BUSTER_TEST(arguments, systemv_abi.arguments[0].parts[0].register_index == 0);
    BUSTER_TEST(arguments, systemv_abi.result.parts[0].register_index == 0);
    BUSTER_TEST(arguments, systemv_abi.stack_size == 0);
    AnalysisFunctionAbi windows_abi =
        analysis_classify_function_abi(arguments->arena, &resolved, code_type->id, (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS});
    BUSTER_TEST(arguments, windows_abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64);
    BUSTER_TEST(arguments, windows_abi.stack_size == 32);
    AnalysisFunctionAbi aapcs_abi =
        analysis_classify_function_abi(arguments->arena, &resolved, code_type->id, (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX});
    BUSTER_TEST(arguments, aapcs_abi.convention == ANALYSIS_ABI_CONVENTION_AAPCS64);
    BUSTER_TEST(arguments, aapcs_abi.arguments[0].parts[0].register_index == 0);
    AnalysisFunctionAbi windows_aarch64_abi = analysis_classify_function_abi(arguments->arena, &resolved, code_type->id,
                                                                             (Target){
                                                                                 .cpu_arch = CPU_ARCH_AARCH64,
                                                                                 .os = OPERATING_SYSTEM_WINDOWS,
                                                                             });
    BUSTER_TEST(arguments, windows_aarch64_abi.convention == ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64);
    BUSTER_TEST(arguments, windows_aarch64_abi.arguments[0].parts[0].register_index == 0);
    AnalysisFunctionAbi uefi_x86_64_abi = analysis_classify_function_abi(arguments->arena, &resolved, code_type->id,
                                                                         (Target){
                                                                             .cpu_arch = CPU_ARCH_X86_64,
                                                                             .os = OPERATING_SYSTEM_UEFI,
                                                                         });
    BUSTER_TEST(arguments, uefi_x86_64_abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64);
    analysis_build_jobs(arguments->arena, &resolved);
    BUSTER_TEST(arguments, resolved.job_count == 16);
    BUSTER_TEST(arguments, resolved.jobs[0].kind == ANALYSIS_JOB_INTERFACE);
    BUSTER_TEST(arguments, resolved.jobs[8].kind == ANALYSIS_JOB_LAYOUT);
    BUSTER_TEST(arguments, resolved.jobs[15].kind == ANALYSIS_JOB_BODY);
    BUSTER_TEST(arguments, resolved.jobs[15].dependency_count == 1);
    BUSTER_TEST(arguments, resolved.jobs[15].dependency_kinds[0] == ANALYSIS_DEPENDENCY_INTERFACE);
    AnalysisScheduleResult schedule = analysis_execute_jobs(arguments->arena, &resolved, 4, 0, 0);
    BUSTER_TEST(arguments, !schedule.has_cycle);
    BUSTER_TEST(arguments, schedule.execution_count == resolved.job_count);
    BUSTER_TEST(arguments, schedule.wave_count == 2);
    for (u32 execution_index = 0; execution_index < schedule.execution_count; execution_index += 1)
    {
        AnalysisJob* scheduled = resolved.jobs + schedule.execution_order[execution_index].value;
        for (u32 dependency_index = 0; dependency_index < scheduled->dependency_count; dependency_index += 1)
        {
            bool dependency_precedes = false;
            for (u32 previous = 0; previous < execution_index; previous += 1)
            {
                dependency_precedes |= schedule.execution_order[previous].value == scheduled->dependencies[dependency_index].value;
            }
            BUSTER_TEST(arguments, dependency_precedes);
        }
    }

    String8 body_source = S8("code add : fn (a: s32, b: s32) s32\n"
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
    TokenizerResult body_tokens = tokenize(arguments->arena, body_source.pointer, body_source.length);
    ParserResult body_parser = parser_parse(arguments->arena, expression_arena, body_source, body_tokens);
    BUSTER_TEST(arguments, body_tokens.error_count == 0);
    BUSTER_TEST(arguments, body_parser.diagnostic_count == 0);
    AnalysisSourceInput body_input = {
        .path = S8("semantic-body.bbb"),
        .parser = &body_parser,
    };
    AnalysisResult body_result = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 10}, S8("semantic-body"), &body_input, 1);
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
            BUSTER_TEST(arguments, body->first_expression->type.value == body_result.types.builtin.s32_type.value);
        }
    }

    String8 constant_source = S8("code constants : fn () f64\n"
                                 "{\n"
                                 "    data sum: f64 = 1.25 + 2.75;\n"
                                 "    data hexadecimal: f64 = 0x1.fp+2;\n"
                                 "    data integer: s32 = @cast(3.75);\n"
                                 "    return sum + hexadecimal + @cast(integer);\n"
                                 "}\n");
    TokenizerResult constant_tokens = tokenize(arguments->arena, constant_source.pointer, constant_source.length);
    ParserResult constant_parser = parser_parse(arguments->arena, expression_arena, constant_source, constant_tokens);
    BUSTER_TEST(arguments, constant_tokens.error_count == 0);
    BUSTER_TEST(arguments, constant_parser.diagnostic_count == 0);
    AnalysisSourceInput constant_input = {
        .path = S8("semantic-constants.bbb"),
        .parser = &constant_parser,
    };
    AnalysisResult constant_result = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 12}, S8("semantic-constants"), &constant_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &constant_result);
    analysis_analyze_bodies(arguments->arena, &constant_result);
    BUSTER_TEST(arguments, constant_result.diagnostic_count == 0);
    AnalysisTypedExpression* constant_expression = constant_result.module.bodies[0].first_expression;
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
        AnalysisConstant hexadecimal = constant_expression->nodes[constant_expression->ast.count - 1].constant;
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

    String8 initialization_source = S8("code initialization : fn (condition: bool) s32\n"
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
    TokenizerResult initialization_tokens = tokenize(arguments->arena, initialization_source.pointer, initialization_source.length);
    ParserResult initialization_parser = parser_parse(arguments->arena, expression_arena, initialization_source, initialization_tokens);
    BUSTER_TEST(arguments, initialization_tokens.error_count == 0);
    BUSTER_TEST(arguments, initialization_parser.diagnostic_count == 0);
    AnalysisSourceInput initialization_input = {
        .path = S8("semantic-initialization.bbb"),
        .parser = &initialization_parser,
    };
    AnalysisResult initialization_result =
        analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 13}, S8("semantic-initialization"), &initialization_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &initialization_result);
    analysis_analyze_bodies(arguments->arena, &initialization_result);
    BUSTER_TEST(arguments, initialization_result.diagnostic_count == 2);
    for (AnalysisDiagnostic* diagnostic = initialization_result.first_diagnostic; diagnostic; diagnostic = diagnostic->next)
    {
        BUSTER_TEST(arguments, diagnostic->kind == ANALYSIS_DIAGNOSTIC_USE_BEFORE_INITIALIZATION);
        BUSTER_TEST(arguments, diagnostic->first_note != 0);
        if (diagnostic->first_note)
        {
            BUSTER_STRING_TEST(arguments, diagnostic->first_note->message, S8("local is declared uninitialized here"));
        }
    }

    String8 error_source = S8("code broken : fn (value: s32) s32\n"
                              "{\n"
                              "    data value: s32 = 0;\n"
                              "    data result: s32 = missing;\n"
                              "    1 = result;\n"
                              "    break;\n"
                              "    return result;\n"
                              "}\n");
    TokenizerResult error_tokens = tokenize(arguments->arena, error_source.pointer, error_source.length);
    ParserResult error_parser = parser_parse(arguments->arena, expression_arena, error_source, error_tokens);
    BUSTER_TEST(arguments, error_parser.diagnostic_count == 0);
    AnalysisSourceInput error_input = {
        .path = S8("semantic-errors.bbb"),
        .parser = &error_parser,
    };
    AnalysisResult error_result = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 11}, S8("semantic-errors"), &error_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &error_result);
    analysis_analyze_bodies(arguments->arena, &error_result);
    BUSTER_TEST(arguments, error_result.diagnostic_count == 5);
    AnalysisDiagnosticKind expected_diagnostics[] = {
        ANALYSIS_DIAGNOSTIC_DUPLICATE_LOCAL,      ANALYSIS_DIAGNOSTIC_UNKNOWN_IDENTIFIER,    ANALYSIS_DIAGNOSTIC_EXPECTED_PLACE,
        ANALYSIS_DIAGNOSTIC_INVALID_CONTROL_FLOW, ANALYSIS_DIAGNOSTIC_UNREACHABLE_STATEMENT,
    };
    AnalysisDiagnostic* diagnostic = error_result.first_diagnostic;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected_diagnostics); index += 1)
    {
        BUSTER_TEST(arguments, diagnostic != 0);
        BUSTER_TEST(arguments, diagnostic->kind == expected_diagnostics[index]);
        diagnostic = diagnostic->next;
    }
    BUSTER_TEST(arguments, diagnostic == 0);

    String8 namespace_math_source = S8("type Vector = struct { x: s32, y: s32, }\n"
                                       "code add : fn (a: s32, b: s32) s32\n"
                                       "{\n"
                                       "    return a + b;\n"
                                       "}\n"
                                       "code identity : fn ($value: $T) $T\n"
                                       "{\n"
                                       "    return value;\n"
                                       "}\n");
    String8 namespace_app_source = S8("import math = \"core/math\";\n"
                                      "code use : fn (value: math.Vector) s32\n"
                                      "{\n"
                                      "    data copy: math.Vector = value;\n"
                                      "    return math.add(copy.x, copy.y) + math.identity(5);\n"
                                      "}\n");
    String8 namespace_app_two_source = S8("import math = \"core/math\";\n"
                                          "code use_again : fn () s32\n"
                                          "{\n"
                                          "    return math.identity(5);\n"
                                          "}\n");
    TokenizerResult namespace_math_tokens = tokenize(arguments->arena, namespace_math_source.pointer, namespace_math_source.length);
    ParserResult namespace_math_parser = parser_parse(arguments->arena, expression_arena, namespace_math_source, namespace_math_tokens);
    TokenizerResult namespace_app_tokens = tokenize(arguments->arena, namespace_app_source.pointer, namespace_app_source.length);
    ParserResult namespace_app_parser = parser_parse(arguments->arena, expression_arena, namespace_app_source, namespace_app_tokens);
    TokenizerResult namespace_app_two_tokens = tokenize(arguments->arena, namespace_app_two_source.pointer, namespace_app_two_source.length);
    ParserResult namespace_app_two_parser = parser_parse(arguments->arena, expression_arena, namespace_app_two_source, namespace_app_two_tokens);
    BUSTER_TEST(arguments, namespace_math_parser.diagnostic_count == 0);
    BUSTER_TEST(arguments, namespace_app_parser.diagnostic_count == 0);
    BUSTER_TEST(arguments, namespace_app_two_parser.diagnostic_count == 0);
    AnalysisSourceInput namespace_math_input = {
        .path = S8("math.bbb"),
        .parser = &namespace_math_parser,
    };
    AnalysisSourceInput namespace_app_input = {
        .path = S8("app.bbb"),
        .parser = &namespace_app_parser,
    };
    AnalysisSourceInput namespace_app_two_input = {
        .path = S8("app-two.bbb"),
        .parser = &namespace_app_two_parser,
    };
    AnalysisResult namespace_math = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 40}, S8("core/math"), &namespace_math_input, 1);
    AnalysisResult namespace_app = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 41}, S8("app"), &namespace_app_input, 1);
    AnalysisResult namespace_app_two = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 42}, S8("app-two"), &namespace_app_two_input, 1);
    AnalysisResult* namespace_modules[] = {
        &namespace_app_two,
        &namespace_math,
        &namespace_app,
    };
    analysis_resolve_program_interfaces(arguments->arena, namespace_modules, BUSTER_ARRAY_LENGTH(namespace_modules));
    analysis_analyze_bodies(arguments->arena, &namespace_math);
    analysis_analyze_bodies(arguments->arena, &namespace_app);
    analysis_analyze_bodies(arguments->arena, &namespace_app_two);
    BUSTER_TEST(arguments, namespace_math.instantiation_count == 1);
    BUSTER_TEST(arguments, namespace_math.first_instantiation && namespace_math.first_instantiation->requester_count == 2);
    BUSTER_TEST(arguments, namespace_math.first_instantiation && namespace_math.first_instantiation->codegen_owner.value == 40);
    analysis_analyze_bodies(arguments->arena, &namespace_math);
    analysis_compute_layouts(&namespace_math, (AnalysisLayoutOptions){.pointer_size = 8, .pointer_alignment = 8});
    analysis_compute_layouts(&namespace_app, (AnalysisLayoutOptions){.pointer_size = 8, .pointer_alignment = 8});
    analysis_compute_layouts(&namespace_app_two, (AnalysisLayoutOptions){.pointer_size = 8, .pointer_alignment = 8});
    BUSTER_TEST(arguments, namespace_math.diagnostic_count == 0);
    BUSTER_TEST(arguments, namespace_app.diagnostic_count == 0);
    BUSTER_TEST(arguments, namespace_app_two.diagnostic_count == 0);
    analysis_build_jobs(arguments->arena, &namespace_math);
    analysis_build_jobs(arguments->arena, &namespace_app);
    analysis_build_jobs(arguments->arena, &namespace_app_two);
    AnalysisResult* scheduled_modules[] = {
        &namespace_app_two,
        &namespace_math,
        &namespace_app,
    };
    AnalysisProgram scheduled_program = {
        .module_results = scheduled_modules,
        .module_count = BUSTER_ARRAY_LENGTH(scheduled_modules),
    };
    AnalysisProgramScheduleResult program_schedule = analysis_execute_program_jobs(arguments->arena, &scheduled_program, 4, 0, 0);
    BUSTER_TEST(arguments, !program_schedule.has_cycle);
    BUSTER_TEST(arguments, program_schedule.execution_count == namespace_math.job_count + namespace_app.job_count + namespace_app_two.job_count);
    BUSTER_TEST(arguments, program_schedule.wave_count >= 2);

    // Interface-only/cached module results need not retain ordinary body
    // storage. Specialized jobs still resolve their instantiation bodies.
    AnalysisBody* namespace_math_bodies = namespace_math.module.bodies;
    namespace_math.module.bodies = 0;
    AnalysisResult* absent_body_modules[] = {&namespace_math};
    AnalysisProgram absent_body_program = {
        .module_results = absent_body_modules,
        .module_count = BUSTER_ARRAY_LENGTH(absent_body_modules),
    };
    AnalysisProgramScheduleResult absent_body_schedule = analysis_execute_program_jobs(arguments->arena, &absent_body_program, 4, 0, 0);
    BUSTER_TEST(arguments, !absent_body_schedule.has_cycle);
    BUSTER_TEST(arguments, absent_body_schedule.execution_count == namespace_math.job_count);
    namespace_math.module.bodies = namespace_math_bodies;

    AnalysisEntity* namespace_use = analysis_value_entity_find(&namespace_app, S8("use"));
    BUSTER_TEST(arguments, namespace_use != 0);
    if (namespace_use)
    {
        AnalysisType* namespace_function = analysis_type_from_id(&namespace_app, namespace_app.module.semantics[namespace_use->id.index.value].type);
        BUSTER_TEST(arguments, namespace_function->kind == ANALYSIS_TYPE_FUNCTION);
        BUSTER_TEST(arguments, namespace_function->kind != ANALYSIS_TYPE_FUNCTION || namespace_function->as.function.argument_count == 1);
        if (namespace_function->kind == ANALYSIS_TYPE_FUNCTION && namespace_function->as.function.argument_count == 1)
        {
            AnalysisType* namespace_vector = analysis_type_from_id(&namespace_app, namespace_function->as.function.argument_types[0]);
            BUSTER_TEST(arguments, namespace_vector->kind == ANALYSIS_TYPE_STRUCT);
            BUSTER_TEST(arguments, namespace_vector->as.declaration.module.value == 40);
            BUSTER_TEST(arguments, namespace_vector->layout.state == ANALYSIS_LAYOUT_RESOLVED);
            BUSTER_TEST(arguments, namespace_vector->layout.size == 8);
            BUSTER_TEST(arguments, namespace_vector->layout.alignment == 4);
        }
        AnalysisBody* namespace_body = namespace_app.module.bodies + namespace_use->id.index.value;
        BUSTER_TEST(arguments, namespace_body->analyzed);
        BUSTER_TEST(arguments, namespace_body->dependency_count == 2);
        for (u32 dependency_index = 0; dependency_index < namespace_body->dependency_count; dependency_index += 1)
        {
            BUSTER_TEST(arguments, namespace_body->dependencies[dependency_index].module.value == 40);
        }
    }
    AnalysisInterfaceSummary specialization_summary = analysis_module_interface_summary(arguments->arena, &namespace_math);
    BUSTER_TEST(arguments, analysis_interface_summary_is_valid(specialization_summary));
    BUSTER_TEST(arguments, string_first_sequence(specialization_summary.bytes, S8("specialization ")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(specialization_summary.bytes, S8("requester=app\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(specialization_summary.bytes, S8("requester=app-two\n")) != BUSTER_STRING_NO_MATCH);
    AnalysisInterfaceCache specialization_cache = {0};
    BUSTER_TEST(arguments, analysis_interface_cache_store(arguments->arena, &specialization_cache, namespace_math.module.name, specialization_summary));
    AnalysisInterfaceSummary specialization_round_trip = analysis_module_interface_summary(arguments->arena, &namespace_math);
    BUSTER_TEST(arguments, !analysis_interface_cache_store(arguments->arena, &specialization_cache, namespace_math.module.name, specialization_round_trip));
    AnalysisInterfaceCacheEntry* specialization_cache_entry = analysis_interface_cache_find(&specialization_cache, namespace_math.module.name);
    BUSTER_TEST(arguments, specialization_cache_entry != 0);
    BUSTER_TEST(arguments, specialization_cache_entry && specialization_cache_entry->summary.hash == specialization_summary.hash);
    BUSTER_TEST(arguments, specialization_cache_entry && string_equal(specialization_cache_entry->summary.bytes, specialization_summary.bytes));
    AnalysisInterfaceSummary corrupted_summary = specialization_summary;
    corrupted_summary.hash ^= 1;
    BUSTER_TEST(arguments, !analysis_interface_summary_is_valid(corrupted_summary));

    String8 stable_source_left = S8("code identity : fn ($value: $T) $T { return value; }\n"
                                    "code main : fn () s32\n"
                                    "{\n"
                                    "    data first: s32 = identity(5);\n"
                                    "    data second: s32 = identity(6);\n"
                                    "    return first + second;\n"
                                    "}\n");
    String8 stable_source_right = S8("code identity : fn ($value: $T) $T { return value; }\n"
                                     "code main : fn () s32\n"
                                     "{\n"
                                     "    data second: s32 = identity(6);\n"
                                     "    data first: s32 = identity(5);\n"
                                     "    return first + second;\n"
                                     "}\n");
    TokenizerResult stable_left_tokens = tokenize(arguments->arena, stable_source_left.pointer, stable_source_left.length);
    ParserResult stable_left_parser = parser_parse(arguments->arena, expression_arena, stable_source_left, stable_left_tokens);
    TokenizerResult stable_right_tokens = tokenize(arguments->arena, stable_source_right.pointer, stable_source_right.length);
    ParserResult stable_right_parser = parser_parse(arguments->arena, expression_arena, stable_source_right, stable_right_tokens);
    AnalysisSourceInput stable_left_input = {
        .path = S8("stable.bbb"),
        .parser = &stable_left_parser,
    };
    AnalysisSourceInput stable_right_input = {
        .path = S8("stable.bbb"),
        .parser = &stable_right_parser,
    };
    AnalysisResult stable_left = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 50}, S8("stable"), &stable_left_input, 1);
    AnalysisResult stable_right = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 99}, S8("stable"), &stable_right_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &stable_left);
    analysis_resolve_module_interfaces(arguments->arena, &stable_right);
    analysis_analyze_bodies(arguments->arena, &stable_left);
    analysis_analyze_bodies(arguments->arena, &stable_right);
    BUSTER_TEST(arguments, stable_left.instantiation_count == 2);
    BUSTER_TEST(arguments, stable_right.instantiation_count == 2);
    String8 stable_left_five = {0};
    String8 stable_right_five = {0};
    AnalysisInstantiationId stable_left_five_id = ANALYSIS_INSTANTIATION_ID_INVALID;
    AnalysisInstantiationId stable_right_five_id = ANALYSIS_INSTANTIATION_ID_INVALID;
    for (AnalysisInstantiation* instantiation = stable_left.first_instantiation; instantiation; instantiation = instantiation->next)
    {
        if (instantiation->compile_time_argument_count && instantiation->compile_time_arguments[0].constant.integer == 5)
        {
            stable_left_five = instantiation->symbol_name;
            stable_left_five_id = instantiation->id;
        }
    }
    for (AnalysisInstantiation* instantiation = stable_right.first_instantiation; instantiation; instantiation = instantiation->next)
    {
        if (instantiation->compile_time_argument_count && instantiation->compile_time_arguments[0].constant.integer == 5)
        {
            stable_right_five = instantiation->symbol_name;
            stable_right_five_id = instantiation->id;
        }
    }
    BUSTER_STRING_TEST(arguments, stable_left_five, stable_right_five);
    BUSTER_TEST(arguments, stable_left_five_id.value != stable_right_five_id.value);
    AnalysisInterfaceSummary stable_left_summary = analysis_module_interface_summary(arguments->arena, &stable_left);
    AnalysisInterfaceSummary stable_right_summary = analysis_module_interface_summary(arguments->arena, &stable_right);
    BUSTER_STRING_TEST(arguments, stable_left_summary.bytes, stable_right_summary.bytes);
    BUSTER_TEST(arguments, stable_left_summary.hash == stable_right_summary.hash);
    AnalysisInterfaceCache stable_cache = {0};
    BUSTER_TEST(arguments, analysis_interface_cache_store(arguments->arena, &stable_cache, S8("stable"), stable_left_summary));
    BUSTER_TEST(arguments, !analysis_interface_cache_store(arguments->arena, &stable_cache, S8("stable"), stable_right_summary));
    BUSTER_CHECK(arena_destroy(expression_arena, 1));

    Arena* fixture_expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(fixture_expression_arena);
    String8 rule_source = S8("type Duplicate = struct { value: s32, value: u32, }\n"
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
    TokenizerResult rule_tokenizer = tokenize(arguments->arena, rule_source.pointer, rule_source.length);
    ParserResult rule_parser = parser_parse(arguments->arena, fixture_expression_arena, rule_source, rule_tokenizer);
    BUSTER_TEST(arguments, rule_parser.diagnostic_count == 0);
    AnalysisSourceInput rule_input = {.path = S8("semantic-rules.bbb"), .parser = &rule_parser};
    AnalysisResult rule_result = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 12}, S8("semantic-rules"), &rule_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &rule_result);
    analysis_analyze_bodies(arguments->arena, &rule_result);
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_FIELD));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_ENUM_MEMBER));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_LOCAL));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_AGGREGATE_FIELD));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_MISSING_AGGREGATE_FIELD));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_SWITCH_CASE));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_NONEXHAUSTIVE_SWITCH));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_MISSING_RETURN));
    BUSTER_TEST(arguments, rule_result.first_diagnostic != 0);
    if (rule_result.first_diagnostic)
    {
        String8 formatted = analysis_format_diagnostic(arguments->arena, &rule_result, rule_result.first_diagnostic);
        BUSTER_TEST(arguments, string_first_sequence(formatted, S8("semantic-rules.bbb")) != BUSTER_STRING_NO_MATCH);
        BUSTER_TEST(arguments, string_first_sequence(formatted, S8(": error: ")) != BUSTER_STRING_NO_MATCH);
    }

    for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(analysis_fixture_tests); fixture_index += 1)
    {
        TemporalArena fixture_temporary = arena_begin_temporal(arguments->arena);
        AnalysisFixtureTest fixture = analysis_fixture_tests[fixture_index];
        FileMapRead source_file = file_map_read(arguments->arena, fixture.path, (FileReadOptions){0});
        String8 source = BYTE_SLICE_TO_STRING(8, source_file.bytes);
        BUSTER_TEST(arguments, source.pointer != 0);
        TokenizerResult tokenizer = tokenize(arguments->arena, source.pointer, source.length);
        ParserResult parser = parser_parse(arguments->arena, fixture_expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, tokenizer.error_count == 0);
        BUSTER_TEST(arguments, parser.diagnostic_count == 0);
        AnalysisSourceInput input = {.path = fixture.path, .parser = &parser};
        AnalysisResult fixture_result = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 100 + fixture_index}, S8("fixture"), &input, 1);
        analysis_resolve_module_interfaces(arguments->arena, &fixture_result);
        analysis_analyze_bodies(arguments->arena, &fixture_result);
        analysis_compute_layouts(&fixture_result, (AnalysisLayoutOptions){.pointer_size = 8, .pointer_alignment = 8});
        analysis_build_jobs(arguments->arena, &fixture_result);
        BUSTER_TEST(arguments, fixture_result.diagnostic_count == fixture.expected_diagnostic_count);
        if (string_equal(fixture.path, S8("tests/basic_vector.bbb")))
        {
            AnalysisEntity* main_entity = analysis_value_entity_find(&fixture_result, S8("main"));
            BUSTER_TEST(arguments, main_entity != 0);
            if (main_entity)
            {
                AnalysisBody* body = fixture_result.module.bodies + main_entity->id.index.value;
                AnalysisLocal* sum = 0;
                AnalysisLocal* less = 0;
                for (u32 local_index = 0; local_index < body->local_count; local_index += 1)
                {
                    if (string_equal(body->locals[local_index].name, S8("sum")))
                    {
                        sum = body->locals + local_index;
                    }
                    if (string_equal(body->locals[local_index].name, S8("less")))
                    {
                        less = body->locals + local_index;
                    }
                }
                BUSTER_TEST(arguments, sum != 0);
                if (sum)
                {
                    AnalysisType* type = analysis_type_from_id(&fixture_result, sum->type);
                    BUSTER_TEST(arguments, type->kind == ANALYSIS_TYPE_VECTOR);
                    BUSTER_TEST(arguments, type->as.vector.count == 4);
                    BUSTER_TEST(arguments, type->layout.size == 16);
                    BUSTER_TEST(arguments, type->layout.abi_class == ANALYSIS_ABI_CLASS_VECTOR);
                }
                BUSTER_TEST(arguments, less != 0);
                if (less)
                {
                    AnalysisType* mask = analysis_type_from_id(&fixture_result, less->type);
                    AnalysisType* lane = mask->kind == ANALYSIS_TYPE_VECTOR ? analysis_type_from_id(&fixture_result, mask->as.vector.element_type) : 0;
                    BUSTER_TEST(arguments, mask->kind == ANALYSIS_TYPE_VECTOR);
                    BUSTER_TEST(arguments, mask->as.vector.count == 4);
                    BUSTER_TEST(arguments, lane && lane->kind == ANALYSIS_TYPE_INTEGER && !lane->as.integer.is_signed && lane->as.integer.bit_width == 32);
                }
            }
        }
        if (string_equal(fixture.path, S8("tests/basic_compile_time.bbb")))
        {
            AnalysisEntity* constant = analysis_value_entity_find(&fixture_result, S8("this_is_a_constant"));
            BUSTER_TEST(arguments, constant && constant->kind == ANALYSIS_ENTITY_DATA);
            if (constant)
            {
                AnalysisEntitySemantic* semantic = fixture_result.module.semantics + constant->id.index.value;
                BUSTER_TEST(arguments, semantic->constant && semantic->constant->kind == ANALYSIS_CONSTANT_INTEGER);
                BUSTER_TEST(arguments, semantic->constant && semantic->constant->integer == 5);
            }
            AnalysisEntity* generic = analysis_value_entity_find(&fixture_result, S8("identity"));
            BUSTER_TEST(arguments, generic != 0);
            if (generic)
            {
                AnalysisType* generic_function = analysis_type_from_id(&fixture_result, fixture_result.module.semantics[generic->id.index.value].type);
                BUSTER_TEST(arguments, generic_function->kind == ANALYSIS_TYPE_FUNCTION);
                BUSTER_TEST(arguments, generic_function->kind != ANALYSIS_TYPE_FUNCTION ||
                                           analysis_type_from_id(&fixture_result, generic_function->as.function.return_type)->kind ==
                                               ANALYSIS_TYPE_COMPILE_TIME_PARAMETER);
            }
            BUSTER_TEST(arguments, fixture_result.instantiation_count == 3);
            u32 analyzed_instantiation_count = 0;
            for (AnalysisInstantiation* instantiation = fixture_result.first_instantiation; instantiation; instantiation = instantiation->next)
            {
                AnalysisType* concrete = analysis_type_from_id(&fixture_result, instantiation->function_type);
                BUSTER_TEST(arguments, instantiation->analyzed);
                BUSTER_TEST(arguments, instantiation->body.analyzed);
                BUSTER_TEST(arguments, concrete->kind == ANALYSIS_TYPE_FUNCTION);
                BUSTER_TEST(arguments, concrete->kind != ANALYSIS_TYPE_FUNCTION || concrete->as.function.argument_count == 0);
                analyzed_instantiation_count += instantiation->analyzed;
            }
            BUSTER_TEST(arguments, analyzed_instantiation_count == 3);
            u32 specialized_body_job_count = 0;
            u32 ordinary_body_job_count = 0;
            for (u32 job_index = 0; job_index < fixture_result.job_count; job_index += 1)
            {
                AnalysisJob* job = fixture_result.jobs + job_index;
                if (job->kind != ANALYSIS_JOB_BODY)
                {
                    continue;
                }
                if (job->instantiation.value == ANALYSIS_ID_UNDERLYING_INVALID)
                {
                    ordinary_body_job_count += 1;
                }
                else
                {
                    specialized_body_job_count += 1;
                }
            }
            BUSTER_TEST(arguments, ordinary_body_job_count == 1);
            BUSTER_TEST(arguments, specialized_body_job_count == 3);
            AnalysisScheduleResult generic_schedule = analysis_execute_jobs(arguments->arena, &fixture_result, 2, 0, 0);
            BUSTER_TEST(arguments, !generic_schedule.has_cycle);
            BUSTER_TEST(arguments, generic_schedule.execution_count == fixture_result.job_count);
        }
        else if (string_equal(fixture.path, S8("tests/compile_time_argument_error.bbb")))
        {
            BUSTER_TEST(arguments, fixture_result.instantiation_count == 0);
            BUSTER_TEST(arguments, fixture_result.first_diagnostic != 0);
            if (fixture_result.first_diagnostic)
            {
                BUSTER_TEST(arguments, fixture_result.first_diagnostic->kind == ANALYSIS_DIAGNOSTIC_COMPILE_TIME_ARGUMENT_REQUIRED);
                BUSTER_TEST(arguments, fixture_result.first_diagnostic->has_argument_index);
                BUSTER_TEST(arguments, fixture_result.first_diagnostic->argument_index == 0);
            }
        }
        for (u32 entity_index = 0; entity_index < fixture_result.module.entity_count; entity_index += 1)
        {
            AnalysisEntity* fixture_entity = fixture_result.module.entities + entity_index;
            if (fixture_entity->kind == ANALYSIS_ENTITY_CODE && fixture_entity->ast.code->has_body)
            {
                if (!analysis_entity_is_generic(fixture_temporary.arena, &fixture_result, fixture_entity))
                {
                    BUSTER_TEST(arguments, fixture_result.module.bodies[entity_index].analyzed);
                }
            }
        }
        file_map_unmap(source_file);
        arena_set_position(fixture_temporary.arena, fixture_temporary.position);
    }

    AnalysisProgram loaded = analysis_program_load(arguments->arena, fixture_expression_arena,
                                                   (AnalysisProgramOptions){
                                                       .root_path = S8("tests/basic_import.bbb"),
                                                       .root_module_name = S8("app"),
                                                       .module_root = S8("tests/modules"),
                                                       .pointer_size = 8,
                                                       .pointer_alignment = 8,
                                                   });
    BUSTER_TEST(arguments, !loaded.load_failed);
    BUSTER_TEST(arguments, loaded.module_count == 3);
    BUSTER_TEST(arguments, loaded.root != 0);
    BUSTER_TEST(arguments, loaded.parser_diagnostic_count == 0);
    BUSTER_TEST(arguments, loaded.analysis_diagnostic_count == 0);
    if (loaded.root)
    {
        BUSTER_STRING_TEST(arguments, loaded.root->name, S8("app"));
        BUSTER_TEST(arguments, loaded.root->analysis->module.id.value == 0);
        BUSTER_TEST(arguments, loaded.root->analysis->module.import_count == 2);
        String8 first_summary = analysis_serialize_module_interface(arguments->arena, loaded.root->analysis);
        String8 second_summary = analysis_serialize_module_interface(arguments->arena, loaded.root->analysis);
        BUSTER_STRING_TEST(arguments, first_summary, second_summary);
        AnalysisInterfaceSummary hashed = analysis_module_interface_summary(arguments->arena, loaded.root->analysis);
        AnalysisInterfaceCache cache = {0};
        BUSTER_TEST(arguments, analysis_interface_cache_store(arguments->arena, &cache, loaded.root->name, hashed));
        BUSTER_TEST(arguments, !analysis_interface_cache_store(arguments->arena, &cache, loaded.root->name, hashed));
        BUSTER_TEST(arguments, cache.count == 1);
        AnalysisProgramScheduleResult loaded_schedule = analysis_execute_program_jobs(arguments->arena, &loaded, 4, 0, 0);
        BUSTER_TEST(arguments, !loaded_schedule.has_cycle);
        BUSTER_TEST(arguments, loaded_schedule.execution_count != 0);
    }
    analysis_program_unmap_sources(&loaded);

    FileMapRead memory_source_map = file_map_read(arguments->arena, S8("tests/fuzz/valid_buster.bbb"), (FileReadOptions){0});
    String8 memory_source = BYTE_SLICE_TO_STRING(8, memory_source_map.bytes);
    BUSTER_TEST(arguments, memory_source.pointer != 0);
    if (memory_source.pointer)
    {
        AnalysisProgram memory_loaded = analysis_program_load_memory(arguments->arena, fixture_expression_arena, memory_source);
        BUSTER_TEST(arguments, !memory_loaded.load_failed);
        BUSTER_TEST(arguments, memory_loaded.module_count == 1);
        BUSTER_TEST(arguments, memory_loaded.root != 0);
        BUSTER_TEST(arguments, memory_loaded.parser_diagnostic_count == 0);
        BUSTER_TEST(arguments, memory_loaded.analysis_diagnostic_count == 0);
        if (memory_loaded.root)
        {
            BUSTER_STRING_TEST(arguments, memory_loaded.root->name, S8("fuzz"));
            BUSTER_STRING_TEST(arguments, memory_loaded.root->path, S8("fuzz.bbb"));
            BUSTER_TEST(arguments, memory_loaded.root->source.pointer == memory_source.pointer);
            BUSTER_TEST(arguments, memory_loaded.root->source.length == memory_source.length);
            BUSTER_TEST(arguments, memory_loaded.root->source_map.bytes.pointer == 0);
            BUSTER_TEST(arguments, memory_loaded.root->source_map.mapped_pointer == 0);
        }
        IrProgram memory_ir = ir_generate_program(arguments->arena, &memory_loaded);
        BUSTER_TEST(arguments, memory_ir.module_count == 1);
        if (memory_loaded.root && memory_ir.module_count == 1)
        {
            BUSTER_TEST(arguments, ir_validate_module(memory_loaded.root->analysis, &memory_ir.modules[0]).error == IR_VALIDATION_NONE);
        }
    }
    file_map_unmap(memory_source_map);

    AnalysisProgram missing_memory = analysis_program_load_memory(arguments->arena, fixture_expression_arena,
                                                                  S8("import missing = \"not_available\";\n"
                                                                     "code main : fn () s32\n"
                                                                     "{\n"
                                                                     "    return 0;\n"
                                                                     "}\n"));
    BUSTER_TEST(arguments, missing_memory.module_count == 1);
    BUSTER_TEST(arguments, missing_memory.parser_diagnostic_count == 0);
    BUSTER_TEST(arguments, missing_memory.analysis_diagnostic_count == 1);
    BUSTER_TEST(arguments, missing_memory.root && analysis_test_has_diagnostic(missing_memory.root->analysis, ANALYSIS_DIAGNOSTIC_MISSING_IMPORTED_MODULE));
    IrProgram missing_memory_ir = ir_generate_program(arguments->arena, &missing_memory);
    BUSTER_TEST(arguments, missing_memory_ir.module_count == 1);
    if (missing_memory.root && missing_memory_ir.module_count == 1)
    {
        BUSTER_TEST(arguments, ir_validate_module(missing_memory.root->analysis, &missing_memory_ir.modules[0]).error == IR_VALIDATION_NONE);
    }
    BUSTER_CHECK(arena_destroy(fixture_expression_arena, 1));

    arena_set_position(temporary.arena, temporary.position);
    return result;
}
