// Included by driver_test.c. Every pass subset executes against fixed C
// answers on the native backend, and reaches all shared non-native consumers.
#include <buster/tests/compiler/codegen/ebpf_test_vm.h>
BUSTER_GLOBAL_LOCAL bool compiler_driver_test_string_contains(String8 text, String8 needle)
{
    bool result = needle.length <= text.length;
    if (result && needle.length)
    {
        result = false;
        for (u64 offset = 0; offset <= text.length - needle.length && !result; offset += 1)
        {
            result = memcmp(text.pointer + offset, needle.pointer, needle.length) == 0;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_positional_languages(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = temporary.arena;
    String8 first = buster_test_temporary_path(arena, S8("buster-positional-language-first"), S8(".input"));
    String8 second = buster_test_temporary_path(arena, S8("buster-positional-language-second"), S8(".c"));
    String8 third = buster_test_temporary_path(arena, S8("buster-positional-language-third"), S8(".source"));
    String8 middle = buster_test_temporary_path(arena, S8("buster-positional-language-middle"), S8(".c"));
    String8 assembly = buster_test_temporary_path(arena, S8("buster-positional-language-assembly"), S8(".input"));
    BUSTER_TEST(arguments, file_write(first, BUSTER_SLICE_TO_BYTE_SLICE(S8("int positional_helper(void) { return 1; }\n"))));
    BUSTER_TEST(arguments, file_write(second, BUSTER_SLICE_TO_BYTE_SLICE(S8("int positional_second(void) { return 2; }\n"))));
    BUSTER_TEST(arguments, file_write(third, BUSTER_SLICE_TO_BYTE_SLICE(S8("int positional_helper(void);\nint main(void) { return positional_helper() == 1 ? 0 : 1; }\n"))));
    BUSTER_TEST(arguments, file_write(middle, BUSTER_SLICE_TO_BYTE_SLICE(S8("int positional_middle(\n"))));
    BUSTER_TEST(arguments, file_write(assembly, BUSTER_SLICE_TO_BYTE_SLICE(S8(".text\n"))));

    String8 mixed_command[] = {
        S8("-nostdinc"), S8("-fsyntax-only"), S8("-x"), S8("c"), first,
        S8("-x"), S8("none"), second,
    };
    CompilerDriverInvocation mixed = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(mixed_command));
    BUSTER_TEST(arguments, mixed.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, mixed.input_count == 2 && mixed.input_language_count == 2 && mixed.input_languages != 0);
    if (BUSTER_REQUIRE(arguments, mixed.input_languages && mixed.input_language_count == 2))
    {
        BUSTER_TEST(arguments, mixed.input_languages[0] == COMPILER_DRIVER_LANGUAGE_C);
        BUSTER_TEST(arguments, mixed.input_languages[1] == COMPILER_DRIVER_LANGUAGE_AUTOMATIC);
    }
    BUSTER_TEST(arguments, mixed.language == COMPILER_DRIVER_LANGUAGE_AUTOMATIC);
    CompilerDriverResult mixed_result = compiler_driver_execute_invocation(arena, mixed);
    if (mixed_result.error != COMPILER_DRIVER_ERROR_NONE)
    {
        arguments->show(arguments, S8("positional mixed input: {S8}\n"), mixed_result.diagnostic);
    }
    BUSTER_TEST(arguments, mixed_result.error == COMPILER_DRIVER_ERROR_NONE);

    String8 trailing_command[] = {
        S8("-nostdinc"), S8("-fsyntax-only"), S8("-x"), S8("c"), first,
        S8("-x"), S8("assembler"),
    };
    CompilerDriverInvocation trailing = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(trailing_command));
    BUSTER_TEST(arguments, trailing.error == COMPILER_DRIVER_ERROR_NONE && trailing.input_count == 1 && trailing.input_language_count == 1);
    if (BUSTER_REQUIRE(arguments, trailing.input_languages && trailing.input_language_count == 1))
    {
        BUSTER_TEST(arguments, trailing.input_languages[0] == COMPILER_DRIVER_LANGUAGE_C);
    }
    BUSTER_TEST(arguments, trailing.language == COMPILER_DRIVER_LANGUAGE_ASSEMBLY);
    CompilerDriverResult trailing_result = compiler_driver_execute_invocation(arena, trailing);
    BUSTER_TEST(arguments, trailing_result.error == COMPILER_DRIVER_ERROR_NONE);

    String8 alternating_command[] = {
        S8("-nostdinc"), S8("-fsyntax-only"),
        S8("-x"), S8("c"), first,
        S8("-x"), S8("assembler"), assembly,
        S8("-x"), S8("none"), second,
        S8("-x"), S8("c"), third,
    };
    CompilerDriverInvocation alternating = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(alternating_command));
    BUSTER_TEST(arguments, alternating.error == COMPILER_DRIVER_ERROR_NONE && alternating.input_count == 4 && alternating.input_language_count == 4);
    if (BUSTER_REQUIRE(arguments, alternating.input_languages && alternating.input_language_count == 4))
    {
        BUSTER_TEST(arguments, alternating.input_languages[0] == COMPILER_DRIVER_LANGUAGE_C);
        BUSTER_TEST(arguments, alternating.input_languages[1] == COMPILER_DRIVER_LANGUAGE_ASSEMBLY);
        BUSTER_TEST(arguments, alternating.input_languages[2] == COMPILER_DRIVER_LANGUAGE_AUTOMATIC);
        BUSTER_TEST(arguments, alternating.input_languages[3] == COMPILER_DRIVER_LANGUAGE_C);
    }

    String8 automatic_command[] = {S8("-nostdinc"), S8("-fsyntax-only"), second};
    CompilerDriverInvocation automatic = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(automatic_command));
    BUSTER_TEST(arguments, automatic.error == COMPILER_DRIVER_ERROR_NONE && automatic.input_language_count == 1 &&
                           automatic.input_languages && automatic.input_languages[0] == COMPILER_DRIVER_LANGUAGE_AUTOMATIC);
    CompilerDriverResult automatic_result = compiler_driver_execute_invocation(arena, automatic);
    BUSTER_TEST(arguments, automatic_result.error == COMPILER_DRIVER_ERROR_NONE);

    String8 missing_command[] = {S8("-x")};
    CompilerDriverInvocation missing = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(missing_command));
    BUSTER_TEST(arguments, missing.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    String8 invalid_command[] = {S8("-x"), S8("objective-c"), first};
    CompilerDriverInvocation invalid = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_command));
    BUSTER_TEST(arguments, invalid.error == COMPILER_DRIVER_ERROR_ARGUMENT);

    CompilerDriverInvocation legacy = trailing;
    legacy.input_languages = 0;
    legacy.input_language_count = 0;
    legacy.language = COMPILER_DRIVER_LANGUAGE_C;
    CompilerDriverResult legacy_result = compiler_driver_execute_invocation(arena, legacy);
    BUSTER_TEST(arguments, legacy_result.error == COMPILER_DRIVER_ERROR_NONE);
    CompilerDriverInvocation malformed = trailing;
    malformed.input_language_count = 0;
    CompilerDriverResult malformed_result = compiler_driver_execute_invocation(arena, malformed);
    BUSTER_TEST(arguments, malformed_result.error == COMPILER_DRIVER_ERROR_ARGUMENT);

    String8 ordered_one_command[] = {
        S8("-nostdinc"), S8("-fcompile-jobs=1"), S8("-x"), S8("c"), first,
        S8("-x"), S8("none"), middle, second,
    };
    String8 ordered_two_command[] = {
        S8("-nostdinc"), S8("-fcompile-jobs=2"), S8("-x"), S8("c"), first,
        S8("-x"), S8("none"), middle, second,
    };
    CompilerDriverInvocation ordered_one = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(ordered_one_command));
    CompilerDriverInvocation ordered_two = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(ordered_two_command));
    CompilerDriverResult ordered_one_result = compiler_driver_execute_invocation(arena, ordered_one);
    CompilerDriverResult ordered_two_result = compiler_driver_execute_invocation(arena, ordered_two);
    BUSTER_TEST(arguments, ordered_one_result.error != COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, ordered_two_result.error == ordered_one_result.error);
    BUSTER_STRING_TEST(arguments, ordered_two_result.diagnostic, ordered_one_result.diagnostic);
    BUSTER_TEST(arguments, compiler_driver_test_string_contains(ordered_two_result.diagnostic, middle));

