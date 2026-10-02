#include <buster/tests/compiler/driver/object_path_test.h>
#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/file.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>
#include <buster/lib/system_headers.h>

#if !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL bool compiler_driver_object_path_test_change_directory(String8 path)
{
#if BUSTER_WINDOWS
    TemporalArena scratch = scratch_begin(0, 0);
    String16 wide_path = string16_from_string8(scratch.arena, path, true);
    bool result = SetCurrentDirectoryW(wide_path.pointer) != 0;
    scratch_end(scratch);
    return result;
#else
    BUSTER_CHECK(path.pointer != 0 && path.pointer[path.length] == 0);
    return chdir((const char*)path.pointer) == 0;
#endif
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_object_path_test_file_exists(String8 path)
{
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){.read = 1}, (OpenPermissions){0});
    bool result = file != 0;
    if (file)
    {
        BUSTER_CHECK(os_file_close(file));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDriverError compiler_driver_object_path_test_compile(UnitTestArguments* arguments, SliceString8 command_line)
{
    Arena* arena = arena_create((ArenaCreation){0});
    if (!arena)
    {
        return COMPILER_DRIVER_ERROR_INVALID_INPUT;
    }
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, command_line);
    CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, invocation);
    if (compiled.error != COMPILER_DRIVER_ERROR_NONE && compiled.diagnostic.length)
    {
        arguments->show(arguments, S8("default object path compiler error: {S8}\n"), compiled.diagnostic);
    }
    CompilerDriverError result = compiled.error;
    BUSTER_CHECK(arena_destroy(arena, 1));
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_object_path_test_process_success(Arena* arena, String8 executable)
{
    String8 command[] = {executable};
    ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
                                                   (ProcessSpawnOptions){.use_process_environment = true});
    return spawned.handle && os_process_wait_sync(arena, spawned).result == PROCESS_RESULT_SUCCESS;
}
#endif

