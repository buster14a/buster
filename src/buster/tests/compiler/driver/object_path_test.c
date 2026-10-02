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
typedef struct CompilerDriverElfEmptySection CompilerDriverElfEmptySection;
struct CompilerDriverElfEmptySection
{
    u32 matches;
    u64 offset;
    u64 size;
    u64 alignment;
};

BUSTER_GLOBAL_LOCAL CompilerDriverElfEmptySection compiler_driver_elf_empty_section(ByteSlice bytes, String8 name)
{
    CompilerDriverElfEmptySection result = {0};
    u64 table = 0;
    u16 count = 0;
    u16 strings = 0;
    if (bytes.pointer && bytes.length >= 64 && memcmp(bytes.pointer, "\x7f" "ELF\x02\x01", 6) == 0)
    {
        memcpy(&table, bytes.pointer + 40, sizeof(table));
        memcpy(&count, bytes.pointer + 60, sizeof(count));
        memcpy(&strings, bytes.pointer + 62, sizeof(strings));
        if (table <= bytes.length && (u64)count * 64 <= bytes.length - table && strings < count)
        {
            u64 string_header = table + (u64)strings * 64;
            u64 string_offset = 0;
            u64 string_size = 0;
            memcpy(&string_offset, bytes.pointer + string_header + 24, sizeof(string_offset));
            memcpy(&string_size, bytes.pointer + string_header + 32, sizeof(string_size));
            if (string_offset <= bytes.length && string_size <= bytes.length - string_offset)
            {
                for (u32 index = 1; index < count; index += 1)
                {
                    u64 header = table + (u64)index * 64;
                    u32 name_offset = 0;
                    memcpy(&name_offset, bytes.pointer + header, sizeof(name_offset));
                    if (name_offset < string_size && name.length < string_size - name_offset &&
                        memcmp(bytes.pointer + string_offset + name_offset, name.pointer, name.length) == 0 &&
                        bytes.pointer[string_offset + name_offset + name.length] == 0)
                    {
                        result.matches += 1;
                        memcpy(&result.offset, bytes.pointer + header + 24, sizeof(result.offset));
                        memcpy(&result.size, bytes.pointer + header + 32, sizeof(result.size));
                        memcpy(&result.alignment, bytes.pointer + header + 48, sizeof(result.alignment));
                    }
                }
            }
        }
    }
    return result;
}