#if !BUSTER_ANDROID && !BUSTER_IOS
    String8 serial_output = buster_test_temporary_path(arena, S8("buster-positional-language-serial"),
#if BUSTER_WINDOWS
                                                        S8(".exe"));
#else
                                                        S8(""));
#endif
    String8 parallel_output = buster_test_temporary_path(arena, S8("buster-positional-language-parallel"),
#if BUSTER_WINDOWS
                                                          S8(".exe"));
#else
                                                          S8(""));
#endif
    String8 boundary_output = buster_test_temporary_path(arena, S8("buster-positional-language-boundary"),
#if BUSTER_WINDOWS
                                                          S8(".exe"));
#else
                                                          S8(""));
#endif
    String8 serial_command[] = {
        S8("-nostdinc"), S8("-fcompile-jobs=1"), S8("-x"), S8("c"), first,
        S8("-x"), S8("c"), third, S8("-o"), serial_output,
    };
    String8 parallel_command[] = {
        S8("-nostdinc"), S8("-fcompile-jobs=2"), S8("-x"), S8("c"), first,
        S8("-x"), S8("c"), third, S8("-o"), parallel_output,
    };
    CompilerDriverInvocation serial = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(serial_command));
    CompilerDriverInvocation parallel = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parallel_command));
    CompilerDriverResult serial_result = compiler_driver_execute_invocation(arena, serial);
    CompilerDriverResult parallel_result = compiler_driver_execute_invocation(arena, parallel);
    if (serial_result.error != COMPILER_DRIVER_ERROR_NONE)
    {
        arguments->show(arguments, S8("positional serial link: {S8}\n"), serial_result.diagnostic);
    }
    if (parallel_result.error != COMPILER_DRIVER_ERROR_NONE)
    {
        arguments->show(arguments, S8("positional parallel link: {S8}\n"), parallel_result.diagnostic);
    }
    BUSTER_TEST(arguments, serial_result.error == COMPILER_DRIVER_ERROR_NONE && serial_result.compilation_workers == 1);
    BUSTER_TEST(arguments, parallel_result.error == COMPILER_DRIVER_ERROR_NONE);
#if !BUSTER_SINGLE_THREADED
    if (lane_count() == 1 && os_get_logical_thread_count() > 1)
    {
        BUSTER_TEST(arguments, parallel_result.compilation_workers == 2);
    }
#endif

    String8 boundary_command[] = {
        S8("-nostdinc"), S8("-fcompile-jobs=2"),
        S8("-x"), S8("c"), first,
        S8("-x"), S8("assembler"), assembly,
        S8("-x"), S8("c"), third,
        S8("-o"), boundary_output,
    };
    CompilerDriverInvocation boundary = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(boundary_command));
    CompilerDriverResult boundary_result = compiler_driver_execute_invocation(arena, boundary);
    if (boundary_result.error != COMPILER_DRIVER_ERROR_NONE)
    {
        arguments->show(arguments, S8("positional cohort boundary: {S8}\n"), boundary_result.diagnostic);
    }
    BUSTER_TEST(arguments, boundary_result.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, boundary_result.compilation_workers == 1);
#endif

    scratch_end(temporary);
    return result;
}

