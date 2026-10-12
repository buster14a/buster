// Included by driver_test.c: pass-through options must change the requested
// artifact or fail before publication. Parse checks cover every native target;
// Linux image checks independently inspect SONAME and dynamic exports.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_pass_through_arguments(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = temporary.arena;
    String8 linker_verbose_wl[] = {S8("--target=x86_64-linux-gnu"), S8("-Wl,-v"), S8("a.o"), S8("-o"), S8("out")};
    CompilerDriverInvocation linker_verbose = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(linker_verbose_wl));
    BUSTER_TEST(arguments, linker_verbose.error == COMPILER_DRIVER_ERROR_NONE && linker_verbose.linker_verbose &&
                               linker_verbose.linker_argument_count == 0);
    String8 linker_verbose_xlinker[] = {S8("--target=x86_64-linux-gnu"), S8("-Xlinker"), S8("-v"), S8("a.o"), S8("-o"), S8("out")};
    CompilerDriverInvocation linker_verbose_split = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(linker_verbose_xlinker));
    BUSTER_TEST(arguments, linker_verbose_split.error == COMPILER_DRIVER_ERROR_NONE && linker_verbose_split.linker_verbose &&
                               linker_verbose_split.linker_argument_count == 0);
    String8 linker_verbose_remaining[] = {S8("--target=x86_64-linux-gnu"), S8("-Wl,-v,--no-undefined"), S8("a.o")};
    CompilerDriverInvocation linker_verbose_with_argument =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(linker_verbose_remaining));
    BUSTER_TEST(arguments, linker_verbose_with_argument.error == COMPILER_DRIVER_ERROR_NONE &&
                               linker_verbose_with_argument.linker_verbose && linker_verbose_with_argument.linker_argument_count == 1);
    if (linker_verbose_with_argument.linker_argument_count == 1)
    {
        BUSTER_STRING_TEST(arguments, linker_verbose_with_argument.linker_arguments[0], S8("--no-undefined"));
    }
    String8 version_script_verbose_operand[] = {S8("--target=x86_64-linux-gnu"), S8("a.o"),
                                                S8("-Wl,--version-script,-v"), S8("-o"), S8("out")};
    CompilerDriverInvocation version_script_verbose =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(version_script_verbose_operand));
    BUSTER_TEST(arguments, version_script_verbose.error == COMPILER_DRIVER_ERROR_NONE &&
                               !version_script_verbose.linker_verbose && version_script_verbose.linker_argument_count == 2);
    if (version_script_verbose.linker_argument_count == 2)
    {
        BUSTER_STRING_TEST(arguments, version_script_verbose.linker_arguments[0], S8("--version-script"));
        BUSTER_STRING_TEST(arguments, version_script_verbose.linker_arguments[1], S8("-v"));
    }
    String8 version_script_xlinker_operand[] = {S8("--target=x86_64-linux-gnu"), S8("a.o"), S8("-Xlinker"),
                                                S8("--version-script"), S8("-Xlinker"), S8("-v"), S8("-o"), S8("out")};
    CompilerDriverInvocation version_script_xlinker =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(version_script_xlinker_operand));
    BUSTER_TEST(arguments, version_script_xlinker.error == COMPILER_DRIVER_ERROR_NONE &&
                               !version_script_xlinker.linker_verbose && version_script_xlinker.linker_argument_count == 2);
    if (version_script_xlinker.linker_argument_count == 2)
    {
        BUSTER_STRING_TEST(arguments, version_script_xlinker.linker_arguments[0], S8("--version-script"));
        BUSTER_STRING_TEST(arguments, version_script_xlinker.linker_arguments[1], S8("-v"));
    }
    String8 version_script_verbose_trailing[] = {S8("--target=x86_64-linux-gnu"), S8("a.o"),
                                                 S8("-Wl,--version-script,-v,-v"), S8("-o"), S8("out")};
    CompilerDriverInvocation version_script_verbose_with_trailing =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(version_script_verbose_trailing));
    BUSTER_TEST(arguments, version_script_verbose_with_trailing.error == COMPILER_DRIVER_ERROR_NONE &&
                               version_script_verbose_with_trailing.linker_verbose &&
                               version_script_verbose_with_trailing.linker_argument_count == 2);
    if (version_script_verbose_with_trailing.linker_argument_count == 2)
    {
        BUSTER_STRING_TEST(arguments, version_script_verbose_with_trailing.linker_arguments[0], S8("--version-script"));
        BUSTER_STRING_TEST(arguments, version_script_verbose_with_trailing.linker_arguments[1], S8("-v"));
    }
    String8 soname_verbose_operand[] = {S8("--target=x86_64-linux-gnu"), S8("-shared"), S8("-Wl,-soname,-v"), S8("a.o")};
    CompilerDriverInvocation soname_verbose =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(soname_verbose_operand));
    BUSTER_TEST(arguments, soname_verbose.error == COMPILER_DRIVER_ERROR_NONE && !soname_verbose.linker_verbose &&
                               soname_verbose.linker_argument_count == 2);
    if (soname_verbose.linker_argument_count == 2)
    {
        BUSTER_STRING_TEST(arguments, soname_verbose.linker_arguments[0], S8("-soname"));
        BUSTER_STRING_TEST(arguments, soname_verbose.linker_arguments[1], S8("-v"));
    }
    String8 z_verbose_operand[] = {S8("--target=x86_64-linux-gnu"), S8("-Wl,-z,-v"), S8("a.o")};
    CompilerDriverInvocation z_verbose = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(z_verbose_operand));
    BUSTER_TEST(arguments, z_verbose.error == COMPILER_DRIVER_ERROR_ARGUMENT && !z_verbose.linker_verbose &&
                               z_verbose.linker_argument_count == 2);
    if (z_verbose.linker_argument_count == 2)
    {
        BUSTER_STRING_TEST(arguments, z_verbose.linker_arguments[0], S8("-z"));
        BUSTER_STRING_TEST(arguments, z_verbose.linker_arguments[1], S8("-v"));
    }
    String8 windows_verbose[] = {S8("--target=x86_64-windows"), S8("-Wl,-v"), S8("a.o")};
    CompilerDriverInvocation windows_linker_verbose =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(windows_verbose));
    BUSTER_TEST(arguments, windows_linker_verbose.error == COMPILER_DRIVER_ERROR_ARGUMENT &&
                               compiler_driver_test_string_contains(windows_linker_verbose.diagnostic, S8("-v")));
    String8 compile_verbose[] = {S8("--target=x86_64-linux-gnu"), S8("-c"), S8("-Wl,-v"), S8("a.c")};
    CompilerDriverInvocation compile_linker_verbose =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(compile_verbose));
    BUSTER_TEST(arguments, compile_linker_verbose.error == COMPILER_DRIVER_ERROR_ARGUMENT && !compile_linker_verbose.linker_verbose &&
                               compiler_driver_test_string_contains(compile_linker_verbose.diagnostic, S8("-v")));
    String8 query_verbose[] = {S8("--target=x86_64-linux-gnu"), S8("-Wl,--version,-v")};
    CompilerDriverInvocation query_linker_verbose =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(query_verbose));
    BUSTER_TEST(arguments, query_linker_verbose.error == COMPILER_DRIVER_ERROR_ARGUMENT && !query_linker_verbose.linker_verbose &&
                               query_linker_verbose.linker_argument_count == 2);
    String8 report_arguments[] = {S8("--target=x86_64-linux-gnu"), S8("-L/opt/x"), S8("-lm"), S8("a.o"), S8("b.a"),
                                  S8("-o"), S8("out"), S8("-v")};
    CompilerDriverInvocation report_invocation =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(report_arguments));
    String8 expected_report = S8(" buster-native-ld -o out -L/opt/x -L/lib/x86_64-linux-gnu -L/usr/lib/x86_64-linux-gnu "
                                 "-L/usr/x86_64-linux-gnu/lib -L/lib64 -L/usr/lib64 -L/lib -L/usr/lib a.o b.a -lm -lc\n");
    BUSTER_TEST(arguments, report_invocation.error == COMPILER_DRIVER_ERROR_NONE && report_invocation.verbose);
    BUSTER_STRING_TEST(arguments, compiler_driver_native_link_report(arena, report_invocation), expected_report);
    String8 sysroot_report_arguments[] = {S8("--target=aarch64-linux-gnu"), S8("--sysroot=/sr"), S8("-L"), S8("/opt/arm lib"),
                                          S8("-lm"), S8("a.o"), S8("-o"), S8("out"), S8("-v")};
    CompilerDriverInvocation sysroot_report_invocation =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(sysroot_report_arguments));
    String8 expected_sysroot_report =
        S8(" buster-native-ld -o out '-L/opt/arm lib' -L/sr/lib/aarch64-linux-gnu -L/sr/usr/lib/aarch64-linux-gnu "
           "-L/sr/lib64 -L/sr/usr/lib64 -L/sr/lib -L/sr/usr/lib a.o -lm -lc\n");
    BUSTER_TEST(arguments, sysroot_report_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_STRING_TEST(arguments, compiler_driver_native_link_report(arena, sysroot_report_invocation), expected_sysroot_report);
    String8 quoted_report_arguments[] = {S8("--target=x86_64-linux-gnu"), S8("a file.o"), S8("-o"), S8("out file"), S8("-v")};
    CompilerDriverInvocation quoted_report_invocation =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(quoted_report_arguments));
    String8 expected_quoted_report =
        S8(" buster-native-ld -o 'out file' -L/lib/x86_64-linux-gnu -L/usr/lib/x86_64-linux-gnu -L/usr/x86_64-linux-gnu/lib "
           "-L/lib64 -L/usr/lib64 -L/lib -L/usr/lib 'a file.o' -lc\n");
    BUSTER_TEST(arguments, quoted_report_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_STRING_TEST(arguments, compiler_driver_native_link_report(arena, quoted_report_invocation), expected_quoted_report);
    String8 identity_arguments[] = {S8("--target=x86_64-linux-gnu"), S8("-Wl,-v"), S8("a.o"), S8("-o"), S8("out")};
    CompilerDriverInvocation identity_invocation =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(identity_arguments));
    BUSTER_STRING_TEST(arguments, compiler_driver_native_link_report(arena, identity_invocation),
                       S8("Buster native linker\nTarget: x86_64-unknown-linux-gnu\n"));
    String8 both_report_arguments[] = {S8("--target=x86_64-linux-gnu"), S8("-Wl,-v"), S8("a.o"), S8("-o"), S8("out"), S8("-v")};
    CompilerDriverInvocation both_report_invocation =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(both_report_arguments));
    String8 expected_both_report = S8(" buster-native-ld -o out -L/lib/x86_64-linux-gnu -L/usr/lib/x86_64-linux-gnu "
                                      "-L/usr/x86_64-linux-gnu/lib -L/lib64 -L/usr/lib64 -L/lib -L/usr/lib a.o -lc\n"
                                      "Buster native linker\nTarget: x86_64-unknown-linux-gnu\n");
    BUSTER_STRING_TEST(arguments, compiler_driver_native_link_report(arena, both_report_invocation), expected_both_report);
    String8 libc_report_arguments[] = {S8("--target=x86_64-linux-gnu"), S8("-lc"), S8("-lm"), S8("a.o"), S8("-v")};
    CompilerDriverInvocation libc_report_invocation =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(libc_report_arguments));
    BUSTER_STRING_TEST(arguments, compiler_driver_native_link_report(arena, libc_report_invocation),
                       S8(" buster-native-ld -o a.out -L/lib/x86_64-linux-gnu -L/usr/lib/x86_64-linux-gnu "
                          "-L/usr/x86_64-linux-gnu/lib -L/lib64 -L/usr/lib64 -L/lib -L/usr/lib a.o -lm -lc\n"));
    String8 targets[] = {S8("x86_64-unknown-linux"), S8("aarch64-unknown-linux"), S8("x86_64-pc-windows-msvc"),
                         S8("aarch64-pc-windows-msvc"), S8("x86_64-apple-macos"), S8("aarch64-apple-macos"),
                         S8("x86_64-linux-android"), S8("aarch64-linux-android"), S8("x86_64-unknown-uefi")};
    String8 refused[] = {S8("--wrap=f"), S8("--defsym=x=1"), S8("-e"), S8("--whole-archive"), S8("--no-whole-archive"),
                         S8("-rpath"), S8("-runpath"), S8("-Bstatic"), S8("-Bdynamic"),
                         S8("--dynamic-linker=loader"), S8("-o"), S8("--this-is-bogus"), S8("/ENTRY:main"), S8("--gc-sections")};
    for (u32 target = 0; target < BUSTER_ARRAY_LENGTH(targets); target += 1)
    {
        for (u32 option = 0; option < BUSTER_ARRAY_LENGTH(refused); option += 1)
        {
            String8 joined = string_format_z(arena, S8("-Wl,{S8}"), refused[option]);
            String8 commands[][5] = {
                {S8("--target"), targets[target], joined, S8("tests/basic_c_multi_main.c"), S8("-g0")},
                {S8("--target"), targets[target], S8("-Xlinker"), refused[option], S8("tests/basic_c_multi_main.c")},
            };
            for (u32 spelling = 0; spelling < BUSTER_ARRAY_LENGTH(commands); spelling += 1)
            {
                CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(commands[spelling]));
                BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_ARGUMENT);
                BUSTER_TEST(arguments, compiler_driver_test_string_contains(invocation.diagnostic, refused[option]));
            }
        }
    }
    for (u32 target = 0; target < BUSTER_ARRAY_LENGTH(targets); target += 1)
    {
        String8 commands[][7] = {
            {S8("--target"), targets[target], S8("-Wl,--version-script=map"), S8("-g0"), S8("tests/basic_c_multi_main.c"), S8("-O0"), S8("-g0")},
            {S8("--target"), targets[target], S8("-Xlinker"), S8("--version-script"), S8("-Xlinker"), S8("map"), S8("tests/basic_c_multi_main.c")},
        };
        for (u32 spelling = 0; spelling < BUSTER_ARRAY_LENGTH(commands); spelling += 1)
        {
            CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(commands[spelling]));
            BUSTER_TEST(arguments, (invocation.error == COMPILER_DRIVER_ERROR_NONE) == (target == 0));
        }
    }
    String8 script_refusals[] = {S8("-Wl,--version-script"), S8("-Wl,--version-script="),
                                S8("-Wl,--version-script=a,--version-script=b")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(script_refusals); index += 1)
    {
        String8 command[] = {S8("--target=x86_64-unknown-linux"), script_refusals[index], S8("tests/basic_c_multi_main.c")};
        BUSTER_TEST(arguments, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)).error == COMPILER_DRIVER_ERROR_ARGUMENT);
    }
    String8 preprocessing[] = {S8("-Wp,-DFROM_WP"), S8("-Wp,-UFROM_WP"), S8("-Wp,-Iinclude"), S8("-Wp,"),
                              S8("-Wa,--this-is-bogus"), S8("-Wa,"), S8("-MF"), S8("-MT"),
                              S8("-MP"), S8("-M"), S8("-MM"), S8("-Xpreprocessor"), S8("-Xassembler")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(preprocessing); index += 1)
    {
        String8 command[] = {S8("-c"), preprocessing[index], S8("tests/basic_c_multi_main.c")};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_ARGUMENT);
        BUSTER_TEST(arguments, compiler_driver_test_string_contains(invocation.diagnostic, preprocessing[index]));
    }
    String8 malformed[] = {S8("-Wl,"), S8("-Wl,,--export-dynamic"), S8("-Wl,--export-dynamic,"),
                           S8("-Wl,--export-dynamic,,--no-undefined"), S8("-Wl,-soname"), S8("-Wl,--soname="),
                           S8("-Wl,-z"), S8("-Wl,-z,now"), S8("-Wl,-z,relro"), S8("-Wl,-z,execstack"), S8("-Wl,-z,noexecstack")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(malformed); index += 1)
    {
        String8 command[] = {S8("--target=x86_64-unknown-linux"), S8("-shared"), malformed[index], S8("tests/basic_c_multi_main.c")};
        BUSTER_TEST(arguments, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)).error == COMPILER_DRIVER_ERROR_ARGUMENT);
    }
    String8 supported[] = {S8("--target=x86_64-unknown-linux"), S8("-shared"),
                           S8("-Wl,--no-undefined,--export-dynamic,-soname,libexample.so,-z,defs,-E"), S8("tests/basic_c_multi_main.c")};
    CompilerDriverInvocation parsed = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(supported));
    BUSTER_TEST(arguments, parsed.error == COMPILER_DRIVER_ERROR_NONE);
    String8 expected[] = {S8("--no-undefined"), S8("--export-dynamic"), S8("-soname"), S8("libexample.so"), S8("-z"), S8("defs"), S8("-E")};
    if (BUSTER_REQUIRE(arguments, parsed.linker_argument_count == BUSTER_ARRAY_LENGTH(expected) && parsed.linker_arguments))
    {
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected); index += 1)
        {
            BUSTER_STRING_TEST(arguments, parsed.linker_arguments[index], expected[index]);
        }
    }
    String8 opaque[] = {S8("--target=x86_64-unknown-linux"), S8("-Xlinker"), S8("--no-undefined,--export-dynamic"), S8("tests/basic_c_multi_main.c")};
    CompilerDriverInvocation unsplit = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(opaque));
    BUSTER_TEST(arguments, unsplit.error == COMPILER_DRIVER_ERROR_ARGUMENT && unsplit.linker_argument_count == 1);
    BUSTER_TEST(arguments, compiler_driver_test_string_contains(unsplit.diagnostic, opaque[2]));
    String8 missing_value[] = {S8("-Xlinker")};
    CompilerDriverInvocation missing = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(missing_value));
    BUSTER_TEST(arguments, missing.error == COMPILER_DRIVER_ERROR_ARGUMENT && compiler_driver_test_string_contains(missing.diagnostic, S8("-Xlinker")));
    for (u32 target = 0; target < BUSTER_ARRAY_LENGTH(targets); target += 1)
    {
        String8 command[] = {S8("--target"), targets[target], S8("-Wl,--export-dynamic"), S8("tests/basic_c_multi_main.c")};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        bool supported_target = target < 2 || target == 6 || target == 7;
        BUSTER_TEST(arguments, (invocation.error == COMPILER_DRIVER_ERROR_NONE) == supported_target);
    }
    String8 separate[] = {S8("--target=x86_64-unknown-linux"), S8("-shared"), S8("-Xlinker"), S8("-soname"),
                          S8("-Xlinker"), S8("libexample.so"), S8("-Xlinker"), S8("-z"), S8("-Xlinker"), S8("defs"), S8("tests/basic_c_multi_main.c")};
    CompilerDriverInvocation paired = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(separate));
    BUSTER_TEST(arguments, paired.error == COMPILER_DRIVER_ERROR_NONE && paired.linker_argument_count == 4);
    String8 modes[] = {S8("-c"), S8("-S"), S8("-E"), S8("-fsyntax-only"), S8("-emit-llvm")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modes); index += 1)
    {
        String8 command[] = {S8("--target=x86_64-unknown-linux"), modes[index], S8("-Wl,--export-dynamic"), S8("tests/basic_c_multi_main.c")};
        BUSTER_TEST(arguments, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)).error == COMPILER_DRIVER_ERROR_ARGUMENT);
    }
    String8 export_flag[] = {S8("--export-dynamic")};
    NativeExecutableLinkOptions options = {.linker_arguments = export_flag, .linker_argument_count = 1};
    String8 unsupported = {0};
    BUSTER_TEST(arguments, !link_validate_linker_arguments(parsed.target, options, false, &unsupported));
    BUSTER_STRING_TEST(arguments, unsupported, export_flag[0]);
    String8 soname_flag[] = {S8("-soname"), S8("libexample.so")};
    options.linker_arguments = soname_flag;
    options.linker_argument_count = 2;
    BUSTER_TEST(arguments, !link_validate_linker_arguments(parsed.target, options, true, &unsupported));
    // Meson's linker query and the split spelling query the actual native
    // linker, with no C file. Missing source/stdin inputs must not be read.
    String8 probe_spellings[][2] = {{S8("-Wl,--version"), S8("-O0")}, {S8("-Xlinker"), S8("--version")}};
    for (u32 target = 0; target < BUSTER_ARRAY_LENGTH(targets); target += 1)
    {
        for (u32 spelling = 0; spelling < BUSTER_ARRAY_LENGTH(probe_spellings); spelling += 1)
        {
            String8 command[] = {S8("--target"), targets[target], probe_spellings[spelling][0], probe_spellings[spelling][1], S8("-g")};
            CompilerDriverInvocation query = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
            if (BUSTER_REQUIRE(arguments, query.error == COMPILER_DRIVER_ERROR_NONE && query.query == COMPILER_DRIVER_QUERY_LINKER_VERSION))
            {
                String8 identity = compiler_driver_query_text(arena, &query);
                BUSTER_TEST(arguments, string_starts_with_sequence(identity, S8("Buster native linker\nTarget: ")));
                BUSTER_TEST(arguments, !compiler_driver_test_string_contains(identity, S8("GNU")) &&
                                           !compiler_driver_test_string_contains(identity, S8("LLD")) &&
                                           !compiler_driver_test_string_contains(identity, S8("clang version")));
                CompilerDriverResult executed = compiler_driver_execute_invocation(arena, query);
                BUSTER_TEST(arguments, executed.error == COMPILER_DRIVER_ERROR_NONE && !executed.has_object &&
                                           !executed.native_link.executable.length);
                BUSTER_STRING_TEST(arguments, executed.output, identity);
            }
        }
    }
    String8 probe[] = {S8("--target=x86_64-unknown-linux"), S8("-march=x86-64"), S8("-Wl,--version"), S8("-O0"), S8("-g")};
    CompilerDriverInvocation meson = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(probe));
    BUSTER_TEST(arguments, meson.error == COMPILER_DRIVER_ERROR_NONE && meson.input_count == 0 &&
                               meson.query == COMPILER_DRIVER_QUERY_LINKER_VERSION);
    String8 output = buster_test_temporary_path(arena, S8("buster-linker-query"), S8(""));
    String8 sentinel = S8("linker query must preserve this output");
    BUSTER_TEST(arguments, file_write(output, BUSTER_SLICE_TO_BYTE_SLICE(sentinel)));
    String8 absent = string_format_z(arena, S8("{S8}.missing.c"), output);
    String8 with_input[] = {S8("--target=x86_64-unknown-linux"), S8("-Wl,--version"), S8("-o"), output, absent};
    CompilerDriverInvocation query = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(with_input));
    CompilerDriverResult executed = compiler_driver_execute_invocation(arena, query);
    BUSTER_TEST(arguments, executed.error == COMPILER_DRIVER_ERROR_NONE);
    ByteSlice preserved = file_read(arena, output, (FileReadOptions){0});
    BUSTER_STRING_TEST(arguments, ((String8){.pointer = (char8*)preserved.pointer, .length = preserved.length}), sentinel);
    String8 stdin_query[] = {S8("--target=x86_64-unknown-linux"), S8("-Wl,--version"), S8("-x"), S8("c"), S8("-")};
    CompilerDriverInvocation stdin_invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(stdin_query));
    BUSTER_TEST(arguments, compiler_driver_execute_invocation(arena, stdin_invocation).error == COMPILER_DRIVER_ERROR_NONE);