// Read program headers directly; a real TLS witness must have nonzero
// storage and a bounded initialized image, rather than merely a PT_TLS tag.
BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_empty_tls(ByteSlice bytes, u32* count_out, u64* file_size_out, u64* memory_size_out)
{
    bool result = bytes.pointer && bytes.length >= 64 && memcmp(bytes.pointer, "\x7f" "ELF\x02\x01", 6) == 0;
    u64 table = 0;
    u16 count = 0;
    u16 entry_size = 0;
    *count_out = 0;
    *file_size_out = 0;
    *memory_size_out = 0;
    if (result)
    {
        memcpy(&table, bytes.pointer + 32, sizeof(table));
        memcpy(&entry_size, bytes.pointer + 54, sizeof(entry_size));
        memcpy(&count, bytes.pointer + 56, sizeof(count));
        result = entry_size == 56 && table <= bytes.length && (u64)count * 56 <= bytes.length - table;
    }
    for (u32 index = 0; result && index < count; index += 1)
    {
        u64 header = table + (u64)index * 56;
        u32 type = 0;
        memcpy(&type, bytes.pointer + header, sizeof(type));
        if (type == 7)
        {
            u64 offset = 0;
            u64 file_size = 0;
            u64 memory_size = 0;
            u64 alignment = 0;
            memcpy(&offset, bytes.pointer + header + 8, sizeof(offset));
            memcpy(&file_size, bytes.pointer + header + 32, sizeof(file_size));
            memcpy(&memory_size, bytes.pointer + header + 40, sizeof(memory_size));
            memcpy(&alignment, bytes.pointer + header + 48, sizeof(alignment));
            result = offset <= bytes.length && file_size <= bytes.length - offset && file_size <= memory_size &&
                alignment && !(alignment & (alignment - 1));
            *count_out += 1;
            *file_size_out = file_size;
            *memory_size_out = memory_size;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_empty_contains(ByteSlice bytes, String8 needle)
{
    bool result = false;
    for (u64 offset = 0; !result && needle.length <= bytes.length && offset <= bytes.length - needle.length; offset += 1)
    {
        result = memcmp(bytes.pointer + offset, needle.pointer, needle.length) == 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_empty_host(UnitTestArguments* arguments, SliceString8 options, ByteSlice* error_out)
{
    String8 command[16] = {S8(BUSTER_HOST_C_COMPILER)};
    u32 count = 1;
    String8 first = S8(BUSTER_HOST_C_COMPILER_ARG1);
    if (first.length) command[count++] = first;
    BUSTER_CHECK(options.length <= BUSTER_ARRAY_LENGTH(command) - count);
    for (u64 index = 0; index < options.length; index += 1) command[count++] = options.pointer[index];
    ProcessSpawnResult spawned = os_process_spawn((SliceString8){.pointer = command, .length = count}, (SliceString8){0}, (SliceString8){0},
        (ProcessSpawnOptions){.use_process_environment = true, .new_process_group = true, .search_path = true,
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
    bool result = spawned.handle != 0;
    *error_out = (ByteSlice){0};
    if (result)
    {
        ProcessWaitResult waited = os_process_wait_deadline(arguments->arena, spawned, 30000000);
        *error_out = waited.streams[STANDARD_STREAM_ERROR];
        result = !waited.timed_out && !waited.capture_failed && !waited.output_truncated && waited.result == PROCESS_RESULT_SUCCESS;
        if (!result) arguments->show(arguments, S8("empty ELF section host command failed: {S8}\n"), BYTE_SLICE_TO_STRING(8, *error_out));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_empty_run(Arena* arena, String8 executable)
{
    String8 command[] = {executable};
    ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
        (ProcessSpawnOptions){.use_process_environment = true, .new_process_group = true,
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
    bool result = spawned.handle != 0;
    if (result)
    {
        ProcessWaitResult waited = os_process_wait_deadline(arena, spawned, 30000000);
        result = !waited.timed_out && !waited.capture_failed && waited.result == PROCESS_RESULT_SUCCESS &&
            !waited.streams[STANDARD_STREAM_OUTPUT].length && !waited.streams[STANDARD_STREAM_ERROR].length;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_elf_empty_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 root = buster_test_temporary_path(arena, S8("buster-elf-empty-sections"), S8(""));
    os_make_directory(root);
    root = os_path_absolute(arena, root, true);
    if (BUSTER_REQUIRE(arguments, root.length != 0))
    {
        String8 literals[] = {S8(""), S8("a"), S8("ab"), S8("abcd"), S8("abcde")};
        String8 frontends[] = {S8("-fno-frontend-ssa"), S8("-ffrontend-ssa")};
        for (u32 frontend = 0; frontend < BUSTER_ARRAY_LENGTH(frontends); frontend += 1)
        {
            for (u32 row = 0; row < BUSTER_ARRAY_LENGTH(literals); row += 1)
            {
                String8 source = string_format_z(arena, S8("{S8}/parity-{u32}-{u32}.c"), root, frontend, row);
                String8 object_path = string_format_z(arena, S8("{S8}/parity-{u32}-{u32}.o"), root, frontend, row);
                String8 direct_path = string_format_z(arena, S8("{S8}/direct-{u32}-{u32}"), root, frontend, row);
                String8 indirect_path = string_format_z(arena, S8("{S8}/indirect-{u32}-{u32}"), root, frontend, row);
                String8 program = string_format(arena, S8("static const char m[] = \"{S8}\";\n"
                    "int main(void) { return *(const volatile char *)m != {u32}; }\n"), literals[row], row ? 97u : 0u);
                bool written = file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(program));
                BUSTER_TEST(arguments, written);
                if (written)
                {
                    String8 direct_command[] = {frontends[frontend], S8("-o"), direct_path, source};
                    String8 object_command[] = {frontends[frontend], S8("-c"), S8("-o"), object_path, source};
                    CompilerDriverError direct_error = compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(direct_command));
                    CompilerDriverError object_error = compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(object_command));
                    BUSTER_TEST(arguments, direct_error == COMPILER_DRIVER_ERROR_NONE && object_error == COMPILER_DRIVER_ERROR_NONE);
                    if (direct_error == COMPILER_DRIVER_ERROR_NONE && object_error == COMPILER_DRIVER_ERROR_NONE)
                    {
                        ByteSlice object_bytes = file_read(arena, object_path, (FileReadOptions){0});
                        BUSTER_TEST(arguments, compiler_driver_elf_empty_section(object_bytes, S8(".tdata")).matches == 0 &&
                            compiler_driver_elf_empty_section(object_bytes, S8(".tbss")).matches == 0);
                        String8 indirect_command[] = {S8("-o"), indirect_path, object_path};
                        CompilerDriverError indirect_error = compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(indirect_command));
                        if (BUSTER_REQUIRE(arguments, indirect_error == COMPILER_DRIVER_ERROR_NONE))
                        {
                            ByteSlice direct = file_read(arena, direct_path, (FileReadOptions){0});
                            ByteSlice indirect = file_read(arena, indirect_path, (FileReadOptions){0});
                            CompilerDriverElfEmptySection direct_rodata = compiler_driver_elf_empty_section(direct, S8(".rodata"));
                            CompilerDriverElfEmptySection indirect_rodata = compiler_driver_elf_empty_section(indirect, S8(".rodata"));
                            bool bounded = direct_rodata.matches == 1 && indirect_rodata.matches == 1 &&
                                direct_rodata.offset <= direct.length && direct_rodata.size <= direct.length - direct_rodata.offset &&
                                indirect_rodata.offset <= indirect.length && indirect_rodata.size <= indirect.length - indirect_rodata.offset;
                            if (BUSTER_REQUIRE(arguments, bounded))
                            {
                                BUSTER_TEST(arguments, direct_rodata.size == literals[row].length + 1 &&
                                    indirect_rodata.size == direct_rodata.size && indirect_rodata.alignment == direct_rodata.alignment);
                                BUSTER_TEST(arguments, direct_rodata.size == indirect_rodata.size &&
                                    memcmp(direct.pointer + direct_rodata.offset, indirect.pointer + indirect_rodata.offset, direct_rodata.size) == 0);
                            }
                            BUSTER_TEST(arguments, compiler_driver_elf_empty_run(arena, direct_path));
                            BUSTER_TEST(arguments, compiler_driver_elf_empty_run(arena, indirect_path));
                        }
                    }
                }
            }
        }
        String8 family = S8(BUSTER_HOST_C_COMPILER_ID);
        bool configured = string_equal(family, S8("Clang")) || string_equal(family, S8("GNU"));
        if (configured)
        {
            String8 probe_source = string_format_z(arena, S8("{S8}/host-probe.c"), root);
            String8 probe_path = string_format_z(arena, S8("{S8}/host-probe"), root);
            bool written = file_write(probe_source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int main(void) { return 0; }\n")));
            BUSTER_TEST(arguments, written);
            if (written)
            {
                String8 probe_options[] = {S8("-fuse-ld=lld"), S8("-no-pie"), probe_source, S8("-o"), probe_path};
                ByteSlice host_error = {0};
                bool available = compiler_driver_elf_empty_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(probe_options), &host_error);
                if (!available)
                {
                    bool missing = compiler_driver_elf_empty_contains(host_error, S8("invalid linker name")) ||
                        compiler_driver_elf_empty_contains(host_error, S8("cannot find 'ld'")) ||
                        compiler_driver_elf_empty_contains(host_error, S8("cannot find ld"));
                    BUSTER_TEST(arguments, missing);
                    arguments->show(arguments, S8("empty ELF section LLD witnesses unavailable for configured {S8}\n"), family);
                }
                else
                {
                    ByteSlice probe = file_read(arena, probe_path, (FileReadOptions){0});
                    BUSTER_TEST(arguments, compiler_driver_elf_empty_contains(probe, S8("LLD")));
                    BUSTER_TEST(arguments, compiler_driver_elf_empty_run(arena, probe_path));
                    u32 tls_count = 0;
                    u64 file_size = 0;
                    u64 memory_size = 0;
                    BUSTER_TEST(arguments, compiler_driver_elf_empty_tls(probe, &tls_count, &file_size, &memory_size) && tls_count == 0);
                    for (u32 tls = 0; tls < 2; tls += 1)
                    {
                        String8 source = string_format_z(arena, S8("{S8}/lld-{u32}.c"), root, tls);
                        String8 object_path = string_format_z(arena, S8("{S8}/lld-{u32}.o"), root, tls);
                        String8 executable = string_format_z(arena, S8("{S8}/lld-{u32}"), root, tls);
                        String8 program = tls ? S8("_Thread_local int initialized = 7;\n_Thread_local int zero;\n"
                            "int main(void) { zero = initialized + 2; return zero != 9; }\n")
                            : S8("static const char value[] = \"a\";\nint main(void) { return *(const volatile char *)value != 97; }\n");
                        bool stored = file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(program));
                        BUSTER_TEST(arguments, stored);
                        if (stored)
                        {
                            String8 compile_command[] = {S8("-c"), S8("-o"), object_path, source};
                            CompilerDriverError error = compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(compile_command));
                            if (BUSTER_REQUIRE(arguments, error == COMPILER_DRIVER_ERROR_NONE))
                            {
                                ByteSlice object = file_read(arena, object_path, (FileReadOptions){0});
                                CompilerDriverElfEmptySection data = compiler_driver_elf_empty_section(object, S8(".tdata"));
                                CompilerDriverElfEmptySection zero = compiler_driver_elf_empty_section(object, S8(".tbss"));
                                BUSTER_TEST(arguments, tls ? data.matches == 1 && data.size >= 4 && zero.matches == 1 && zero.size >= 4
                                    : data.matches == 0 && zero.matches == 0);
                                String8 link_options[] = {S8("-fuse-ld=lld"), S8("-no-pie"), object_path, S8("-o"), executable};
                                bool linked = compiler_driver_elf_empty_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(link_options), &host_error);
                                if (BUSTER_REQUIRE(arguments, linked))
                                {
                                    ByteSlice image = file_read(arena, executable, (FileReadOptions){0});
                                    bool headers = compiler_driver_elf_empty_tls(image, &tls_count, &file_size, &memory_size);
                                    BUSTER_TEST(arguments, headers && (tls ? tls_count == 1 && file_size >= 4 && memory_size >= 8 : tls_count == 0));
                                    BUSTER_TEST(arguments, compiler_driver_elf_empty_contains(image, S8("LLD")));
                                    BUSTER_TEST(arguments, compiler_driver_elf_empty_run(arena, executable));
                                    arguments->show(arguments, S8("empty ELF section native LLD witness TLS={u32}, PT_TLS={u32}, file={u64}, memory={u64}\n"),
                                        tls, tls_count, file_size, memory_size);
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            arguments->show(arguments, S8("empty ELF section LLD witnesses skipped for configured compiler {S8}\n"), family);
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
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_elf_empty_tests);
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