// Static assertions containing local-object sizeof or _Generic operands must
// agree in the semantic-only and object actions, including both frontend SSA
// forms.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_local_sizeof_static_asserts(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct
    {
        String8 source;
        bool valid;
        String8 diagnostic_fragment;
    } cases[] = {
        {S8("typedef long T;\n"
            "int f(void) {\n"
            "    T a = 0;\n"
            "    {\n"
            "        typedef char T;\n"
            "        T b = 0;\n"
            "        _Static_assert(sizeof b == 1, \"inner\");\n"
            "    }\n"
            "    return sizeof a;\n"
            "}\n"), true, {0}},
        {S8("int f(void) {\n"
            "    int a[4];\n"
            "    _Static_assert(sizeof a == 4 * sizeof(int), \"array\");\n"
            "    return _Generic(a, int *: 1);\n"
            "}\n"), true, {0}},
        {S8("int f(void) {\n"
            "    char value;\n"
            "    _Static_assert(sizeof (value) == 1, \"parenthesized scalar\");\n"
            "    return sizeof value;\n"
            "}\n"), true, {0}},
        {S8("int f(void) {\n"
            "    int values[4];\n"
            "    _Static_assert(sizeof (values) == 4 * sizeof(int), \"parenthesized array\");\n"
            "    return sizeof values;\n"
            "}\n"), true, {0}},
        {S8("int f(int n) {\n"
            "    _Static_assert(sizeof n == n, \"runtime value\");\n"
            "    return n;\n"
            "}\n"), false, S8("static assertion expression is not an integer constant expression: sizeof n == n")},
        {S8("int f(int n) {\n"
            "    int values[n];\n"
            "    _Static_assert(sizeof values == n * sizeof(int), \"variable array\");\n"
            "    return (int)sizeof values;\n"
            "}\n"), false, S8("static assertion expression is not an integer constant expression: sizeof values == n * sizeof(int)")},
        {S8("int f(void) {\n"
            "    char value;\n"
            "    _Static_assert(sizeof value == 2, \"false\");\n"
            "    return 0;\n"
            "}\n"), false, S8("static assertion failed: \"false\"")},
        // #1697: _Generic selects on a block-scope object's type.
        {S8("void f(void) {\n"
            "    long y = 0;\n"
            "    _Static_assert(_Generic(y, long: 1, default: 0), \"generic local\");\n"
            "}\n"), true, {0}},
        {S8("void f(void) {\n"
            "    long y = 0;\n"
            "    _Static_assert(_Generic(y, int: 1, default: 0), \"generic local\");\n"
            "}\n"), false, S8("static assertion failed: \"generic local\"")},
    };
    String8 forms[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(cases); case_index += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        Arena* arena = temporary.arena;
        String8 input = buster_test_temporary_path(arena, S8("buster-local-sizeof-static-assert"), S8(".c"));
        BUSTER_TEST(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(cases[case_index].source)));
        for (u32 form = 0; form < BUSTER_ARRAY_LENGTH(forms); form += 1)
        {
            String8 output = buster_test_temporary_path(arena,
                string_format(arena, S8("buster-local-sizeof-static-assert-{u32}-{u32}"), case_index, form), S8(".o"));
            String8 syntax_command[] = {S8("-nostdinc"), S8("-g0"), S8("-std=gnu23"), forms[form], S8("-fsyntax-only"), input};
            String8 object_command[] = {S8("-nostdinc"), S8("-g0"), S8("-std=gnu23"), forms[form], S8("-c"), S8("-o"), output, input};
            CompilerDriverInvocation syntax_invocation = compiler_driver_parse_arguments(
                arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(syntax_command));
            CompilerDriverInvocation object_invocation = compiler_driver_parse_arguments(
                arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(object_command));
            BUSTER_TEST(arguments, syntax_invocation.error == COMPILER_DRIVER_ERROR_NONE);
            BUSTER_TEST(arguments, object_invocation.error == COMPILER_DRIVER_ERROR_NONE);
#if BUSTER_BENCH_ALLOCATIONS
            IrConstructionCounters before = ir_construction_counters();
#endif
            CompilerDriverResult syntax = compiler_driver_execute_invocation(arena, syntax_invocation);
#if BUSTER_BENCH_ALLOCATIONS
            IrConstructionCounters after = ir_construction_counters();
            BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
            for (u32 counter = 0; counter < IR_CONSTRUCTION_COUNT; counter += 1)
            {
                BUSTER_TEST(arguments, before.values[counter] == after.values[counter]);
            }
#endif
            CompilerDriverResult object = compiler_driver_execute_invocation(arena, object_invocation);
            BUSTER_TEST(arguments, (syntax.error == COMPILER_DRIVER_ERROR_NONE) == cases[case_index].valid);
            BUSTER_TEST(arguments, syntax.error == object.error);
            BUSTER_TEST_RAW(arguments, string_equal(syntax.diagnostic, object.diagnostic),
                            string_format(arena, S8("source={S8}\nsyntax={S8}\nobject={S8}"),
                                          cases[case_index].source, syntax.diagnostic, object.diagnostic));
            BUSTER_STRING_TEST(arguments, syntax.warning, object.warning);
            BUSTER_TEST(arguments, syntax.diagnostic_count == object.diagnostic_count);
            BUSTER_TEST(arguments, syntax.analysis_diagnostic_count == object.analysis_diagnostic_count);
            for (u32 diagnostic = 0; diagnostic < BUSTER_MIN(syntax.diagnostic_count, object.diagnostic_count); diagnostic += 1)
            {
                CompilerDiagnostic first = syntax.diagnostics[diagnostic];
                CompilerDiagnostic second = object.diagnostics[diagnostic];
                BUSTER_TEST(arguments, first.severity == second.severity && first.note_count == second.note_count);
                BUSTER_STRING_TEST(arguments, first.code, second.code);
                BUSTER_STRING_TEST(arguments, first.symbol, second.symbol);
            }
            if (cases[case_index].diagnostic_fragment.length)
            {
                BUSTER_TEST_RAW(arguments, compiler_driver_test_string_contains(syntax.diagnostic, cases[case_index].diagnostic_fragment),
                                string_format(arena, S8("source={S8}\ndiagnostic={S8}"), cases[case_index].source, syntax.diagnostic));
            }
            if (cases[case_index].valid)
            {
                BUSTER_TEST(arguments, object.has_object);
            }
        }
        scratch_end(temporary);
    }
    return result;
}

// The work ledger observes an ordinary compile without changing it: every
// family that the source exercises counts, the counters keep their documented
// relationships, each pipeline phase is entered once per native compile, and
// the phase rows partition the arena traffic exactly. A syntax-only compile is
// the negative control -- it reaches no lowering, machine or output counter.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_work_ledger(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = temporary.arena;
    String8 input = buster_test_temporary_path(arena, S8("buster-work-ledger"), S8(".c"));
    String8 output = buster_test_temporary_path(arena, S8("buster-work-ledger"), S8(".o"));
    String8 source = S8("static const unsigned char table[] = {1, 2, 3, 'x', 0x10};\n"
                        "struct Inner { int a; };\n"
                        "struct Outer { struct { int b; }; struct Inner inner; };\n"
                        "int f(struct Outer* o, int x) { return o->b + o->inner.a + table[x & 3] + (int)sizeof(struct Outer); }\n");
    BUSTER_TEST(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(source)));
    String8 object_command[] = {S8("-nostdinc"), S8("-g0"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("-c"), S8("-o"), output, input};
    String8 syntax_command[] = {S8("-nostdinc"), S8("-g0"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("-fsyntax-only"), input};
    CompilerDriverInvocation object_invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(object_command));
    CompilerDriverInvocation syntax_invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(syntax_command));
    BUSTER_TEST(arguments, object_invocation.error == COMPILER_DRIVER_ERROR_NONE && syntax_invocation.error == COMPILER_DRIVER_ERROR_NONE);