#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_semantic_host(UnitTestArguments* arguments, SliceString8 options)
{
    String8 command[16] = {S8(BUSTER_HOST_C_COMPILER)};
    u32 count = 1;
    String8 argument = S8(BUSTER_HOST_C_COMPILER_ARG1);
    if (argument.length) command[count++] = argument;
    BUSTER_CHECK(options.length <= BUSTER_ARRAY_LENGTH(command) - count);
    for (u64 index = 0; index < options.length; index += 1) command[count++] = options.pointer[index];
    ProcessSpawnResult spawned = os_process_spawn((SliceString8){.pointer = command, .length = count}, (SliceString8){0}, (SliceString8){0},
        (ProcessSpawnOptions){.use_process_environment = true, .new_process_group = true, .search_path = true,
                              .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
    bool result = spawned.handle != 0;
    if (result)
    {
        ProcessWaitResult waited = os_process_wait_deadline(arguments->arena, spawned, 30000000);
        result = !waited.timed_out && waited.result == PROCESS_RESULT_SUCCESS;
        if (!result) arguments->show(arguments, S8("ELF semantics host compiler failed: {S8}\n"), BYTE_SLICE_TO_STRING(8, waited.streams[STANDARD_STREAM_ERROR]));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_semantic_run(Arena* arena, String8 executable)
{
    String8 command[] = {executable};
    ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
        (ProcessSpawnOptions){.use_process_environment = true, .new_process_group = true});
    bool result = spawned.handle != 0;
    if (result)
    {
        ProcessWaitResult waited = os_process_wait_deadline(arena, spawned, 30000000);
        result = !waited.timed_out && waited.result == PROCESS_RESULT_SUCCESS;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ByteSlice compiler_driver_elf_semantic_archive(Arena* arena, ByteSlice member)
{
    u64 padding = member.length & 1;
    ByteSlice bytes = {.pointer = arena_allocate(arena, u8, 8 + 60 + member.length + padding), .length = 8 + 60 + member.length + padding};
    memcpy(bytes.pointer, "!<arch>\n", 8);
    memset(bytes.pointer + 8, ' ', 60);
    memcpy(bytes.pointer + 8, "semantic.o/", 11);
    bytes.pointer[8 + 16] = '0';
    bytes.pointer[8 + 28] = '0';
    bytes.pointer[8 + 34] = '0';
    memcpy(bytes.pointer + 8 + 40, "644", 3);
    String8 size = string_format(arena, S8("{u64}"), member.length);
    BUSTER_CHECK(size.length <= 10);
    memcpy(bytes.pointer + 8 + 48, size.pointer, size.length);
    memcpy(bytes.pointer + 8 + 58, "`\n", 2);
    memcpy(bytes.pointer + 68, member.pointer, member.length);
    if (padding) bytes.pointer[bytes.length - 1] = '\n';
    return bytes;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_elf_semantic_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 compiler = S8(BUSTER_HOST_C_COMPILER_ID);
    bool supported = string_equal(compiler, S8("GNU")) || string_equal(compiler, S8("Clang")) || string_equal(compiler, S8("AppleClang"));
    if (supported)
    {
        typedef struct ElfSemanticCase ElfSemanticCase;
        struct ElfSemanticCase
        {
            String8 name;
            String8 program;
            String8 diagnostic;
        };
        ElfSemanticCase cases[] = {
            {S8("ifunc"), S8("static int implementation(int x) { return x * 3; }\n"
                "static int (*resolve_scale(void))(int) { return implementation; }\n"
                "int scale(int) __attribute__((ifunc(\"resolve_scale\")));\n"
                "int main(void) { return scale(7) != 21; }\n"), S8("unsupported ELF symbol scale (type 10)")},
            {S8("ctors"), S8("static int ran; static void hook(void) { ran = 1; }\n"
                "__attribute__((section(\".ctors\"), used)) static void (*entry)(void) = hook;\n"
                "int main(void) { return ran != 1; }\n"), S8("unsupported ELF section .ctors (type 1)")},
            {S8("dtors"), S8("extern void _Exit(int); static void hook(void) { _Exit(0); }\n"
                "__attribute__((section(\".dtors\"), used)) static void (*entry)(void) = hook;\n"
                "int main(void) { return 1; }\n"), S8("unsupported ELF section .dtors (type 1)")},
#if BUSTER_CPU_ARCH_X86_64
            {S8("init"), S8("static int ran; void semantic_hook(void) { ran = 1; }\n"
                "__asm__(\".pushsection .init,\\\"ax\\\",@progbits\\ncall semantic_hook\\n.popsection\");\n"
                "int main(void) { return ran != 1; }\n"), S8("unsupported ELF section .init (type 1)")},
            {S8("fini"), S8("extern void _Exit(int); void semantic_hook(void) { _Exit(0); }\n"
                "__asm__(\".pushsection .fini,\\\"ax\\\",@progbits\\ncall semantic_hook\\n.popsection\");\n"
                "int main(void) { return 1; }\n"), S8("unsupported ELF section .fini (type 1)")},
            {S8("allocated-note"), S8("__asm__(\".pushsection .note.vendor,\\\"a\\\",@note\\n.balign 4\\n.long 4,4,1\\n.asciz \\\"VND\\\"\\n.long 0\\n.popsection\");\n"
                "int main(void) { return 0; }\n"), S8("unsupported ELF section .note.vendor (type 7)")},
#else
            {S8("init"), S8("static int ran; void semantic_hook(void) { ran = 1; }\n"
                "__asm__(\".pushsection .init,\\\"ax\\\",%progbits\\nbl semantic_hook\\n.popsection\");\n"
                "int main(void) { return ran != 1; }\n"), S8("unsupported ELF section .init (type 1)")},
            {S8("fini"), S8("extern void _Exit(int); void semantic_hook(void) { _Exit(0); }\n"
                "__asm__(\".pushsection .fini,\\\"ax\\\",%progbits\\nbl semantic_hook\\n.popsection\");\n"
                "int main(void) { return 1; }\n"), S8("unsupported ELF section .fini (type 1)")},
            {S8("allocated-note"), S8("__asm__(\".pushsection .note.vendor,\\\"a\\\",%note\\n.balign 4\\n.long 4,4,1\\n.asciz \\\"VND\\\"\\n.long 0\\n.popsection\");\n"
                "int main(void) { return 0; }\n"), S8("unsupported ELF section .note.vendor (type 7)")},
#endif
            {S8("absolute"), S8("extern char semantic_absolute;\n"
                "__asm__(\".globl semantic_absolute\\n.set semantic_absolute,0x1234\");\n"
                "int main(void) { return (unsigned long)&semantic_absolute != 0x1234; }\n"),
                S8("unsupported ELF symbol semantic_absolute (section index 65521)")},
            {S8("weak-absolute"), S8("extern char semantic_absolute __attribute__((weak));\n"
                "__asm__(\".weak semantic_absolute\\n.set semantic_absolute,0x1234\");\n"
                "int main(void) { return (unsigned long)&semantic_absolute != 0x1234; }\n"),
                S8("unsupported ELF symbol semantic_absolute (section index 65521)")},
            {S8("preinit-control"), S8("static int ran; static void preinit(void) { ran = 1; }\n"
                "__attribute__((section(\".preinit_array\"), used)) static void (*entry)(void) = preinit;\n"
                "__attribute__((constructor)) static void constructor(void) { ran = ran == 1 ? 2 : 9; }\n"
                "int main(void) { return ran != 2; }\n"), {0}},
        };
        String8 root = buster_test_temporary_path(arena, S8("buster-elf-semantic-inputs"), S8(""));
        OsDirectoryCreateResult created = os_make_directory(root);
        BUSTER_TEST(arguments, created.error.v == 0);
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
        {
            ElfSemanticCase test = cases[index];
            String8 source = string_format_z(arena, S8("{S8}/{S8}.c"), root, test.name);
            String8 object = string_format_z(arena, S8("{S8}/{S8}.o"), root, test.name);
            String8 oracle = string_format_z(arena, S8("{S8}/{S8}-oracle"), root, test.name);
            String8 output = string_format_z(arena, S8("{S8}/{S8}-buster"), root, test.name);
            BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(test.program)));
            String8 compile[] = {S8("-O2"), S8("-fno-pie"), S8("-c"), source, S8("-o"), object};
            bool produced = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(compile));
            BUSTER_TEST(arguments, produced);
            if (produced)
            {
                // The independent linker must preserve the input's runtime meaning.
                String8 host_link[] = {S8("-no-pie"), object, S8("-o"), oracle};
                bool linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_link));
                BUSTER_TEST(arguments, linked);
                if (linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, oracle));
                ObjectFile read = object_read(arena, file_read(arena, object, (FileReadOptions){0}), target_native);
                String8 command[] = {S8("-no-pie"), object, S8("-o"), output};
                CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, invocation);
                if (test.diagnostic.length)
                {
                    BUSTER_TEST(arguments, read.error == OBJECT_ERROR_UNSUPPORTED_TARGET);
                    BUSTER_STRING_TEST(arguments, read.diagnostic, test.diagnostic);
                    BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_OBJECT && compiled.object_error == OBJECT_ERROR_UNSUPPORTED_TARGET);
                    BUSTER_STRING_TEST(arguments, compiled.diagnostic, string_format(arena, S8("could not read object {S8}: {S8}"), object, test.diagnostic));
                    BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(output));
                    BUSTER_TEST(arguments, !compiled.native_link.executable.pointer && !compiled.native_link.executable.length);
                }
                else
                {
                    BUSTER_TEST(arguments, read.error == OBJECT_ERROR_NONE && compiled.error == COMPILER_DRIVER_ERROR_NONE);
                    if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, output));
                }
                if (index == 0)
                {
                    ByteSlice member = file_read(arena, object, (FileReadOptions){0});
                    ByteSlice archive_bytes = compiler_driver_elf_semantic_archive(arena, member);
                    ObjectArchive eager = object_archive_read(arena, archive_bytes, target_native);
                    BUSTER_TEST(arguments, eager.error == OBJECT_ERROR_UNSUPPORTED_TARGET && eager.failed_member == 0);
                    BUSTER_STRING_TEST(arguments, eager.diagnostic, S8("member semantic.o: unsupported ELF symbol scale (type 10)"));
                    ObjectArchive lazy = object_archive_read_link(arena, archive_bytes, target_native);
                    BUSTER_TEST(arguments, lazy.error == OBJECT_ERROR_NONE && lazy.object_count == 1);
                    String8 archive = string_format_z(arena, S8("{S8}/ifunc.a"), root);
                    String8 caller = string_format_z(arena, S8("{S8}/caller.c"), root);
                    BUSTER_TEST(arguments, file_write(archive, archive_bytes));
                    for (u32 selected = 0; selected < 2; selected += 1)
                    {
                        String8 program = selected ? S8("int scale(int); int main(void) { return scale(7) != 21; }\n")
                                                   : S8("int main(void) { return 0; }\n");
                        BUSTER_TEST(arguments, file_write(caller, BUSTER_SLICE_TO_BYTE_SLICE(program)));
                        String8 image = string_format_z(arena, S8("{S8}/archive-{u32}"), root, selected);
                        String8 link[] = {S8("-no-pie"), caller, archive, S8("-o"), image};
                        invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(link));
                        CompilerDriverResult archived = compiler_driver_execute_invocation(arena, invocation);
                        if (selected)
                        {
                            BUSTER_TEST(arguments, archived.error == COMPILER_DRIVER_ERROR_OBJECT && archived.object_error == OBJECT_ERROR_UNSUPPORTED_TARGET);
                            String8 cpu = cpu_arch_to_string_os(target_native.cpu_arch);
                            String8 os = operating_system_to_string_os(target_native.os);
                            BUSTER_STRING_TEST(arguments, archived.diagnostic, string_format(arena,
                                S8("could not read archive {S8}: selected member semantic.o ({S8}-{S8}) for {S8}-{S8}: unsupported ELF symbol scale (type 10); error {u32}"),
                                archive, cpu, os, cpu, os, (u32)OBJECT_ERROR_UNSUPPORTED_TARGET));
                            BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(image));
                        }
                        else
                        {
                            BUSTER_TEST(arguments, archived.error == COMPILER_DRIVER_ERROR_NONE);
                            if (archived.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, image));
                        }
                    }
                }
            }
        }
        BUSTER_TEST(arguments, os_directory_delete(root));
    }
    return result;
}
#endif