#if !BUSTER_ANDROID && !BUSTER_IOS
    // Observe the real CLI status/streams as well as the invocation API.
    String8 cli_output = {0}, cli_error = {0};
    String8 cli_queries[][5] = {
        {S8("--target=x86_64-unknown-linux"), S8("-Wl,--version"), S8("-O0"), S8("-g"), absent},
        {S8("--target=x86_64-unknown-linux"), S8("-Xlinker"), S8("--version"), S8("-x"), S8("c")},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cli_queries); index += 1)
    {
        BUSTER_TEST(arguments, compiler_driver_test_run_cc(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(cli_queries[index]),
                                                           &cli_output, &cli_error));
        BUSTER_STRING_TEST(arguments, cli_output, S8("Buster native linker\nTarget: x86_64-unknown-linux-gnu\n"));
        BUSTER_TEST(arguments, cli_error.length == 0);
    }
    BUSTER_TEST(arguments, compiler_driver_test_run_cc(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(stdin_query),
                                                       &cli_output, &cli_error));
    BUSTER_STRING_TEST(arguments, cli_output, S8("Buster native linker\nTarget: x86_64-unknown-linux-gnu\n"));
    BUSTER_TEST(arguments, cli_error.length == 0);
    String8 invalid_cli[] = {S8("--target=x86_64-unknown-linux"), S8("-c"), S8("-Wl,--version"), absent};
    BUSTER_TEST(arguments, !compiler_driver_test_run_cc(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_cli),
                                                        &cli_output, &cli_error));
    BUSTER_TEST(arguments, cli_output.length == 0 && compiler_driver_test_string_contains(cli_error, S8("native linker --version")));