#if BUSTER_BENCH_ALLOCATIONS
    WorkLedgerCounters before = work_ledger_counters();
    ArenaBenchmarkCounters arena_before = arena_benchmark_counters();
#endif
    CompilerDriverResult object = compiler_driver_execute_invocation(arena, object_invocation);
    BUSTER_TEST(arguments, object.error == COMPILER_DRIVER_ERROR_NONE);
#if BUSTER_BENCH_ALLOCATIONS
    WorkLedgerCounters after = work_ledger_counters();
    ArenaBenchmarkCounters arena_after = arena_benchmark_counters();
    BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
    u64 delta[WORK_LEDGER_COUNT];
    for (u32 counter = 0; counter < WORK_LEDGER_COUNT; counter += 1)
    {
        BUSTER_TEST(arguments, after.values[counter] >= before.values[counter]);
        delta[counter] = after.values[counter] - before.values[counter];
    }
    BUSTER_TEST(arguments, delta[WORK_LEDGER_REDERIVE_TYPE_QUERY_ROOTS] != 0);
    BUSTER_TEST(arguments, delta[WORK_LEDGER_REDERIVE_TYPE_QUERY_CACHE_HITS] + delta[WORK_LEDGER_REDERIVE_TYPE_QUERY_UNCACHED] +
                               delta[WORK_LEDGER_REDERIVE_TYPE_QUERY_LITERAL_ANSWERS] == delta[WORK_LEDGER_REDERIVE_TYPE_QUERY_ROOTS]);
    BUSTER_TEST(arguments, delta[WORK_LEDGER_SNAPSHOT_QUERY_CHECKPOINTS] == delta[WORK_LEDGER_REDERIVE_TYPE_QUERY_UNCACHED]);
    BUSTER_TEST(arguments, delta[WORK_LEDGER_SNAPSHOT_FRAME_PUSHES] != 0 &&
                               delta[WORK_LEDGER_SNAPSHOT_FRAME_BYTES] % delta[WORK_LEDGER_SNAPSHOT_FRAME_PUSHES] == 0);
    BUSTER_TEST(arguments, delta[WORK_LEDGER_REDERIVE_INITIALIZER_ELEMENTS] >= delta[WORK_LEDGER_REDERIVE_INITIALIZER_LITERAL_ELEMENTS] &&
                               delta[WORK_LEDGER_REDERIVE_INITIALIZER_LITERAL_ELEMENTS] >= 5);
    BUSTER_TEST(arguments, delta[WORK_LEDGER_POPULATION_MEMBER_PROMOTED_SEARCHES] != 0);
    BUSTER_TEST(arguments, delta[WORK_LEDGER_LITERAL_NUMBER_CONVERSIONS] != 0);
    BUSTER_TEST(arguments, delta[WORK_LEDGER_LOOKUP_SYMBOL_INTERNS] != 0 &&
                               delta[WORK_LEDGER_LOOKUP_SYMBOL_INTERN_PROBES] >= delta[WORK_LEDGER_LOOKUP_SYMBOL_INTERNS]);
    BUSTER_TEST(arguments, delta[WORK_LEDGER_MACHINE_FUNCTIONS_SELECTED] == 1 && delta[WORK_LEDGER_MACHINE_ROWS] != 0 &&
                               delta[WORK_LEDGER_MACHINE_ENCODED_BYTES] != 0);
    BUSTER_TEST(arguments, delta[WORK_LEDGER_OUTPUT_OBJECT_BYTES] != 0 && delta[WORK_LEDGER_OUTPUT_LINK_IMAGE_BYTES] == 0);
    WorkLedgerPhase entered[] = {WORK_LEDGER_PHASE_PREPROCESS, WORK_LEDGER_PHASE_PARSE, WORK_LEDGER_PHASE_SEMANTIC, WORK_LEDGER_PHASE_LOWER,
                                 WORK_LEDGER_PHASE_PREPARE, WORK_LEDGER_PHASE_TARGET_PREWARM, WORK_LEDGER_PHASE_OBJECT, WORK_LEDGER_PHASE_OUTPUT};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(entered); index += 1)
    {
        BUSTER_TEST(arguments, after.phases[entered[index]].marks == before.phases[entered[index]].marks + 1);
    }
    // Codegen is entered by the driver and again after the target prewarm.
    BUSTER_TEST(arguments, after.phases[WORK_LEDGER_PHASE_CODEGEN].marks == before.phases[WORK_LEDGER_PHASE_CODEGEN].marks + 2);
    u64 phase_calls = 0;
    u64 phase_bytes = 0;
    for (u32 phase = 0; phase < WORK_LEDGER_PHASE_COUNT; phase += 1)
    {
        phase_calls += after.phases[phase].arena_calls - before.phases[phase].arena_calls;
        phase_bytes += after.phases[phase].arena_bytes - before.phases[phase].arena_bytes;
    }
    BUSTER_TEST(arguments, phase_calls == arena_after.calls - arena_before.calls);
    BUSTER_TEST(arguments, phase_bytes == arena_after.requested_bytes - arena_before.requested_bytes);
    WorkLedgerCounters syntax_before = work_ledger_counters();
#endif
    CompilerDriverResult syntax = compiler_driver_execute_invocation(arena, syntax_invocation);
    BUSTER_TEST(arguments, syntax.error == COMPILER_DRIVER_ERROR_NONE);
