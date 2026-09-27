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

// Static assertions containing local-object sizeof operands must agree in the
// semantic-only and object actions, including both frontend SSA forms.
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
            "}\n"), false, S8("static assertion expression is not an integer constant expression")},
        {S8("int f(int n) {\n"
            "    int values[n];\n"
            "    _Static_assert(sizeof values == n * sizeof(int), \"variable array\");\n"
            "    return (int)sizeof values;\n"
            "}\n"), false, S8("static assertion expression is not an integer constant expression")},
        {S8("int f(void) {\n"
            "    char value;\n"
            "    _Static_assert(sizeof value == 2, \"false\");\n"
            "    return 0;\n"
            "}\n"), false, S8("static assertion expression is not a true integer constant expression")},
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

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_fast(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_test_local_sizeof_static_asserts);
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_test_positional_languages);
    String8 default_command[] = {S8("source.c")};
    CompilerDriverInvocation default_invocation = compiler_driver_parse_arguments(arguments->arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(default_command));
    BUSTER_TEST(arguments, default_invocation.error == COMPILER_DRIVER_ERROR_NONE && default_invocation.fast_passes == IR_FAST_ALL);
    String8 disabled_command[] = {S8("-fcanonical-fast"), S8("-fno-canonical-fast"), S8("source.c")};
    CompilerDriverInvocation disabled_invocation = compiler_driver_parse_arguments(arguments->arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(disabled_command));
    BUSTER_TEST(arguments, disabled_invocation.error == COMPILER_DRIVER_ERROR_NONE && disabled_invocation.fast_passes == 0);
    String8 modes[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
    for (u32 backend = 0; backend < 7; backend += 1)
    {
        for (u32 mask = 0; mask <= IR_FAST_ALL; mask += 1)
        {
            TemporalArena temporary = arena_begin_temporal(arguments->arena);
            Arena* arena = temporary.arena;
            String8 path = buster_test_temporary_path(arena, S8("buster-canonical-fast"),
                backend < 4 && !BUSTER_ANDROID && !BUSTER_IOS ? S8(".exe") : S8(".artifact"));
            String8 command[16];
            u32 count = 0;
            command[count++] = S8("-nostdinc");
            command[count++] = S8("-o");
            command[count++] = path;
            command[count++] = backend < 4 ? S8("tests/basic_c_canonical_fast.c") : S8("tests/basic_c_canonical_fast_scalar.c");
            if (backend < 4)
            {
                command[count++] = string_format(arena, S8("-fregister-allocator={S8}"), modes[backend]);
#if BUSTER_ANDROID || BUSTER_IOS
                // Mobile tests run in an application process. They cannot
                // launch generated executables, but still validate every
                // native subset through machine selection and object writing.
                command[count++] = S8("-c");
#endif
            }
            else if (backend < 6)
            {
                command[count++] = S8("-target");
                command[count++] = backend == 4 ? S8("wasm64-unknown-freestanding") : S8("bpfel-unknown-linux");
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
                if (backend < 4)
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
                    if (backend == 4) BUSTER_TEST(arguments, compiled.has_wasm64 && bytes.length >= 8 && memcmp(bytes.pointer, "\0asm\1\0\0\0", 8) == 0);
                    else if (backend == 5)
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