#endif
    CompilerDriverInvocation api_query = {.target = meson.target, .query = COMPILER_DRIVER_QUERY_LINKER_VERSION};
    BUSTER_STRING_TEST(arguments, compiler_driver_execute_invocation(arena, api_query).output, compiler_driver_query_text(arena, &meson));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modes); index += 1)
    {
        String8 command[] = {S8("--target=x86_64-unknown-linux"), modes[index], S8("-Wl,--version")};
        CompilerDriverInvocation refused_query = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        BUSTER_TEST(arguments, refused_query.error == COMPILER_DRIVER_ERROR_ARGUMENT);
        BUSTER_TEST(arguments, compiler_driver_execute_invocation(arena, refused_query).error == COMPILER_DRIVER_ERROR_ARGUMENT);
    }
    String8 query_refusals[][3] = {
        {S8("--target=x86_64-unknown-linux"), S8("-Wl,--version,--gc-sections"), S8("-O0")},
        {S8("--target=x86_64-unknown-linux"), S8("-Wl,--gc-sections,--version"), S8("-O0")},
        {S8("--target=x86_64-unknown-linux"), S8("-Wl,--version"), S8("--version")},
        {S8("--target=wasm64-unknown-unknown"), S8("-Wl,--version"), S8("-O0")},
        {S8("--target=nvptx64-nvidia-cuda"), S8("-Wl,--version"), S8("-O0")},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(query_refusals); index += 1)
    {
        CompilerDriverInvocation refused_query = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(query_refusals[index]));
        BUSTER_TEST(arguments, refused_query.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    }
    api_query.action = COMPILER_DRIVER_ACTION_OBJECT;
    BUSTER_TEST(arguments, compiler_driver_execute_invocation(arena, api_query).error == COMPILER_DRIVER_ERROR_ARGUMENT);
    api_query.action = COMPILER_DRIVER_ACTION_LINK;
    api_query.linker_arguments = export_flag;
    api_query.linker_argument_count = 1;
    BUSTER_TEST(arguments, compiler_driver_execute_invocation(arena, api_query).error == COMPILER_DRIVER_ERROR_ARGUMENT);
    // --version as a SONAME value is not a query and still produces a library.
    String8 soname_query[] = {S8("--target=x86_64-unknown-linux"), S8("-shared"), S8("-Wl,-soname,--version"),
                             S8("tests/basic_c_multi_main.c")};
    CompilerDriverInvocation soname = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(soname_query));
    BUSTER_TEST(arguments, soname.error == COMPILER_DRIVER_ERROR_NONE && soname.query == COMPILER_DRIVER_QUERY_NONE);
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_pass_through_depfiles(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = temporary.arena;
    String8 directory = buster_test_temporary_path(arena, S8("buster-pass-through-deps"), S8(""));
    os_make_directory(directory);
    String8 source = string_format_z(arena, S8("{S8}/main.c"), directory);
    String8 middle = string_format_z(arena, S8("{S8}/middle.h"), directory);
    String8 header = string_format_z(arena, S8("{S8}/leaf.h"), directory);
    String8 depfile = string_format_z(arena, S8("{S8}/main.d"), directory);
    String8 output = string_format_z(arena, S8("{S8}/main.o"), directory);
    String8 sentinel = S8("existing object must survive a refused request");
    BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(S8("#include \"middle.h\"\nint answer(void) { return VALUE; }\n"))));
    BUSTER_TEST(arguments, file_write(middle, BUSTER_SLICE_TO_BYTE_SLICE(S8("#include \"leaf.h\"\n"))));
    BUSTER_TEST(arguments, file_write(output, BUSTER_SLICE_TO_BYTE_SLICE(sentinel)));
    for (u32 revision = 0; revision < 2; revision += 1)
    {
        String8 contents = revision ? S8("#define VALUE 2\n") : S8("#define VALUE 1\n");
        BUSTER_TEST(arguments, file_write(header, BUSTER_SLICE_TO_BYTE_SLICE(contents)));
        String8 flags[] = {string_format_z(arena, S8("-Wp,-MD,{S8},-MP"), depfile), string_format_z(arena, S8("-Wp,-MMD,{S8},-DFROM_WP"), depfile),
                           S8("-Wp,-DFROM_WP")};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(flags); index += 1)
        {
            String8 command[] = {S8("-c"), flags[index], S8("-o"), output, source};
            CompilerDriverResult refused = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
            BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_ARGUMENT);
            BUSTER_TEST(arguments, compiler_driver_test_string_contains(refused.diagnostic, flags[index]));
            if (BUSTER_REQUIRE(arguments, refused.diagnostic_count == 1 && refused.diagnostics))
            {
                BUSTER_STRING_TEST(arguments, refused.diagnostics[0].code, S8("driver.argument"));
            }
            FileMapRead object = file_map_read(arena, output, (FileReadOptions){0});
            BUSTER_TEST(arguments, object.bytes.pointer && string_equal((String8){.pointer = (char8*)object.bytes.pointer, .length = object.bytes.length}, sentinel));
            file_map_unmap(object);
            FileMapRead dependencies = file_map_read(arena, depfile, (FileReadOptions){0});
            BUSTER_TEST(arguments, !dependencies.bytes.pointer);
            file_map_unmap(dependencies);
        }
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_version_script_images(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_LINUX && !BUSTER_ANDROID && BUSTER_CPU_ARCH_X86_64
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = temporary.arena;
    String8 source = buster_test_temporary_path(arena, S8("buster-version-library"), S8(".c"));
    String8 client = buster_test_temporary_path(arena, S8("buster-version-client"), S8(".c"));
    String8 script = buster_test_temporary_path(arena, S8("buster-version-library"), S8(".map"));
    String8 library = buster_test_temporary_path(arena, S8("buster-version-library"), S8(".so"));
    String8 executable = buster_test_temporary_path(arena, S8("buster-version-client"), S8(""));
    String8 source_text = S8("int private_helper(void) { return 7; }\n"
                             "int api(void) { return private_helper(); }\n"
                             "int datum = 19;\n"
                             "int main(void) { return api() - 7; }\n");
    String8 client_text = S8("#define _GNU_SOURCE\n#include <dlfcn.h>\n#include <string.h>\n"
                             "int private_helper(void) { return 99; }\n"
                             "int main(int argc, char **argv) {\n"
                             " int status = 1;\n"
                             " if (argc == 2) {\n"
                             "  void *lib = dlopen(argv[1], RTLD_NOW);\n"
                             "  if (lib) {\n"
                             "   int (*api)(void) = 0; void *plain = dlsym(lib, \"api\");\n"
                             "   void *versioned = dlvsym(lib, \"api\", \"V_1\");\n"
                             "   int *data = (int *)dlvsym(lib, \"datum\", \"V_1\");\n"
                             "   if (plain && plain == versioned) memcpy(&api, &plain, sizeof(api));\n"
                             "   if (api && api() == 7 && data && *data == 19 && !dlsym(lib, \"private_helper\") &&\n"
                             "       !dlsym(lib, \"main\") && !dlvsym(lib, \"api\", \"WRONG\")) status = 0;\n"
                             "   dlclose(lib);\n"
                             "  }\n"
                             " }\n"
                             " return status;\n}\n");
    BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(source_text)));
    BUSTER_TEST(arguments, file_write(client, BUSTER_SLICE_TO_BYTE_SLICE(client_text)));
    BUSTER_TEST(arguments, file_write(script, BUSTER_SLICE_TO_BYTE_SLICE(S8("V_1 { global: api; datum; local: *; };"))));
    String8 script_option = string_format_z(arena, S8("-Wl,--version-script={S8}"), script);
    String8 client_command[] = {S8("clang"), client, S8("-rdynamic"), S8("-ldl"), S8("-o"), executable};
    ProcessSpawnResult host = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(client_command), (SliceString8){0}, (SliceString8){0},
                                              (ProcessSpawnOptions){.use_process_environment = true, .search_path = true});
    bool client_ready = host.handle && os_process_wait_sync(arena, host).result == PROCESS_RESULT_SUCCESS;
    BUSTER_TEST(arguments, client_ready);
    // GNU ld and Clang/lld compile the same literal source and consume the same
    // map. Their client proves the observation contract before Buster is used.
    String8 host_linkers[] = {S8("-fuse-ld=bfd"), S8("-fuse-ld=lld")};
    for (u32 linker = 0; linker < BUSTER_ARRAY_LENGTH(host_linkers); linker += 1)
    {
        String8 control[] = {S8("clang"), host_linkers[linker], S8("-shared"), S8("-fPIC"), script_option, source, S8("-o"), library};
        ProcessSpawnResult control_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(control), (SliceString8){0}, (SliceString8){0},
                                                           (ProcessSpawnOptions){.use_process_environment = true, .search_path = true});
        bool ready = control_spawn.handle && os_process_wait_sync(arena, control_spawn).result == PROCESS_RESULT_SUCCESS;
        if (BUSTER_REQUIRE(arguments, ready && client_ready))
        {
            String8 check[] = {executable, library};
            ProcessSpawnResult child = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(check), (SliceString8){0}, (SliceString8){0},
                                                       (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, child.handle && os_process_wait_sync(arena, child).result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 allocators[] = {S8("-fregister-allocator=fast"), S8("-fregister-allocator=quality")};
    String8 frontends[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
    String8 modes[] = {S8("-shared"), S8("-pie"), S8("-no-pie")};
    for (u32 allocator = 0; allocator < BUSTER_ARRAY_LENGTH(allocators); allocator += 1)
    {
        for (u32 frontend = 0; frontend < BUSTER_ARRAY_LENGTH(frontends); frontend += 1)
        {
            for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
            {
                String8 command[] = {modes[mode], S8("-fPIC"), S8("-g0"), allocators[allocator], frontends[frontend],
                                     script_option, S8("-o"), mode == 0 ? library : executable, source};
                CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                if (BUSTER_REQUIRE(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE))
                {
                    String8 check[] = {executable, library};
                    ProcessSpawnResult child = os_process_spawn((SliceString8){.pointer = check, .length = mode == 0 ? 2 : 1},
                                                               (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
                    BUSTER_TEST(arguments, child.handle && os_process_wait_sync(arena, child).result == PROCESS_RESULT_SUCCESS);
                }
            }
            // Restore the independent host client after executable modes.
            host = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(client_command), (SliceString8){0}, (SliceString8){0},
                                    (ProcessSpawnOptions){.use_process_environment = true, .search_path = true});
            BUSTER_TEST(arguments, host.handle && os_process_wait_sync(arena, host).result == PROCESS_RESULT_SUCCESS);
        }
    }
    os_file_delete(source);
    os_file_delete(client);
    os_file_delete(script);
    os_file_delete(library);
    os_file_delete(executable);
    scratch_end(temporary);
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_pass_through_images(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_test_version_script_images);
#if BUSTER_LINUX && !BUSTER_ANDROID && BUSTER_CPU_ARCH_X86_64
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = temporary.arena;
    String8 output = buster_test_temporary_path(arena, S8("buster-pass-through-image"), S8(""));
    String8 export_options[] = {S8("-Wl,--no-undefined,--export-dynamic"), S8("-Wl,--export-dynamic,--no-undefined"), S8("-Wl,-z,defs,-E")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(export_options); index += 1)
    {
        String8 command[] = {export_options[index], S8("-g0"), S8("-o"), output, S8("tests/basic_c_export_dynamic.c"), S8("-ldl")};
        CompilerDriverResult image = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
        if (BUSTER_REQUIRE(arguments, image.error == COMPILER_DRIVER_ERROR_NONE))
        {
            String8 run[] = {output};
            ProcessSpawnResult child = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                                                       (ProcessSpawnOptions){.use_process_environment = true});
            if (BUSTER_REQUIRE(arguments, child.handle != 0))
            {
                BUSTER_TEST(arguments, os_process_wait_sync(arena, child).result == PROCESS_RESULT_SUCCESS);
            }
        }
    }
    String8 source = buster_test_temporary_path(arena, S8("buster-pass-through-shared"), S8(".c"));
    BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int main(void) { return 0; }\n"))));
    String8 sentinel = S8("existing image must survive a refused static export");
    BUSTER_TEST(arguments, file_write(output, BUSTER_SLICE_TO_BYTE_SLICE(sentinel)));
    String8 static_command[] = {S8("-g0"), S8("-Wl,--export-dynamic"), S8("-o"), output, source};
    CompilerDriverResult static_image = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(static_command)));
    BUSTER_TEST(arguments, static_image.error == COMPILER_DRIVER_ERROR_LINK && compiler_driver_test_string_contains(static_image.diagnostic, S8("--export-dynamic")));
    ByteSlice preserved = file_read(arena, output, (FileReadOptions){0});
    BUSTER_TEST(arguments, preserved.pointer && string_equal((String8){.pointer = (char8*)preserved.pointer, .length = preserved.length}, sentinel));
    BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(S8("extern int missing(void); int answer(void) { return missing(); }\n"))));
    // SONAME operands remain literal even when they resemble linker flags.
    String8 sonames[] = {S8("defs"), S8("--no-undefined"), S8("--soname=literal"), S8("--version")};
    for (u32 name = 0; name < BUSTER_ARRAY_LENGTH(sonames); name += 1)
    {
        String8 shared[] = {S8("-shared"), S8("-g0"), string_format_z(arena, S8("-Wl,-soname,{S8}"), sonames[name]), S8("-o"), output, source};
        CompilerDriverResult library = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(shared)));
        if (BUSTER_REQUIRE(arguments, library.error == COMPILER_DRIVER_ERROR_NONE))
        {
            FileMapRead map = file_map_read(arena, output, (FileReadOptions){0});
            u64 offset = 0, size = 0, address = 0;
            if (BUSTER_REQUIRE(arguments, map.bytes.pointer && compiler_driver_test_elf_section_find(map.bytes, S8(".dynstr"), &offset, &size, &address)))
            {
                u64 dynamic_offset = 0, dynamic_size = 0;
                bool found_soname = false;
                if (BUSTER_REQUIRE(arguments, compiler_driver_test_elf_section_find(map.bytes, S8(".dynamic"), &dynamic_offset, &dynamic_size, &address)))
                {
                    for (u64 entry = 0; entry + 16 <= dynamic_size; entry += 16)
                    {
                        u64 tag = 0, value = 0;
                        memcpy(&tag, map.bytes.pointer + dynamic_offset + entry, 8);
                        memcpy(&value, map.bytes.pointer + dynamic_offset + entry + 8, 8);
                        found_soname |= tag == 14 && value < size && size - value > sonames[name].length &&
                                       memcmp(map.bytes.pointer + offset + value, sonames[name].pointer, sonames[name].length) == 0 &&
                                       map.bytes.pointer[offset + value + sonames[name].length] == 0;
                    }
                }
                BUSTER_TEST(arguments, found_soname);
            }
            ByteSlice saved = {.pointer = arena_allocate(arena, u8, map.bytes.length), .length = map.bytes.length};
            if (map.bytes.length) memcpy(saved.pointer, map.bytes.pointer, map.bytes.length);
            file_map_unmap(map);
            String8 undefined_options[] = {S8("-Wl,--no-undefined"), S8("-Wl,-z,defs")};
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(undefined_options); index += 1)
            {
                shared[2] = undefined_options[index];
                CompilerDriverResult refused = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(shared)));
                BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_LINK);
                FileMapRead unchanged = file_map_read(arena, output, (FileReadOptions){0});
                BUSTER_TEST(arguments, unchanged.bytes.pointer && unchanged.bytes.length == saved.length &&
                                           memcmp(unchanged.bytes.pointer, saved.pointer, saved.length) == 0);
                file_map_unmap(unchanged);
            }
        }
    }
    bool report_source_written = file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int main(void) { return 0; }\n")));
    BUSTER_TEST(arguments, report_source_written);
    String8 report_prefix = string_format(arena, S8(" buster-native-ld -o {S8} "), output);
    String8 report_suffix = S8("-lc\n");
    if (report_source_written)
    {
        String8 verbose_arguments[] = {S8("-v"), S8("-o"), output, source};
        CompilerDriverInvocation verbose_invocation =
            compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(verbose_arguments));
        CompilerDriverResult verbose_result = compiler_driver_execute_invocation(arena, verbose_invocation);
        bool verbose_report_line = verbose_result.linker_report.length >= report_prefix.length + report_suffix.length &&
                                   string_starts_with_sequence(verbose_result.linker_report, report_prefix) &&
                                   memcmp(verbose_result.linker_report.pointer + verbose_result.linker_report.length - report_suffix.length,
                                          report_suffix.pointer, report_suffix.length) == 0;
        BUSTER_TEST(arguments, verbose_invocation.error == COMPILER_DRIVER_ERROR_NONE &&
                                   verbose_result.error == COMPILER_DRIVER_ERROR_NONE && verbose_report_line);

        String8 linker_verbose_arguments[] = {S8("-Wl,-v"), S8("-o"), output, source};
        CompilerDriverInvocation linker_verbose_invocation =
            compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(linker_verbose_arguments));
        CompilerDriverResult linker_verbose_result = compiler_driver_execute_invocation(arena, linker_verbose_invocation);
        BUSTER_TEST(arguments, linker_verbose_invocation.error == COMPILER_DRIVER_ERROR_NONE &&
                                   linker_verbose_result.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_STRING_TEST(arguments, linker_verbose_result.linker_report,
                           S8("Buster native linker\nTarget: x86_64-unknown-linux-gnu\n"));

        bool syntax_source_written = file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int main(void) { return 0 }\n")));
        BUSTER_TEST(arguments, syntax_source_written);
        if (syntax_source_written)
        {
            String8 syntax_arguments[] = {S8("-v"), S8("-o"), output, source};
            CompilerDriverInvocation syntax_invocation =
                compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(syntax_arguments));
            CompilerDriverResult syntax_result = compiler_driver_execute_invocation(arena, syntax_invocation);
            bool syntax_report_line = syntax_result.linker_report.length >= report_prefix.length + report_suffix.length &&
                                      string_starts_with_sequence(syntax_result.linker_report, report_prefix) &&
                                      memcmp(syntax_result.linker_report.pointer + syntax_result.linker_report.length - report_suffix.length,
                                             report_suffix.pointer, report_suffix.length) == 0;
            BUSTER_TEST(arguments, syntax_invocation.error == COMPILER_DRIVER_ERROR_NONE &&
                                       syntax_result.error != COMPILER_DRIVER_ERROR_NONE && syntax_report_line);
        }

        bool undefined_source_written =
            file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(S8("extern int linker_report_missing(void); int main(void) { return linker_report_missing(); }\n")));
        BUSTER_TEST(arguments, undefined_source_written);
        if (undefined_source_written)
        {
            String8 undefined_arguments[] = {S8("-Wl,-v"), S8("-o"), output, source};
            CompilerDriverInvocation undefined_invocation =
                compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(undefined_arguments));
            CompilerDriverResult undefined_result = compiler_driver_execute_invocation(arena, undefined_invocation);
            BUSTER_TEST(arguments, undefined_invocation.error == COMPILER_DRIVER_ERROR_NONE &&
                                       undefined_result.error == COMPILER_DRIVER_ERROR_LINK);
            BUSTER_STRING_TEST(arguments, undefined_result.linker_report,
                               S8("Buster native linker\nTarget: x86_64-unknown-linux-gnu\n"));
        }
    }
    BUSTER_TEST(arguments, os_file_delete(source));
    BUSTER_TEST(arguments, os_file_delete(output));
    scratch_end(temporary);
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}