#if BUSTER_BENCH_ALLOCATIONS
    WorkLedgerCounters syntax_after = work_ledger_counters();
    WorkLedgerCounter untouched[] = {WORK_LEDGER_REDERIVE_LOWER_QUERY_ROOTS, WORK_LEDGER_MACHINE_FUNCTIONS_SELECTED, WORK_LEDGER_MACHINE_ROWS,
                                     WORK_LEDGER_MACHINE_ENCODED_BYTES, WORK_LEDGER_OUTPUT_OBJECT_BYTES};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(untouched); index += 1)
    {
        BUSTER_TEST(arguments, syntax_after.values[untouched[index]] == syntax_before.values[untouched[index]]);
    }
    BUSTER_TEST(arguments, syntax_after.values[WORK_LEDGER_REDERIVE_TYPE_QUERY_ROOTS] > syntax_before.values[WORK_LEDGER_REDERIVE_TYPE_QUERY_ROOTS]);
    BUSTER_TEST(arguments, syntax_after.phases[WORK_LEDGER_PHASE_LOWER].marks == syntax_before.phases[WORK_LEDGER_PHASE_LOWER].marks);
    BUSTER_TEST(arguments, syntax_after.phases[WORK_LEDGER_PHASE_SEMANTIC].marks == syntax_before.phases[WORK_LEDGER_PHASE_SEMANTIC].marks + 1);
#endif
    scratch_end(temporary);
    return result;
}

// #1601: a function whose declarator returns a function pointer, declared and
// then defined, gave its RETURN rows and its direct calls a return type the
// function's own signature does not name. -fverify-codegen rejected the unit,
// and the default producer-certified path silently declined FAST for all of it
// (#1602). Plain, static, qualified-return and two-level shapes, in both
// frontend forms: the strict validator accepts the unit, FAST reaches every
// function codegen receives, and every allocator's executable agrees.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_function_pointer_return_redeclarations(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = temporary.arena;
    String8 input = buster_test_temporary_path(arena, S8("buster-function-pointer-return"), S8(".c"));
    BUSTER_TEST(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(S8(
        "static int twice(int x) { return 2 * x; }\n"
        "static int thrice(int x) { return 3 * x; }\n"
        "static int (*pick(int))(int);\n"
        "int early(int v) { return pick(0)(v); }\n"
        "static int (*pick(int which))(int) { return which ? twice : thrice; }\n"
        "int (*pick_extern(int))(int);\n"
        "int (*pick_extern(int which))(int) { return which ? thrice : twice; }\n"
        "static int (*const pick_const(int))(int);\n"
        "static int (*const pick_const(int which))(int) { return which ? twice : 0; }\n"
        "static int (*volatile pick_volatile(int))(int);\n"
        "static int (*volatile pick_volatile(int which))(int) { return which ? thrice : 0; }\n"
        "static int (*level1(int which))(int) { return which ? twice : thrice; }\n"
        "static int (*(*level2(int))(int))(int);\n"
        "static int (*(*level2(int which))(int))(int) { return which ? level1 : 0; }\n"
        "int main(void)\n"
        "{\n"
        "    int (*(*outer)(int))(int) = level2(1);\n"
        "    return early(5) != 15 || pick(1)(5) != 10 || pick_extern(1)(4) != 12 || pick_const(1)(6) != 12 ||\n"
        "           pick_volatile(1)(2) != 6 || outer(0)(7) != 21;\n"
        "}\n"))));
    String8 forms[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
    for (u32 form = 0; form < BUSTER_ARRAY_LENGTH(forms); form += 1)
    {
        // The default path trusts the producer and is where the decline was
        // silent; -fverify-codegen validates the input before anything runs.
        for (u32 verify = 0; verify < 2; verify += 1)
        {
            String8 object = buster_test_temporary_path(arena,
                string_format(arena, S8("buster-function-pointer-return-{u32}-{u32}"), form, verify), S8(".o"));
            String8 command[7];
            u32 count = 0;
            command[count++] = S8("-nostdinc");
            command[count++] = forms[form];
            if (verify) command[count++] = S8("-fverify-codegen");
            command[count++] = S8("-c");
            command[count++] = S8("-o");
            command[count++] = object;
            command[count++] = input;
            CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8){command, count});
            BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.fast_passes == IR_FAST_ALL);
            CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, invocation);
            if (compiled.error != COMPILER_DRIVER_ERROR_NONE)
            {
                arguments->show(arguments, S8("function-pointer return form={u32} verify={u32}: {S8}\n"), form, verify, compiled.diagnostic);
            }
            BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object);
            BUSTER_TEST(arguments, compiled.fast.validation_skips == 0 && compiled.fast.functions != 0 &&
                                   compiled.fast.functions == compiled.codegen_statistics.function_count);
        }
#if !BUSTER_ANDROID && !BUSTER_IOS
        // Mobile tests run in an application process that cannot launch the
        // generated executables; the object checks above still run there.
        String8 modes[] = {S8("fast"), S8("quality")};
        for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
        {
            String8 executable = buster_test_temporary_path(arena,
                string_format(arena, S8("buster-function-pointer-return-{u32}-{S8}"), form, modes[mode]), S8(".exe"));
            String8 command[] = {S8("-nostdinc"), forms[form], string_format(arena, S8("-fregister-allocator={S8}"), modes[mode]),
                                 S8("-o"), executable, input};
            CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
            BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE);
            CompilerDriverResult linked = compiler_driver_execute_invocation(arena, invocation);
            if (linked.error != COMPILER_DRIVER_ERROR_NONE)
            {
                arguments->show(arguments, S8("function-pointer return form={u32} mode={S8}: {S8}\n"), form, modes[mode], linked.diagnostic);
            }
            BUSTER_TEST(arguments, linked.error == COMPILER_DRIVER_ERROR_NONE && !linked.codegen_statistics.fallback_function_count);
            if (linked.error == COMPILER_DRIVER_ERROR_NONE)
            {
                String8 run[] = {executable};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                                                           (ProcessSpawnOptions){.use_process_environment = 1, .search_path = 1});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, 30000000);
                    BUSTER_TEST(arguments, !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS);
                }
            }
        }
#endif
    }
    scratch_end(temporary);
    return result;
}

// Static initializers sized by the number of elements they list rather than by
// the nesting of the type they initialize (#2527). Each source is generated
// here, compiled through the driver for a fixed ELF target, and read back
// from the object with expectations computed independently of the compiler.
typedef struct CompilerDriverInitializerSource CompilerDriverInitializerSource;
struct CompilerDriverInitializerSource
{
    u8* bytes;
    u64 length;
    u64 capacity;
};