UnitTestResult compiler_driver_object_path_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_elf_semantic_tests);
#endif
#if BUSTER_ANDROID || BUSTER_IOS
    BUSTER_UNUSED(arguments);
#else
    Arena* arena = arguments->arena;
    String8 root = buster_test_temporary_path(arena, S8("buster-driver-object-path"), S8(""));
    os_make_directory(root);
    String8 root_absolute = os_path_absolute(arena, root, true);
    BUSTER_TEST(arguments, root_absolute.length != 0);
    if (!root_absolute.length)
    {
        return result;
    }

    String8 relative_directory = string_format_z(arena, S8("{S8}/relative-source"), root_absolute);
    String8 absolute_directory = string_format_z(arena, S8("{S8}/absolute-source"), root_absolute);
    String8 left_directory = string_format_z(arena, S8("{S8}/left-source"), root_absolute);
    String8 right_directory = string_format_z(arena, S8("{S8}/right-source"), root_absolute);
    String8 work_directory = string_format_z(arena, S8("{S8}/work"), root_absolute);
    os_make_directory(relative_directory);
    os_make_directory(absolute_directory);
    os_make_directory(left_directory);
    os_make_directory(right_directory);
    os_make_directory(work_directory);

    String8 relative_source = string_format_z(arena, S8("{S8}/relative.c"), relative_directory);
    String8 absolute_source = string_format_z(arena, S8("{S8}/absolute.c"), absolute_directory);
    String8 cwd_source = string_format_z(arena, S8("{S8}/cwd.c"), work_directory);
    String8 left_source = string_format_z(arena, S8("{S8}/collide.c"), left_directory);
    String8 right_source = string_format_z(arena, S8("{S8}/collide.c"), right_directory);
    BUSTER_TEST(arguments, file_write(relative_source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int relative_case(void) { return 11; }\n"))));
    BUSTER_TEST(arguments, file_write(absolute_source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int absolute_case(void) { return 22; }\n"))));
    BUSTER_TEST(arguments, file_write(cwd_source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int cwd_case(void) { return 33; }\n"))));
    BUSTER_TEST(arguments, file_write(left_source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int collision_left(void) { return 44; }\n"))));
    BUSTER_TEST(arguments, file_write(right_source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int collision_right(void) { return 55; }\n"))));

    String8 original_directory = os_path_absolute(arena, S8("."), true);
    String8 absolute_input = os_path_absolute(arena, absolute_source, true);
    String8 relative_wrong_output = string_format_z(arena, S8("{S8}/relative.o"), relative_directory);
    String8 absolute_wrong_output = string_format_z(arena, S8("{S8}/absolute.o"), absolute_directory);
    String8 left_wrong_output = string_format_z(arena, S8("{S8}/collide.o"), left_directory);
    String8 right_wrong_output = string_format_z(arena, S8("{S8}/collide.o"), right_directory);
    BUSTER_TEST(arguments, original_directory.length != 0 && absolute_input.length != 0);

    bool changed_directory = original_directory.length && absolute_input.length && compiler_driver_object_path_test_change_directory(work_directory);
    BUSTER_TEST(arguments, changed_directory);
    if (changed_directory)
    {
        String8 relative_command[] = {S8("-c"), S8("../relative-source/relative.c")};
        CompilerDriverError relative_error = compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(relative_command));
        BUSTER_TEST(arguments, relative_error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, compiler_driver_object_path_test_file_exists(S8("relative.o")));
        BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(relative_wrong_output));

        String8 absolute_command[] = {S8("-c"), absolute_input};
        CompilerDriverError absolute_error = compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(absolute_command));
        BUSTER_TEST(arguments, absolute_error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, compiler_driver_object_path_test_file_exists(S8("absolute.o")));
        BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(absolute_wrong_output));

        String8 cwd_command[] = {S8("-c"), S8("cwd.c")};
        CompilerDriverError cwd_error = compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(cwd_command));
        BUSTER_TEST(arguments, cwd_error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, compiler_driver_object_path_test_file_exists(S8("cwd.o")));

        String8 collision_command[] = {S8("-c"), S8("../left-source/collide.c"), S8("../right-source/collide.c")};
        CompilerDriverError collision_error = compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(collision_command));
        BUSTER_TEST(arguments, collision_error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, compiler_driver_object_path_test_file_exists(S8("collide.o")));
        BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(left_wrong_output));
        BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(right_wrong_output));
    }

    BUSTER_TEST(arguments, compiler_driver_object_path_test_change_directory(original_directory));

    // A label may re-enter after a fixed-size automatic declaration. Storage
    // must exist, but the skipped initializer must not execute. The skipped
    // block-scope extern must continue to refer to external storage.
    String8 unreachable_source = string_format_z(arena, S8("{S8}/unreachable-local.c"), root_absolute);
    String8 unreachable_program = S8(
        "static volatile int initializer_calls;\n"
        "int external_value = 9;\n"
        "static int read_external(void)\n"
        "{\n"
        "    goto live;\n"
        "    extern int external_value;\n"
        "live:\n"
        "    return external_value;\n"
        "}\n"
        "int main(void)\n"
        "{\n"
        "    goto live;\n"
        "    int target = (initializer_calls += 1, 5);\n"
        "live:\n"
        "    target = 7;\n"
        "    goto dead;\n"
        "dead:\n"
        "    return initializer_calls != 0 || target != 7 || read_external() != 9;\n"
        "}\n");
    BUSTER_TEST(arguments, file_write(unreachable_source, BUSTER_SLICE_TO_BYTE_SLICE(unreachable_program)));
    String8 allocators[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
    String8 frontends[] = {S8("-fno-frontend-ssa"), S8("-ffrontend-ssa")};
    for (u32 frontend = 0; frontend < BUSTER_ARRAY_LENGTH(frontends); frontend += 1)
    {
        for (u32 allocator = 0; allocator < BUSTER_ARRAY_LENGTH(allocators); allocator += 1)
        {
#if BUSTER_WINDOWS
            String8 executable = string_format_z(arena, S8("{S8}/unreachable-local-{u32}-{u32}.exe"), root_absolute, frontend, allocator);
#else
            String8 executable = string_format_z(arena, S8("{S8}/unreachable-local-{u32}-{u32}"), root_absolute, frontend, allocator);
#endif
            String8 allocator_option = string_format_z(arena, S8("-fregister-allocator={S8}"), allocators[allocator]);
            String8 command[8] = {allocator_option, frontends[frontend]};
            u32 command_count = 2;
            if (allocator != 0)
            {
                command[command_count++] = S8("-fno-machine-fallback");
                command[command_count++] = S8("-fverify-codegen");
            }
            command[command_count++] = S8("-o");
            command[command_count++] = executable;
            command[command_count++] = unreachable_source;
            CompilerDriverError error = compiler_driver_object_path_test_compile(
                arguments, (SliceString8){.pointer = command, .length = command_count});
            BUSTER_TEST(arguments, error == COMPILER_DRIVER_ERROR_NONE);
            if (error == COMPILER_DRIVER_ERROR_NONE)
            {
                BUSTER_TEST(arguments, compiler_driver_object_path_test_process_success(arena, executable));
            }
        }
    }

    BUSTER_TEST(arguments, os_directory_delete(root_absolute));
#endif
    return result;
}

#endif
