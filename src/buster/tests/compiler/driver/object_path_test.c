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
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){0}, (OsFileAccess){ .read = 1 }, (OsFileCreateMode){0}, (OsFileShareFlags){0});
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

UnitTestResult compiler_driver_object_path_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
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