BUSTER_GLOBAL_LOCAL void compiler_driver_test_source_text(CompilerDriverInitializerSource* source, String8 text)
{
    if (text.length <= source->capacity - source->length)
    {
        memcpy(source->bytes + source->length, text.pointer, text.length);
        source->length += text.length;
    }
    return;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_test_source_number(CompilerDriverInitializerSource* source, u32 value)
{
    char8 digits[10];
    u32 count = 0;
    do
    {
        digits[count++] = (char8)('0' + value % 10);
        value /= 10;
    } while (value);
    String8 reversed = {.pointer = digits, .length = count};
    for (u32 index = 0; index < count / 2; index += 1)
    {
        char8 swap = digits[index];
        digits[index] = digits[count - 1 - index];
        digits[count - 1 - index] = swap;
    }
    compiler_driver_test_source_text(source, reversed);
    return;
}

BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_test_compile_initializer_source(Arena* arena, CompilerDriverInitializerSource const* source)
{
    String8 input = buster_test_temporary_path(arena, S8("buster-large-initializer"), S8(".c"));
    String8 output = buster_test_temporary_path(arena, S8("buster-large-initializer"), S8(".o"));
    CompilerDriverResult compiled = {0};
    compiled.error = COMPILER_DRIVER_ERROR_FILE_READ;
    if (file_write(input, (ByteSlice){.pointer = source->bytes, .length = source->length}))
    {
        String8 command[] = {S8("-c"), S8("-nostdinc"), S8("-g0"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("-o"), output, input};
        compiled = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
        os_file_delete(output);
    }
    os_file_delete(input);
    return compiled;
}

// The data bytes of a defined object, or an empty slice.
BUSTER_GLOBAL_LOCAL ByteSlice compiler_driver_test_data_symbol(ObjectFile* object, String8 name, u64 size, u64* offset_out)
{
    ByteSlice bytes = {0};
    ObjectSymbol* symbol = compiler_driver_test_symbol_by_name(object, name);
    if (symbol && symbol->section == OBJECT_SECTION_DATA && symbol->size == size && object->sections[OBJECT_SECTION_DATA].data.length >= symbol->value &&
        size <= object->sections[OBJECT_SECTION_DATA].data.length - symbol->value)
    {
        bytes = (ByteSlice){.pointer = object->sections[OBJECT_SECTION_DATA].data.pointer + symbol->value, .length = size};
        *offset_out = symbol->value;
    }
    return bytes;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_large_static_initializers(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // The fixture arena is too small for a few megabytes of source plus the
    // object each compile returns, so this fixture owns a reservation.
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(4), .flags = {.no_pool = true}});
    BUSTER_TEST(arguments, arena != 0);
    if (!arena)
    {
        return result;
    }
    u64 const capacity = BUSTER_MB(16);
    CompilerDriverInitializerSource source = {.bytes = arena_allocate(arena, u8, capacity), .capacity = capacity};
    TemporalArena round = arena_begin_temporal(arena);

    // 1,000,000 unsigned char elements, the length inferred from the list.
    u32 const blob_count = 1000000;
    compiler_driver_test_source_text(&source, S8("unsigned char blob[] = {"));
    for (u32 index = 0; index < blob_count; index += 1)
    {
        compiler_driver_test_source_number(&source, (index * 7 + 3) & 255);
        compiler_driver_test_source_text(&source, S8(","));
    }
    compiler_driver_test_source_text(&source, S8("};\n"));
    CompilerDriverResult compiled = compiler_driver_test_compile_initializer_source(arena, &source);
    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object, compiled.diagnostic);
    if (compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object)
    {
        u64 offset = 0;
        ByteSlice blob = compiler_driver_test_data_symbol(&compiled.object, S8("blob"), blob_count, &offset);
        if (BUSTER_REQUIRE(arguments, blob.pointer))
        {
            u64 expected_sum = 0;
            u64 actual_sum = 0;
            u32 mismatches = 0;
            for (u32 index = 0; index < blob_count; index += 1)
            {
                u8 expected = (u8)(index * 7 + 3);
                expected_sum += expected;
                actual_sum += blob.pointer[index];
                mismatches += blob.pointer[index] != expected;
            }
            BUSTER_TEST(arguments, mismatches == 0 && actual_sum == expected_sum);
        }
    }

    scratch_end(round);
    round = arena_begin_temporal(arena);
    // 250,000 string pointers: a two-type-deep initializer whose token count
    // used to size its working storage.
    u32 const string_count = 250000;
    source.length = 0;
    compiler_driver_test_source_text(&source, S8("const char *names[] = {"));
    for (u32 index = 0; index < string_count; index += 1)
    {
        compiler_driver_test_source_text(&source, S8("\"s"));
        compiler_driver_test_source_number(&source, index);
        compiler_driver_test_source_text(&source, S8("\","));
    }
    compiler_driver_test_source_text(&source, S8("};\n"));
    compiled = compiler_driver_test_compile_initializer_source(arena, &source);
    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object, compiled.diagnostic);
    if (compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object)
    {
        ObjectFile* object = &compiled.object;
        u64 offset = 0;
        ByteSlice table = compiler_driver_test_data_symbol(object, S8("names"), (u64)string_count * 8, &offset);
        if (BUSTER_REQUIRE(arguments, table.pointer))
        {
            u8* seen = arena_allocate_zeroed(arena, u8, string_count);
            u32 resolved = 0;
            u32 relocated = 0;
            for (u32 relocation_index = 0; relocation_index < object->relocation_count; relocation_index += 1)
            {
                ObjectRelocation* relocation = object->relocations + relocation_index;
                if (relocation->section == OBJECT_SECTION_DATA && relocation->offset >= offset && relocation->offset - offset < table.length)
                {
                    u64 slot = (relocation->offset - offset) / 8;
                    ObjectSymbol* target = relocation->symbol < object->symbol_count ? object->symbols + relocation->symbol : 0;
                    ByteSlice data = target ? object->sections[target->section].data : (ByteSlice){0};
                    u64 position = target ? target->value + (u64)relocation->addend : 0;
                    relocated += 1;
                    // The string the slot points at, compared with the one
                    // the generator wrote into that slot.
                    CompilerDriverInitializerSource expected = {.bytes = (u8*)arena_allocate(arena, char8, 16), .capacity = 16};
                    compiler_driver_test_source_text(&expected, S8("s"));
                    compiler_driver_test_source_number(&expected, (u32)slot);
                    bool matches = (relocation->offset - offset) % 8 == 0 && position <= data.length && expected.length + 1 <= data.length - position &&
                                   memcmp(data.pointer + position, expected.bytes, expected.length) == 0 && data.pointer[position + expected.length] == 0;
                    resolved += matches && !seen[slot];
                    seen[slot] = 1;
                }
            }
            BUSTER_TEST(arguments, relocated == string_count && resolved == string_count);
        }
    }

    scratch_end(round);
    round = arena_begin_temporal(arena);
    // 200,000 records, each holding an id and the address of an array element.
    u32 const record_count = 200000;
    source.length = 0;
    compiler_driver_test_source_text(&source, S8("int targets[16]; struct P { int id; int *target; };\nstruct P records[] = {"));
    for (u32 index = 0; index < record_count; index += 1)
    {
        compiler_driver_test_source_text(&source, S8("{"));
        compiler_driver_test_source_number(&source, index);
        compiler_driver_test_source_text(&source, S8(",&targets["));
        compiler_driver_test_source_number(&source, index % 16);
        compiler_driver_test_source_text(&source, S8("]},"));
    }
    compiler_driver_test_source_text(&source, S8("};\n"));
    compiled = compiler_driver_test_compile_initializer_source(arena, &source);
    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object, compiled.diagnostic);
    if (compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object)
    {
        ObjectFile* object = &compiled.object;
        u64 offset = 0;
        ByteSlice table = compiler_driver_test_data_symbol(object, S8("records"), (u64)record_count * 16, &offset);
        ObjectSymbol* targets = compiler_driver_test_symbol_by_name(object, S8("targets"));
        if (BUSTER_REQUIRE(arguments, table.pointer && targets))
        {
            u32 id_mismatches = 0;
            for (u32 index = 0; index < record_count; index += 1)
            {
                u32 id;
                memcpy(&id, table.pointer + (u64)index * 16, sizeof(id));
                id_mismatches += id != index;
            }
            u8* seen = arena_allocate_zeroed(arena, u8, record_count);
            u32 resolved = 0;
            u32 relocated = 0;
            for (u32 relocation_index = 0; relocation_index < object->relocation_count; relocation_index += 1)
            {
                ObjectRelocation* relocation = object->relocations + relocation_index;
                if (relocation->section == OBJECT_SECTION_DATA && relocation->offset >= offset && relocation->offset - offset < table.length)
                {
                    u64 slot = (relocation->offset - offset) / 16;
                    bool on_pointer = (relocation->offset - offset) % 16 == 8;
                    ObjectSymbol* target = relocation->symbol < object->symbol_count ? object->symbols + relocation->symbol : 0;
                    relocated += 1;
                    resolved += on_pointer && target == targets && relocation->addend == (s64)(slot % 16) * 4 && !seen[slot];
                    seen[slot] = 1;
                }
            }
            BUSTER_TEST(arguments, id_mismatches == 0 && relocated == record_count && resolved == record_count);
        }
    }

    scratch_end(round);
    round = arena_begin_temporal(arena);
    // A chain of forty structs, each holding the previous as its only member.
    // A one-token list reaches the innermost scalar through brace elision and
    // a designator names it through thirty-nine `.m` steps; neither depends on
    // how many tokens the initializer spells.
    u32 const chain_depth = 40;
    source.length = 0;
    compiler_driver_test_source_text(&source, S8("struct S0 { int x; };\n"));
    for (u32 level = 1; level < chain_depth; level += 1)
    {
        compiler_driver_test_source_text(&source, S8("struct S"));
        compiler_driver_test_source_number(&source, level);
        compiler_driver_test_source_text(&source, S8(" { struct S"));
        compiler_driver_test_source_number(&source, level - 1);
        compiler_driver_test_source_text(&source, S8(" m; };\n"));
    }
    compiler_driver_test_source_text(&source, S8("struct S39 elided = {1};\nstruct S39 pair[2] = {2, 3};\nstruct S39 designated = {"));
    for (u32 level = 1; level < chain_depth; level += 1)
    {
        compiler_driver_test_source_text(&source, S8(".m"));
    }
    compiler_driver_test_source_text(&source, S8(".x = 4};\nstruct S39 zero = {0};\n"
        "int cube[2][2][2] = {1, 2, 3, 4, 5, 6, 7, 8};\n"
        "struct S0 ranged[3][2] = {[0 ... 2] = {[0 ... 1] = {5}}};\n"));
    compiled = compiler_driver_test_compile_initializer_source(arena, &source);
    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object, compiled.diagnostic);
    if (compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object)
    {
        struct { String8 name; u32 count; u32 expected[8]; } cases[] = {
            {S8("elided"), 1, {1}}, {S8("pair"), 2, {2, 3}}, {S8("designated"), 1, {4}},
            {S8("cube"), 8, {1, 2, 3, 4, 5, 6, 7, 8}}, {S8("ranged"), 6, {5, 5, 5, 5, 5, 5}},
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
        {
            u64 offset = 0;
            ByteSlice data = compiler_driver_test_data_symbol(&compiled.object, cases[index].name, (u64)cases[index].count * 4, &offset);
            if (BUSTER_REQUIRE(arguments, data.pointer))
            {
                BUSTER_TEST(arguments, memory_compare(data.pointer, cases[index].expected, data.length));
            }
        }
    }

    // The neighbouring invalid spellings fail with a diagnostic instead of
    // being cut short by the depth-sized working storage.
    String8 invalid[] = {
        S8("struct S39 bad = {1, 2};\n"),
        S8("struct S39 bad = {.nope = 1};\n"),
        S8("struct S39 bad = {[0] = 1};\n"),
        S8("struct S39 bad[2] = {1, 2, 3};\n"),
        S8("int bad[2][2] = {[0 ... 2] = {1}};\n"),
        S8("struct S0 bad[2][2] = {[0 ... 1] = {[0 ... 2] = {1}}};\n"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1)
    {
        source.length = 0;
        compiler_driver_test_source_text(&source, S8("struct S0 { int x; };\n"));
        for (u32 level = 1; level < chain_depth; level += 1)
        {
            compiler_driver_test_source_text(&source, S8("struct S"));
            compiler_driver_test_source_number(&source, level);
            compiler_driver_test_source_text(&source, S8(" { struct S"));
            compiler_driver_test_source_number(&source, level - 1);
            compiler_driver_test_source_text(&source, S8(" m; };\n"));
        }
        compiler_driver_test_source_text(&source, invalid[index]);
        compiled = compiler_driver_test_compile_initializer_source(arena, &source);
        BUSTER_TEST(arguments, compiled.error != COMPILER_DRIVER_ERROR_NONE && compiled.diagnostic.length != 0);
    }
    scratch_end(round);
    BUSTER_TEST(arguments, arena_destroy(arena, 1));
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_fast(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_test_local_sizeof_static_asserts);
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_test_work_ledger);
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_test_positional_languages);
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_test_function_pointer_return_redeclarations);
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_test_large_static_initializers);
    String8 default_command[] = {S8("source.c")};
    CompilerDriverInvocation default_invocation = compiler_driver_parse_arguments(arguments->arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(default_command));
    BUSTER_TEST(arguments, default_invocation.error == COMPILER_DRIVER_ERROR_NONE && default_invocation.fast_passes == IR_FAST_ALL);
    String8 disabled_command[] = {S8("-fcanonical-fast"), S8("-fno-canonical-fast"), S8("source.c")};
    CompilerDriverInvocation disabled_invocation = compiler_driver_parse_arguments(arguments->arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(disabled_command));
    BUSTER_TEST(arguments, disabled_invocation.error == COMPILER_DRIVER_ERROR_NONE && disabled_invocation.fast_passes == 0);
    String8 modes[] = {S8("fast"), S8("quality")};
    u32 native_count = BUSTER_ARRAY_LENGTH(modes);
    for (u32 backend = 0; backend < native_count + 3; backend += 1)
    {
        for (u32 mask = 0; mask <= IR_FAST_ALL; mask += 1)
        {
            TemporalArena temporary = arena_begin_temporal(arguments->arena);
            Arena* arena = temporary.arena;
            String8 path = buster_test_temporary_path(arena, S8("buster-canonical-fast"),
                backend < native_count && !BUSTER_ANDROID && !BUSTER_IOS ? S8(".exe") : S8(".artifact"));
            String8 command[16];
            u32 count = 0;
            command[count++] = S8("-nostdinc");
            command[count++] = S8("-o");
            command[count++] = path;
            command[count++] = backend < native_count ? S8("tests/basic_c_canonical_fast.c") : S8("tests/basic_c_canonical_fast_scalar.c");
            if (backend < native_count)
            {
                command[count++] = string_format(arena, S8("-fregister-allocator={S8}"), modes[backend]);
#if BUSTER_ANDROID || BUSTER_IOS
                // Mobile tests run in an application process. They cannot
                // launch generated executables, but still validate every
                // native subset through machine selection and object writing.
                command[count++] = S8("-c");
#endif
            }
            else if (backend < native_count + 2)
            {
                command[count++] = S8("-target");
                command[count++] = backend == native_count ? S8("wasm64-unknown-freestanding") : S8("bpfel-unknown-linux");
            }
            else
            {
                command[count++] = S8("-emit-llvm");
                command[count++] = S8("-c");
            }
            command[count++] = S8("-fcanonical-fast");
            for (u32 pass = 0; pass < IR_FAST_PASS_COUNT; pass += 1)
            {
                if (!(mask & IR_FAST_PASS_BIT(pass))) command[count++] = string_format(arena, S8("-fno-canonical-fast-{S8}"), ir_fast_pass_name((IrFastPass)pass));
            }
            CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8){command, count});
            BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.fast_passes == mask);
            CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, invocation);
            if (compiled.error != COMPILER_DRIVER_ERROR_NONE) arguments->show(arguments, S8("FAST backend={u32} mask={u32}: {S8}\n"), backend, mask, compiled.diagnostic);
            BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
            if (compiled.error == COMPILER_DRIVER_ERROR_NONE)
            {
                if (backend < native_count)
                {
                    BUSTER_TEST(arguments, !compiled.codegen_statistics.fallback_function_count);
#if !BUSTER_ANDROID && !BUSTER_IOS
                    String8 run[] = {path};
                    ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                                                               (ProcessSpawnOptions){.use_process_environment = 1, .search_path = 1});
                    BUSTER_TEST(arguments, spawn.handle != 0);
                    if (spawn.handle)
                    {
                        ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, 30000000);
                        BUSTER_TEST(arguments, !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS);
                    }
