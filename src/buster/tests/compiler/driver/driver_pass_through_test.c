// Included by driver_test.c: pass-through options must change the requested
// artifact or fail before publication. Parse checks cover every native target;
// Linux image checks independently inspect SONAME and dynamic exports.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_pass_through_arguments(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = temporary.arena;
    String8 targets[] = {S8("x86_64-unknown-linux"), S8("aarch64-unknown-linux"), S8("x86_64-pc-windows-msvc"),
                         S8("aarch64-pc-windows-msvc"), S8("x86_64-apple-macos"), S8("aarch64-apple-macos"),
                         S8("x86_64-linux-android"), S8("aarch64-linux-android"), S8("x86_64-unknown-uefi")};
    String8 refused[] = {S8("--wrap=f"), S8("--defsym=x=1"), S8("-e"), S8("--whole-archive"), S8("--no-whole-archive"),
                         S8("-rpath"), S8("-runpath"), S8("-Bstatic"), S8("-Bdynamic"), S8("--version-script=map"),
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
    String8 preprocessing[] = {S8("-Wp,-DFROM_WP"), S8("-Wp,-UFROM_WP"), S8("-Wp,-Iinclude"), S8("-Wp,"),
                              S8("-Wa,--this-is-bogus"), S8("-Wa,"), S8("-MD"), S8("-MMD"), S8("-MF"), S8("-MT"),
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
        String8 flags[] = {string_format_z(arena, S8("-Wp,-MD,{S8}"), depfile), string_format_z(arena, S8("-Wp,-MMD,{S8}"), depfile),
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

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_pass_through_images(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
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
    String8 sonames[] = {S8("defs"), S8("--no-undefined"), S8("--soname=literal")};
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
    scratch_end(temporary);
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

// -mtune is accepted and ignored, -static is a request the linker honors or
// refuses precisely, and `-x <language> -` reads the unit from standard input
// under the name <stdin>. Parse checks cover every native target; the Linux
// image check independently runs the statically linked program.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_static_tune_stdin_options(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = temporary.arena;
    String8 tune[] = {S8("-mtune=native"), S8("-mtune=znver5"), S8("-mtune=generic")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(tune); index += 1)
    {
        String8 command[] = {S8("-c"), tune[index], S8("tests/basic_c_multi_main.c")};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE);
    }
    // -static is ignored by compile-only actions, as it is for GCC.
    String8 compile_only[] = {S8("-c"), S8("-S"), S8("-E")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(compile_only); index += 1)
    {
        String8 command[] = {compile_only[index], S8("-static"), S8("--target"), S8("x86_64-pc-windows-msvc"), S8("tests/basic_c_multi_main.c")};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.static_link);
    }
    String8 windows_link[] = {S8("-static"), S8("--target"), S8("x86_64-pc-windows-msvc"), S8("tests/basic_c_multi_main.c")};
    CompilerDriverInvocation windows = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(windows_link));
    BUSTER_TEST(arguments, windows.error == COMPILER_DRIVER_ERROR_ARGUMENT && compiler_driver_test_string_contains(windows.diagnostic, S8("-static")));
    String8 stdin_command[] = {S8("-x"), S8("c"), S8("-c"), S8("-")};
    CompilerDriverInvocation from_stdin = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(stdin_command));
    BUSTER_TEST(arguments, from_stdin.error == COMPILER_DRIVER_ERROR_NONE && from_stdin.input_count == 1);
    if (BUSTER_REQUIRE(arguments, from_stdin.input_count == 1))
    {
        BUSTER_STRING_TEST(arguments, from_stdin.input_paths[0], S8("<stdin>"));
    }
    String8 no_language[] = {S8("-c"), S8("-")};
    CompilerDriverInvocation unknown = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(no_language));
    BUSTER_TEST(arguments, unknown.error == COMPILER_DRIVER_ERROR_ARGUMENT && compiler_driver_test_string_contains(unknown.diagnostic, S8("-x")));
    String8 twice[] = {S8("-x"), S8("c"), S8("-c"), S8("-"), S8("-")};
    CompilerDriverInvocation repeated = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(twice));
    BUSTER_TEST(arguments, repeated.error == COMPILER_DRIVER_ERROR_ARGUMENT && compiler_driver_test_string_contains(repeated.diagnostic, S8("once")));
#if BUSTER_LINUX && !BUSTER_ANDROID && BUSTER_CPU_ARCH_X86_64
    String8 source = buster_test_temporary_path(arena, S8("buster-static-image"), S8(".c"));
    String8 output = buster_test_temporary_path(arena, S8("buster-static-image"), S8(""));
    BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int main(void) { return 0; }\n"))));
    String8 static_command[] = {S8("-static"), S8("-mtune=native"), S8("-g0"), S8("-o"), output, source};
    CompilerDriverResult image = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(static_command)));
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
    // A program that imports from the system C library cannot be static.
    BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int puts(const char*); int main(void) { return puts(\"x\"); }\n"))));
    CompilerDriverResult refused = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(static_command)));
    BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_LINK &&
                               compiler_driver_test_string_contains(refused.diagnostic, S8("-static is not supported when the program links against the system C library")));
#endif
    scratch_end(temporary);
    return result;
}