#else
                    ByteSlice object = file_read(arena, path, (FileReadOptions){0});
                    BUSTER_TEST(arguments, compiled.has_object && object.length >= 8);
#endif
                }
                else
                {
                    ByteSlice bytes = file_read(arena, path, (FileReadOptions){0});
                    BUSTER_TEST(arguments, bytes.length >= 8);
                    if (backend == native_count) BUSTER_TEST(arguments, compiled.has_wasm64 && bytes.length >= 8 && memcmp(bytes.pointer, "\0asm\1\0\0\0", 8) == 0);
                    else if (backend == native_count + 1)
                    {
                        BUSTER_TEST(arguments, compiled.has_ebpf);
                        u64 values[] = {0, 1, 0xffffffffu, UINT64_MAX};
                        for (u32 input = 0; input < BUSTER_ARRAY_LENGTH(values); input += 1)
                        {
                            u64 actual = 0;
                            BUSTER_TEST(arguments, codegen_test_ebpf_execute(bytes, values[input], input & 1, &actual));
                            BUSTER_TEST(arguments, actual == values[input] + 7);
                        }
                    }
                    else BUSTER_TEST(arguments, bytes.length >= 4 && memcmp(bytes.pointer, "BC\xc0\xde", 4) == 0);
                }
            }
            scratch_end(temporary);
        }
    }
    String8 toggle[] = {S8("-fcanonical-fast"), S8("-fno-canonical-fast"), S8("-fcanonical-fast-fold"), S8("-ftime-canonical-fast"), S8("source.c")};
    CompilerDriverInvocation parsed = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(toggle));
    BUSTER_TEST(arguments, parsed.error == COMPILER_DRIVER_ERROR_NONE && parsed.fast_passes == IR_FAST_PASS_BIT(IR_FAST_FOLD) && parsed.measure_fast_passes);
    return result;
}
