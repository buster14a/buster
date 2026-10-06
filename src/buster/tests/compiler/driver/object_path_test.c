#include <buster/tests/compiler/driver/object_path_test.h>
#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/compiler/object/object.h>
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
                    "int main(void) {{ return *(const volatile char *)m != {u32}; }}\n"), literals[row], row ? 97u : 0u);
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
                            "int main(void) { if (initialized != 7 || zero != 0) return 1; zero = initialized + 2; "
                            "return initialized != 7 || zero != 9; }\n")
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

#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
BUSTER_GLOBAL_LOCAL bool compiler_driver_tls_export_host(UnitTestArguments* arguments, SliceString8 options)
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
        if (!result) arguments->show(arguments, S8("TLS export host compiler failed: {S8}\n"), BYTE_SLICE_TO_STRING(8, waited.streams[STANDARD_STREAM_ERROR]));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_tls_export_run(UnitTestArguments* arguments, String8 executable, String8 library_path)
{
    String8 command[] = {executable};
    String8 keys[] = {S8("LD_LIBRARY_PATH")};
    String8 values[] = {library_path};
    ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8)BUSTER_ARRAY_TO_SLICE(keys),
        (SliceString8)BUSTER_ARRAY_TO_SLICE(values), (ProcessSpawnOptions){.new_process_group = true, .capture = (u64)1 << STANDARD_STREAM_ERROR});
    bool result = spawned.handle != 0;
    if (result)
    {
        ProcessWaitResult waited = os_process_wait_deadline(arguments->arena, spawned, 30000000);
        result = !waited.timed_out && waited.result == PROCESS_RESULT_SUCCESS;
        if (!result) arguments->show(arguments, S8("TLS export runtime {S8}: status {u32}, timeout {u32}: {S8}\n"),
            executable, waited.platform_status, (u32)waited.timed_out, BYTE_SLICE_TO_STRING(8, waited.streams[STANDARD_STREAM_ERROR]));
    }
    return result;
}

// Read ELF fields independently of the object reader and image writer.
BUSTER_GLOBAL_LOCAL bool compiler_driver_tls_export_field(ByteSlice bytes, u64 offset, u32 width, u64* value)
{
    bool result = bytes.pointer && width <= sizeof(*value) && offset <= bytes.length && width <= bytes.length - offset;
    if (result)
    {
        *value = 0;
        memcpy(value, bytes.pointer + offset, width);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_tls_export_symbol(ByteSlice bytes, String8 name, bool present, bool initialized, u32 initial_value, u32 binding)
{
    u64 section_table = 0;
    u64 section_stride = 0;
    u64 section_count = 0;
    u64 program_table = 0;
    u64 program_stride = 0;
    u64 program_count = 0;
    bool result = bytes.pointer && bytes.length >= 64 && memcmp(bytes.pointer, "\177ELF\2\1", 6) == 0 &&
                  compiler_driver_tls_export_field(bytes, 40, 8, &section_table) && compiler_driver_tls_export_field(bytes, 58, 2, &section_stride) &&
                  compiler_driver_tls_export_field(bytes, 60, 2, &section_count) && compiler_driver_tls_export_field(bytes, 32, 8, &program_table) &&
                  compiler_driver_tls_export_field(bytes, 54, 2, &program_stride) && compiler_driver_tls_export_field(bytes, 56, 2, &program_count) &&
                  section_stride == 64 && program_stride == 56 && section_table <= bytes.length && program_table <= bytes.length &&
                  section_count <= (bytes.length - section_table) / section_stride && program_count <= (bytes.length - program_table) / program_stride;
    u64 tls_address = 0;
    u64 tls_file_size = 0;
    u64 tls_memory_size = 0;
    u64 tls_alignment = 0;
    u32 tls_count = 0;
    for (u64 index = 0; result && index < program_count; index += 1)
    {
        u64 header = program_table + index * program_stride;
        u64 type = 0;
        result = compiler_driver_tls_export_field(bytes, header, 4, &type);
        if (result && type == 7)
        {
            tls_count += 1;
            result = compiler_driver_tls_export_field(bytes, header + 16, 8, &tls_address) &&
                     compiler_driver_tls_export_field(bytes, header + 32, 8, &tls_file_size) &&
                     compiler_driver_tls_export_field(bytes, header + 40, 8, &tls_memory_size) && tls_file_size <= tls_memory_size &&
                     compiler_driver_tls_export_field(bytes, header + 48, 8, &tls_alignment) && tls_alignment &&
                     !(tls_alignment & (tls_alignment - 1)) && tls_address % tls_alignment == 0;
        }
    }
    result = result && tls_count == 1;
    u32 dynamic_tables = 0;
    u32 matches = 0;
    for (u64 index = 0; result && index < section_count; index += 1)
    {
        u64 header = section_table + index * section_stride;
        u64 type = 0;
        result = compiler_driver_tls_export_field(bytes, header + 4, 4, &type);
        if (result && type == 11)
        {
            dynamic_tables += 1;
            u64 offset = 0;
            u64 size = 0;
            u64 string_index = 0;
            u64 entry_size = 0;
            result = compiler_driver_tls_export_field(bytes, header + 24, 8, &offset) &&
                     compiler_driver_tls_export_field(bytes, header + 32, 8, &size) &&
                     compiler_driver_tls_export_field(bytes, header + 40, 4, &string_index) &&
                     compiler_driver_tls_export_field(bytes, header + 56, 8, &entry_size) &&
                     entry_size == 24 && size % 24 == 0 && offset <= bytes.length && size <= bytes.length - offset && string_index < section_count;
            u64 string_offset = 0;
            u64 string_size = 0;
            if (result)
            {
                u64 strings = section_table + string_index * section_stride;
                u64 string_type = 0;
                result = compiler_driver_tls_export_field(bytes, strings + 4, 4, &string_type) && string_type == 3 &&
                         compiler_driver_tls_export_field(bytes, strings + 24, 8, &string_offset) &&
                         compiler_driver_tls_export_field(bytes, strings + 32, 8, &string_size) &&
                         string_offset <= bytes.length && string_size <= bytes.length - string_offset;
            }
            for (u64 entry = 0; result && entry < size / 24; entry += 1)
            {
                u64 symbol = offset + entry * 24;
                u64 name_offset = 0;
                result = compiler_driver_tls_export_field(bytes, symbol, 4, &name_offset) && name_offset < string_size;
                if (result && name.length < string_size - name_offset && bytes.pointer[string_offset + name_offset + name.length] == 0 &&
                    memcmp(bytes.pointer + string_offset + name_offset, name.pointer, name.length) == 0)
                {
                    matches += 1;
                    u64 output_section = 0;
                    u64 value = 0;
                    u64 symbol_size = 0;
                    result = present && bytes.pointer[symbol + 4] == ((binding << 4) | 6) && (bytes.pointer[symbol + 5] & 3) == 0 &&
                             compiler_driver_tls_export_field(bytes, symbol + 6, 2, &output_section) && output_section != 0 && output_section < section_count &&
                             compiler_driver_tls_export_field(bytes, symbol + 8, 8, &value) &&
                             compiler_driver_tls_export_field(bytes, symbol + 16, 8, &symbol_size) && symbol_size == 4 &&
                             value <= tls_memory_size && symbol_size <= tls_memory_size - value;
                    if (result)
                    {
                        u64 section = section_table + output_section * section_stride;
                        u64 section_type = 0;
                        u64 flags = 0;
                        u64 address = 0;
                        u64 section_offset = 0;
                        u64 section_size = 0;
                        u64 section_alignment = 0;
                        result = compiler_driver_tls_export_field(bytes, section + 4, 4, &section_type) && section_type == (initialized ? 1u : 8u) &&
                                 compiler_driver_tls_export_field(bytes, section + 8, 8, &flags) && (flags & 0x403) == 0x403 &&
                                 compiler_driver_tls_export_field(bytes, section + 16, 8, &address) && address >= tls_address &&
                                 compiler_driver_tls_export_field(bytes, section + 24, 8, &section_offset) &&
                                 compiler_driver_tls_export_field(bytes, section + 32, 8, &section_size) && value >= address - tls_address &&
                                 compiler_driver_tls_export_field(bytes, section + 48, 8, &section_alignment) && section_alignment &&
                                 !(section_alignment & (section_alignment - 1)) && section_alignment <= tls_alignment && address % section_alignment == 0;
                        if (result)
                        {
                            u64 relative = value - (address - tls_address);
                            result = relative <= section_size && symbol_size <= section_size - relative;
                            if (result && initialized)
                            {
                                u64 stored = 0;
                                result = section_offset <= bytes.length && relative <= bytes.length - section_offset &&
                                         compiler_driver_tls_export_field(bytes, section_offset + relative, 4, &stored) && stored == initial_value;
                            }
                            else if (result)
                            {
                                result = value >= tls_file_size;
                            }
                        }
                    }
                }
            }
        }
    }
    result = result && dynamic_tables == 1 && matches == (present ? 1u : 0u);
    return result;
}


BUSTER_GLOBAL_LOCAL bool compiler_driver_tls_export_single_layout(ByteSlice bytes, bool initialized, u64 alignment)
{
    u64 programs = 0;
    u64 program_count = 0;
    u64 program_stride = 0;
    u64 sections = 0;
    u64 section_count = 0;
    u64 section_stride = 0;
    bool result = bytes.pointer && bytes.length >= 64 && memcmp(bytes.pointer, "\177ELF\2\1", 6) == 0 &&
                  compiler_driver_tls_export_field(bytes, 32, 8, &programs) && compiler_driver_tls_export_field(bytes, 54, 2, &program_stride) &&
                  compiler_driver_tls_export_field(bytes, 56, 2, &program_count) && program_stride == 56 && programs <= bytes.length &&
                  program_count <= (bytes.length - programs) / program_stride &&
                  compiler_driver_tls_export_field(bytes, 40, 8, &sections) && compiler_driver_tls_export_field(bytes, 58, 2, &section_stride) &&
                  compiler_driver_tls_export_field(bytes, 60, 2, &section_count) && section_stride == 64 && sections <= bytes.length &&
                  section_count <= (bytes.length - sections) / section_stride;
    u64 tls_address = 0;
    u64 tls_offset = 0;
    u64 tls_file_size = 0;
    u32 tls_count = 0;
    for (u64 index = 0; result && index < program_count; index += 1)
    {
        u64 header = programs + index * program_stride;
        u64 type = 0;
        result = compiler_driver_tls_export_field(bytes, header, 4, &type);
        if (result && type == 7)
        {
            u64 memory_size = 0;
            u64 declared_alignment = 0;
            tls_count += 1;
            result = compiler_driver_tls_export_field(bytes, header + 8, 8, &tls_offset) &&
                     compiler_driver_tls_export_field(bytes, header + 16, 8, &tls_address) &&
                     compiler_driver_tls_export_field(bytes, header + 32, 8, &tls_file_size) &&
                     compiler_driver_tls_export_field(bytes, header + 40, 8, &memory_size) &&
                     compiler_driver_tls_export_field(bytes, header + 48, 8, &declared_alignment) &&
                     declared_alignment == alignment && tls_address % alignment == 0 &&
                     tls_file_size == (initialized ? 4u : 0u) &&
                     (initialized ? memory_size >= 4 && memory_size <= alignment : memory_size == 4) &&
                     tls_offset <= bytes.length && tls_file_size <= bytes.length - tls_offset;
        }
    }
    result = result && tls_count == 1;
    bool template_loaded = !initialized;
    for (u64 index = 0; result && index < program_count; index += 1)
    {
        u64 header = programs + index * program_stride;
        u64 type = 0;
        result = compiler_driver_tls_export_field(bytes, header, 4, &type);
        if (result && type == 1)
        {
            u64 address = 0;
            u64 offset = 0;
            u64 file_size = 0;
            u64 memory_size = 0;
            u64 load_alignment = 0;
            result = compiler_driver_tls_export_field(bytes, header + 8, 8, &offset) &&
                     compiler_driver_tls_export_field(bytes, header + 16, 8, &address) &&
                     compiler_driver_tls_export_field(bytes, header + 32, 8, &file_size) &&
                     compiler_driver_tls_export_field(bytes, header + 40, 8, &memory_size) &&
                     compiler_driver_tls_export_field(bytes, header + 48, 8, &load_alignment) &&
                     file_size <= memory_size && offset <= bytes.length && file_size <= bytes.length - offset &&
                     load_alignment && !(load_alignment & (load_alignment - 1)) && address % load_alignment == offset % load_alignment;
            if (result && initialized && tls_address >= address && tls_offset >= offset)
            {
                u64 relative_address = tls_address - address;
                u64 relative_offset = tls_offset - offset;
                template_loaded |= relative_address == relative_offset && relative_offset <= file_size &&
                                   tls_file_size <= file_size - relative_offset && relative_address <= memory_size &&
                                   tls_file_size <= memory_size - relative_address;
            }
        }
    }
    u32 tls_sections = 0;
    for (u64 index = 0; result && index < section_count; index += 1)
    {
        u64 header = sections + index * section_stride;
        u64 flags = 0;
        result = compiler_driver_tls_export_field(bytes, header + 8, 8, &flags);
        if (result && (flags & 0x400))
        {
            u64 type = 0;
            u64 address = 0;
            u64 offset = 0;
            u64 size = 0;
            u64 declared_alignment = 0;
            tls_sections += 1;
            result = (flags & 3) == 3 && compiler_driver_tls_export_field(bytes, header + 4, 4, &type) && type == (initialized ? 1u : 8u) &&
                     compiler_driver_tls_export_field(bytes, header + 16, 8, &address) && address == tls_address &&
                     compiler_driver_tls_export_field(bytes, header + 24, 8, &offset) && offset == tls_offset &&
                     compiler_driver_tls_export_field(bytes, header + 32, 8, &size) && size == 4 &&
                     compiler_driver_tls_export_field(bytes, header + 48, 8, &declared_alignment) && declared_alignment == alignment &&
                     address % declared_alignment == 0;
        }
    }
    result = result && template_loaded && tls_sections == 1;
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_tls_export_single_classes(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 family = S8(BUSTER_HOST_C_COMPILER_ID);
    bool supported = string_equal(family, S8("GNU")) || string_equal(family, S8("Clang"));
    if (BUSTER_REQUIRE(arguments, supported))
    {
        String8 root = buster_test_temporary_path(arena, S8("buster-single-class-tls"), S8(""));
        OsDirectoryCreateResult created = os_make_directory(root);
        if (BUSTER_REQUIRE(arguments, created.error.v == 0))
        {
            String8 library_source = string_format_z(arena, S8("{S8}/library.c"), root);
            String8 library = string_format_z(arena, S8("{S8}/libsingle-tls.so"), root);
            String8 library_option = string_format_z(arena, S8("-L{S8}"), root);
            String8 library_program = S8("extern _Thread_local int single_tls;\n"
                "int check_single(int *p, int expected) { if (p != &single_tls || single_tls != expected) return 1; single_tls += 1; return 0; }\n");
            bool prepared = file_write(library_source, BUSTER_SLICE_TO_BYTE_SLICE(library_program));
            String8 build_library[] = {S8("-O2"), S8("-fPIC"), S8("-shared"), library_source, S8("-Wl,-soname,libsingle-tls.so"), S8("-o"), library};
            prepared = prepared && compiler_driver_tls_export_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(build_library));
            BUSTER_TEST(arguments, prepared);
            struct
            {
                u32 alignment;
                bool initialized;
                bool weak;
            } rows[] = {{32, true, false}, {32, false, false}, {8388608, false, false},
                        {8388608, true, false}, {32, true, true}, {32, false, true}};
            for (u32 row = 0; prepared && row < BUSTER_ARRAY_LENGTH(rows); row += 1)
            {
                TemporalArena temporary = arena_begin_temporal(arena);
                String8 source = string_format_z(arena, S8("{S8}/single-{u32}.c"), root, row);
                String8 object = string_format_z(arena, S8("{S8}/single-{u32}.o"), root, row);
                u32 initial_value = rows[row].initialized ? 42 : 0;
                String8 program = string_format(arena,
                    S8("_Thread_local int single_tls __attribute__((aligned({u32}){S8})) {S8};\n"
                       "int check_single(int *, int);\n"
                       "int main(void) {{ int failed = check_single(&single_tls, {u32}); return failed || single_tls != {u32}; }}\n"),
                    rows[row].alignment, rows[row].weak ? S8(", weak") : S8(""), rows[row].initialized ? S8("= 42") : S8(""),
                    initial_value, initial_value + 1);
                bool produced = file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(program));
                String8 build[] = {S8("-O2"), S8("-fno-pie"), S8("-c"), source, S8("-o"), object};
                produced = produced && compiler_driver_tls_export_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(build));
                BUSTER_TEST(arguments, produced);
                for (u32 export_all = 0; produced && export_all < 2; export_all += 1)
                {
                    String8 oracle = string_format_z(arena, S8("{S8}/host-{u32}-{u32}"), root, row, export_all);
                    String8 host_command[8] = {S8("-no-pie"), object, library_option, S8("-l:libsingle-tls.so"), S8("-o"), oracle};
                    u32 host_count = 6;
                    if (export_all) host_command[host_count++] = S8("-rdynamic");
                    bool host_linked = compiler_driver_tls_export_host(arguments, (SliceString8){host_command, host_count});
                    BUSTER_TEST(arguments, host_linked);
                    if (host_linked)
                    {
                        ByteSlice bytes = file_read(arena, oracle, (FileReadOptions){0});
                        BUSTER_TEST(arguments, compiler_driver_tls_export_single_layout(bytes, rows[row].initialized, rows[row].alignment));
                        BUSTER_TEST(arguments, compiler_driver_tls_export_symbol(bytes, S8("single_tls"), true, rows[row].initialized, initial_value, rows[row].weak ? 2 : 1));
                        BUSTER_TEST(arguments, compiler_driver_tls_export_run(arguments, oracle, root));
                    }
                    for (u32 imported = 0; imported < 2; imported += 1)
                    {
                        String8 output = string_format_z(arena, S8("{S8}/buster-{u32}-{u32}-{u32}"), root, row, export_all, imported);
                        String8 command[8] = {S8("-g0"), S8("-no-pie"), imported ? object : source, library_option, S8("-l:libsingle-tls.so"), S8("-o"), output};
                        u32 count = 7;
                        if (export_all) command[count++] = S8("-rdynamic");
                        CompilerDriverResult linked = compiler_driver_execute_invocation(arena,
                            compiler_driver_parse_arguments(arena, (SliceString8){command, count}));
                        BUSTER_TEST_RAW(arguments, linked.error == COMPILER_DRIVER_ERROR_NONE,
                            string_format(arena, S8("single TLS row {u32}, all {u32}, object {u32}: {S8}"), row, export_all, imported, linked.diagnostic));
                        if (linked.error == COMPILER_DRIVER_ERROR_NONE)
                        {
                            ByteSlice bytes = file_read(arena, output, (FileReadOptions){0});
                            BUSTER_TEST(arguments, compiler_driver_tls_export_single_layout(bytes, rows[row].initialized, rows[row].alignment));
                            BUSTER_TEST(arguments, compiler_driver_tls_export_symbol(bytes, S8("single_tls"), true, rows[row].initialized, initial_value, rows[row].weak ? 2 : 1));
                            BUSTER_TEST(arguments, compiler_driver_tls_export_run(arguments, output, root));
                        }
                    }
                }
                scratch_end(temporary);
            }
            BUSTER_TEST(arguments, os_directory_delete(root));
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_tls_export_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_tls_export_single_classes);
    Arena* arena = arguments->arena;
    String8 family = S8(BUSTER_HOST_C_COMPILER_ID);
    bool supported = string_equal(family, S8("GNU")) || string_equal(family, S8("Clang"));
    BUSTER_TEST(arguments, supported);
    if (!supported) arguments->show(arguments, S8("TLS export fixture requires a configured GNU or Clang host compiler\n"));
    if (supported)
    {
        String8 root = buster_test_temporary_path(arena, S8("buster-executable-tls-export"), S8(""));
        OsDirectoryCreateResult created = os_make_directory(root);
        BUSTER_TEST(arguments, created.error.v == 0);
        String8 library_source = string_format_z(arena, S8("{S8}/library.c"), root);
        String8 library = string_format_z(arena, S8("{S8}/libtls-exports.so"), root);
        String8 main_source = string_format_z(arena, S8("{S8}/main.c"), root);
        String8 library_option = string_format_z(arena, S8("-L{S8}"), root);
        String8 library_program = S8("extern _Thread_local int shared_tdata, shared_tbss;\n"
            "int read_tls(void) { if (shared_tdata != 42 || shared_tbss != 0) return 1; shared_tdata += 1; shared_tbss = 11; return 0; }\n");
        String8 main_program = S8("_Thread_local int shared_tdata __attribute__((aligned(16))) = 42;\n"
            "_Thread_local int shared_tbss __attribute__((aligned(32)));\n"
            "_Thread_local int spare_tls = 7;\n"
            "_Thread_local int private_tls = 9;\n"
            "__asm__(\".hidden private_tls\");\n"
            "int read_tls(void);\n"
            "int main(void) { return read_tls() || shared_tdata != 43 || shared_tbss != 11 || spare_tls != 7 || private_tls != 9; }\n");
        BUSTER_TEST(arguments, file_write(library_source, BUSTER_SLICE_TO_BYTE_SLICE(library_program)));
        BUSTER_TEST(arguments, file_write(main_source, BUSTER_SLICE_TO_BYTE_SLICE(main_program)));
        String8 build_library[] = {S8("-O2"), S8("-fPIC"), S8("-shared"), library_source, S8("-Wl,-soname,libtls-exports.so"), S8("-o"), library};
        bool built_library = compiler_driver_tls_export_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(build_library));
        BUSTER_TEST(arguments, built_library);
#if BUSTER_CPU_ARCH_X86_64
        u32 image_count = 2;
#else
        u32 image_count = 1; // The existing AArch64 writer supports fixed images.
#endif
        for (u32 pie = 0; built_library && pie < image_count; pie += 1)
        {
            String8 object = string_format_z(arena, S8("{S8}/main-{u32}.o"), root, pie);
            String8 build_object[] = {S8("-O2"), pie ? S8("-fPIE") : S8("-fno-pie"), S8("-c"), main_source, S8("-o"), object};
            bool built_object = compiler_driver_tls_export_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(build_object));
            BUSTER_TEST(arguments, built_object);
            for (u32 export_all = 0; built_object && export_all < 2; export_all += 1)
            {
                String8 oracle = string_format_z(arena, S8("{S8}/oracle-{u32}-{u32}"), root, pie, export_all);
                String8 host_link[8] = {pie ? S8("-pie") : S8("-no-pie"), object, library_option, S8("-l:libtls-exports.so"), S8("-o"), oracle};
                u32 host_count = 6;
                if (export_all) host_link[host_count++] = S8("-rdynamic");
                bool host_linked = compiler_driver_tls_export_host(arguments, (SliceString8){.pointer = host_link, .length = host_count});
                BUSTER_TEST(arguments, host_linked);
                if (host_linked)
                {
                    BUSTER_TEST(arguments, compiler_driver_tls_export_run(arguments, oracle, root));
                    ByteSlice bytes = file_read(arena, oracle, (FileReadOptions){0});
                    u64 image_type = 0;
                    BUSTER_TEST(arguments, compiler_driver_tls_export_field(bytes, 16, 2, &image_type) && image_type == (pie ? 3u : 2u));
                    BUSTER_TEST(arguments, compiler_driver_tls_export_symbol(bytes, S8("shared_tdata"), true, true, 42, 1));
                    BUSTER_TEST(arguments, compiler_driver_tls_export_symbol(bytes, S8("shared_tbss"), true, false, 0, 1));
                    BUSTER_TEST(arguments, compiler_driver_tls_export_symbol(bytes, S8("spare_tls"), export_all != 0, true, 7, 1));
                    BUSTER_TEST(arguments, compiler_driver_tls_export_symbol(bytes, S8("private_tls"), false, true, 9, 1));
                }
                for (u32 imported = 0; imported < 2; imported += 1)
                {
                    String8 output = string_format_z(arena, S8("{S8}/buster-{u32}-{u32}-{u32}"), root, pie, export_all, imported);
                    String8 command[8] = {pie ? S8("-pie") : S8("-no-pie"), imported ? object : main_source, library_option,
                                          S8("-l:libtls-exports.so"), S8("-o"), output};
                    u32 count = 6;
                    if (export_all) command[count++] = S8("-rdynamic");
                    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8){.pointer = command, .length = count});
                    CompilerDriverResult linked = compiler_driver_execute_invocation(arena, invocation);
                    BUSTER_TEST(arguments, linked.error == COMPILER_DRIVER_ERROR_NONE);
                    if (linked.error != COMPILER_DRIVER_ERROR_NONE)
                    {
                        arguments->show(arguments, S8("TLS export image {u32}, all {u32}, object {u32}: {S8}\n"), pie, export_all, imported, linked.diagnostic);
                    }
                    else
                    {
                        ByteSlice bytes = file_read(arena, output, (FileReadOptions){0});
                        u64 image_type = 0;
                        BUSTER_TEST(arguments, compiler_driver_tls_export_field(bytes, 16, 2, &image_type) && image_type == (pie ? 3u : 2u));
                        BUSTER_TEST(arguments, compiler_driver_tls_export_symbol(bytes, S8("shared_tdata"), true, true, 42, 1));
                        BUSTER_TEST(arguments, compiler_driver_tls_export_symbol(bytes, S8("shared_tbss"), true, false, 0, 1));
                        BUSTER_TEST(arguments, compiler_driver_tls_export_symbol(bytes, S8("spare_tls"), export_all != 0, true, 7, 1));
                        BUSTER_TEST(arguments, compiler_driver_tls_export_symbol(bytes, S8("private_tls"), false, true, 9, 1));
                        BUSTER_TEST(arguments, compiler_driver_tls_export_run(arguments, output, root));
                    }
                }
            }
        }
        for (u32 tls_definition = 0; tls_definition < 2; tls_definition += 1)
        {
            String8 definition = string_format_z(arena, S8("{S8}/definition-{u32}.c"), root, tls_definition);
            String8 reference = string_format_z(arena, S8("{S8}/reference-{u32}.c"), root, tls_definition);
            String8 object = string_format_z(arena, S8("{S8}/definition-{u32}.o"), root, tls_definition);
            String8 output = string_format_z(arena, S8("{S8}/mismatch-{u32}"), root, tls_definition);
            String8 definition_program = tls_definition ? S8("_Thread_local int mismatch_tls = 42;\n") : S8("int mismatch_tls = 42;\n");
            String8 reference_program = tls_definition ? S8("extern int mismatch_tls; int main(void) { return mismatch_tls; }\n")
                                                      : S8("extern _Thread_local int mismatch_tls; int main(void) { return mismatch_tls; }\n");
            BUSTER_TEST(arguments, file_write(definition, BUSTER_SLICE_TO_BYTE_SLICE(definition_program)));
            BUSTER_TEST(arguments, file_write(reference, BUSTER_SLICE_TO_BYTE_SLICE(reference_program)));
            String8 build[] = {S8("-O2"), S8("-fno-pie"), S8("-c"), definition, S8("-o"), object};
            bool produced = compiler_driver_tls_export_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(build));
            BUSTER_TEST(arguments, produced);
            if (produced)
            {
                String8 command[] = {S8("-no-pie"), reference, object, S8("-o"), output};
                CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                CompilerDriverResult linked = compiler_driver_execute_invocation(arena, invocation);
                BUSTER_TEST(arguments, linked.error == COMPILER_DRIVER_ERROR_LINK && linked.native_link.error == LINK_ERROR_TLS_SYMBOL_MISMATCH);
                BUSTER_STRING_TEST(arguments, linked.native_link.symbol, S8("mismatch_tls"));
                BUSTER_STRING_TEST(arguments, linked.diagnostic, S8("C object linking failed with TLS/non-TLS symbol mismatch on symbol 'mismatch_tls'"));
                BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(output));
                BUSTER_TEST(arguments, !linked.native_link.executable.pointer && !linked.native_link.executable.length);
            }
        }
        String8 hidden_source = string_format_z(arena, S8("{S8}/hidden.c"), root);
        String8 hidden_program = S8("extern _Thread_local int hidden_tls;\n"
                                   "__asm__(\".hidden hidden_tls\");\n"
                                   "int main(void) { return hidden_tls; }\n");
        BUSTER_TEST(arguments, file_write(hidden_source, BUSTER_SLICE_TO_BYTE_SLICE(hidden_program)));
        for (u32 pie = 0; pie < image_count; pie += 1)
        {
            String8 output = string_format_z(arena, S8("{S8}/hidden-{u32}"), root, pie);
            String8 command[] = {pie ? S8("-pie") : S8("-no-pie"), S8("-rdynamic"), hidden_source, library_option, S8("-l:libtls-exports.so"), S8("-o"), output};
            CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
            CompilerDriverResult linked = compiler_driver_execute_invocation(arena, invocation);
            BUSTER_TEST(arguments, linked.error == COMPILER_DRIVER_ERROR_LINK && linked.native_link.error == LINK_ERROR_UNRESOLVED_SYMBOL);
            BUSTER_STRING_TEST(arguments, linked.native_link.symbol, S8("hidden_tls"));
            BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(output));
            BUSTER_TEST(arguments, !linked.native_link.executable.pointer && !linked.native_link.executable.length);
        }
        BUSTER_TEST(arguments, os_directory_delete(root));
    }
    return result;
}
#endif

#if !BUSTER_ANDROID && !BUSTER_IOS
// Inspect the original ELF bytes without object_read's decoder or the writer.
BUSTER_GLOBAL_LOCAL ByteSlice compiler_driver_aarch64_printer_text(ByteSlice image)
{
    ByteSlice text = {0};
    bool valid = image.pointer && image.length >= 64 && memcmp(image.pointer, "\177ELF", 4) == 0 &&
                 image.pointer[4] == 2 && image.pointer[5] == 1;
    u16 type = 0;
    u16 machine = 0;
    u64 section_offset = 0;
    u16 section_size = 0;
    u16 section_count = 0;
    u16 names_index = 0;
    if (valid)
    {
        memcpy(&type, image.pointer + 16, 2);
        memcpy(&machine, image.pointer + 18, 2);
        memcpy(&section_offset, image.pointer + 40, 8);
        memcpy(&section_size, image.pointer + 58, 2);
        memcpy(&section_count, image.pointer + 60, 2);
        memcpy(&names_index, image.pointer + 62, 2);
        valid = type == 1 && machine == 183 && section_size == 64 && section_count && names_index < section_count &&
                section_offset <= image.length && (u64)section_count * 64 <= image.length - section_offset;
    }
    u64 names_offset = 0;
    u64 names_size = 0;
    if (valid)
    {
        u64 header = section_offset + (u64)names_index * 64;
        memcpy(&names_offset, image.pointer + header + 24, 8);
        memcpy(&names_size, image.pointer + header + 32, 8);
        valid = names_offset <= image.length && names_size <= image.length - names_offset;
    }
    u32 found = 0;
    for (u16 index = 0; valid && index < section_count; index += 1)
    {
        u64 header = section_offset + (u64)index * 64;
        u32 name = 0;
        memcpy(&name, image.pointer + header, 4);
        if (name <= names_size && 6 <= names_size - name &&
            memcmp(image.pointer + names_offset + name, ".text\0", 6) == 0)
        {
            u32 kind = 0;
            u64 flags = 0;
            u64 offset = 0;
            u64 size = 0;
            memcpy(&kind, image.pointer + header + 4, 4);
            memcpy(&flags, image.pointer + header + 8, 8);
            memcpy(&offset, image.pointer + header + 24, 8);
            memcpy(&size, image.pointer + header + 32, 8);
            valid = kind == 1 && (flags & 6) == 6 && offset <= image.length && size <= image.length - offset;
            if (valid)
            {
                text = (ByteSlice){.pointer = image.pointer + offset, .length = size};
                found += 1;
            }
        }
    }
    if (!valid || found != 1)
    {
        text = (ByteSlice){0};
    }
    return text;
}

// Match a bounded ELF string-table entry, including its terminator.
BUSTER_GLOBAL_LOCAL bool compiler_driver_aarch64_printer_elf_name(ByteSlice names, u32 offset, String8 expected)
{
    bool result = names.pointer && offset < names.length && expected.length < names.length - offset &&
                  memcmp(names.pointer + offset, expected.pointer, expected.length) == 0 &&
                  names.pointer[offset + expected.length] == 0;
    return result;
}

// This fixed ABI oracle reads symbol and RELA records directly. It never
// invokes the production object reader, writer or assembly symbol planner.
BUSTER_GLOBAL_LOCAL bool compiler_driver_aarch64_printer_anchor_metadata(ByteSlice image)
{
    bool valid = compiler_driver_aarch64_printer_text(image).length == 44;
    u64 table = 0;
    u16 count = 0;
    u16 section_names = 0;
    if (valid)
    {
        memcpy(&table, image.pointer + 40, 8);
        memcpy(&count, image.pointer + 60, 2);
        memcpy(&section_names, image.pointer + 62, 2);
    }
    ByteSlice names = {0};
    if (valid)
    {
        u64 offset = 0;
        u64 size = 0;
        u64 header = table + (u64)section_names * 64;
        memcpy(&offset, image.pointer + header + 24, 8);
        memcpy(&size, image.pointer + header + 32, 8);
        valid = offset <= image.length && size <= image.length - offset;
        if (valid) names = (ByteSlice){.pointer = image.pointer + offset, .length = size};
    }
    u32 text_index = UINT32_MAX;
    u32 data_index = UINT32_MAX;
    u32 symbols_index = UINT32_MAX;
    u32 symbol_names_index = UINT32_MAX;
    u32 rela_symbols = UINT32_MAX;
    u32 rela_owner = UINT32_MAX;
    ByteSlice symbols = {0};
    ByteSlice symbol_names = {0};
    ByteSlice relas = {0};
    for (u32 section = 1; valid && section < count; section += 1)
    {
        u64 header = table + (u64)section * 64;
        u32 name = 0;
        u32 type = 0;
        u32 link = 0;
        u32 info = 0;
        u64 flags = 0;
        u64 offset = 0;
        u64 size = 0;
        u64 entry_size = 0;
        memcpy(&name, image.pointer + header, 4);
        memcpy(&type, image.pointer + header + 4, 4);
        memcpy(&flags, image.pointer + header + 8, 8);
        memcpy(&offset, image.pointer + header + 24, 8);
        memcpy(&size, image.pointer + header + 32, 8);
        memcpy(&link, image.pointer + header + 40, 4);
        memcpy(&info, image.pointer + header + 44, 4);
        memcpy(&entry_size, image.pointer + header + 56, 8);
        bool payload = offset <= image.length && size <= image.length - offset;
        if (compiler_driver_aarch64_printer_elf_name(names, name, S8(".text")))
        {
            valid = text_index == UINT32_MAX && type == 1 && flags == 6 && size == 44 && payload;
            text_index = section;
        }
        if (compiler_driver_aarch64_printer_elf_name(names, name, S8(".rodata")))
        {
            valid = valid && data_index == UINT32_MAX && type == 1 && flags == 2 && size == 16 && payload;
            data_index = section;
            for (u64 byte = 0; valid && byte < size; byte += 1)
            {
                valid = image.pointer[offset + byte] == 0;
            }
        }
        if (type == 2)
        {
            valid = valid && symbols_index == UINT32_MAX && entry_size == 24 && size % 24 == 0 && payload && link < count;
            symbols_index = section;
            symbol_names_index = link;
            if (valid) symbols = (ByteSlice){.pointer = image.pointer + offset, .length = size};
        }
        if (compiler_driver_aarch64_printer_elf_name(names, name, S8(".rela.rodata")))
        {
            valid = valid && !relas.pointer && type == 4 && entry_size == 24 && size == 48 && payload;
            rela_symbols = link;
            rela_owner = info;
            if (valid) relas = (ByteSlice){.pointer = image.pointer + offset, .length = size};
        }
    }
    valid = valid && text_index != UINT32_MAX && data_index != UINT32_MAX && symbols.pointer &&
            symbol_names_index < count && relas.pointer && rela_symbols == symbols_index && rela_owner == data_index;
    if (valid)
    {
        u64 header = table + (u64)symbol_names_index * 64;
        u32 type = 0;
        u64 offset = 0;
        u64 size = 0;
        memcpy(&type, image.pointer + header + 4, 4);
        memcpy(&offset, image.pointer + header + 24, 8);
        memcpy(&size, image.pointer + header + 32, 8);
        valid = type == 3 && offset <= image.length && size <= image.length - offset;
        if (valid) symbol_names = (ByteSlice){.pointer = image.pointer + offset, .length = size};
    }
    valid = valid && symbols.length / 24 <= UINT32_MAX;
    u32 anchor = UINT32_MAX;
    u32 function = UINT32_MAX;
    u32 local_count = 0;
    u32 owner_count = 0;
    for (u32 symbol = 0; valid && symbol < symbols.length / 24; symbol += 1)
    {
        u8* record = symbols.pointer + (u64)symbol * 24;
        u32 name = 0;
        u16 section = 0;
        u64 value = 0;
        u64 size = 0;
        memcpy(&name, record, 4);
        memcpy(&section, record + 6, 2);
        memcpy(&value, record + 8, 8);
        memcpy(&size, record + 16, 8);
        if (record[4] == 3 && section == text_index)
        {
            valid = anchor == UINT32_MAX && value == 0 && size == 0;
            anchor = symbol;
        }
        if (compiler_driver_aarch64_printer_elf_name(symbol_names, name, S8("anchor_function")))
        {
            valid = valid && function == UINT32_MAX && record[4] == 0x12 && record[5] == 0 &&
                    section == text_index && value == 0 && size == 44;
            function = symbol;
        }
        if (compiler_driver_aarch64_printer_elf_name(symbol_names, name, S8("local_zero")))
        {
            valid = valid && record[4] == 2 && record[5] == 0 && section == text_index && value == 0 && size == 0;
            local_count += 1;
        }
        if (compiler_driver_aarch64_printer_elf_name(symbol_names, name, S8("anchor_refs")))
        {
            valid = valid && record[4] == 0x11 && record[5] == 0 && section == data_index && value == 0 && size == 16;
            owner_count += 1;
        }
    }
    valid = valid && anchor != UINT32_MAX && function != UINT32_MAX && local_count == 1 && owner_count == 1;
    for (u32 row = 0; valid && row < 2; row += 1)
    {
        u8* record = relas.pointer + row * 24;
        u64 offset = 0;
        u64 info = 0;
        s64 addend = 0;
        memcpy(&offset, record, 8);
        memcpy(&info, record + 8, 8);
        memcpy(&addend, record + 16, 8);
        valid = offset == (u64)row * 8 && (u32)info == 257 && info >> 32 == (row ? function : anchor) && addend == 4;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_aarch64_printer_process(UnitTestArguments* arguments, Arena* arena, bool* admission, SliceString8 command)
{
    bool success = false;
    if (*admission)
    {
        u64 limit = BUSTER_KB(64);
        ProcessSpawnResult child = os_process_spawn(command, (SliceString8){0}, (SliceString8){0},
            (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                  .use_process_environment = true, .new_process_group = true, .search_path = true,
                                  .capture_limits = {.per_stream = {[STANDARD_STREAM_OUTPUT] = limit, [STANDARD_STREAM_ERROR] = limit},
                                                     .total = limit * 2},
                                  .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL});
        if (child.handle)
        {
            ProcessWaitResult waited = os_process_wait_deadline(arena, child, 30000000);
            bool group_valid = !waited.process_tree_cleanup_failed && !waited.process_group_reservation_retained &&
                               !waited.process_group_ownership_lost;
            *admission &= group_valid;
            success = !waited.timed_out && waited.result == PROCESS_RESULT_SUCCESS && !waited.capture_failed &&
                      !waited.output_truncated && !waited.capture_limit_exceeded && group_valid;
            if (!success)
            {
                String8 diagnostic = BYTE_SLICE_TO_STRING(8, waited.streams[STANDARD_STREAM_ERROR]);
                arguments->show(arguments, S8("AARCH64_PRINTER_ASSEMBLER status={u32} timeout={u32} admission={u32}\n{S8}\n"),
                    waited.platform_status, (u32)waited.timed_out, (u32)*admission,
                    string_slice(diagnostic, 0, BUSTER_MIN(diagnostic.length, 4096)));
            }
        }
    }
    return success;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_aarch64_printer_assemble(UnitTestArguments* arguments, Arena* arena, bool* admission, String8 compiler,
                                                                  String8 compiler_argument, bool clang, String8 source, String8 output)
{
    String8 command[10];
    u32 count = 0;
    command[count++] = compiler;
    if (compiler_argument.length)
    {
        command[count++] = compiler_argument;
    }
    if (clang)
    {
        command[count++] = S8("-target");
        command[count++] = S8("aarch64-unknown-linux-gnu");
    }
    command[count++] = S8("-c");
    command[count++] = S8("-x");
    command[count++] = S8("assembler");
    command[count++] = source;
    command[count++] = S8("-o");
    command[count++] = output;
    return compiler_driver_aarch64_printer_process(arguments, arena, admission, (SliceString8){.pointer = command, .length = count});
}
#endif

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_aarch64_printer_roundtrip(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_ANDROID || BUSTER_IOS
    BUSTER_UNUSED(arguments);
#else
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    String8 compiler = {0};
    String8 compiler_argument = {0};
    bool clang = true;
#if defined(BUSTER_HOST_C_COMPILER) && !BUSTER_HOST_C_COMPILER_MSVC
    bool configured_clang = string_first_sequence(S8(BUSTER_HOST_C_COMPILER_ID), S8("Clang")) < S8(BUSTER_HOST_C_COMPILER_ID).length;
    if (configured_clang)
    {
        compiler = S8(BUSTER_HOST_C_COMPILER);
        compiler_argument = S8(BUSTER_HOST_C_COMPILER_ARG1);
    }
#if BUSTER_LINUX && BUSTER_CPU_ARCH_AARCH64
    else if (string_equal(S8(BUSTER_HOST_C_COMPILER_ID), S8("GNU")))
    {
        compiler = S8(BUSTER_HOST_C_COMPILER);
        compiler_argument = S8(BUSTER_HOST_C_COMPILER_ARG1);
        clang = false;
    }
#endif
#endif
    if (!compiler.length)
    {
        compiler = executable_resolve_in_path(arena, S8("clang"));
    }
    if (!compiler.length)
    {
        arguments->show(arguments, S8("AARCH64_PRINTER_ASSEMBLER unavailable; independent round trip not executed\n"));
    }
#if BUSTER_LINUX || BUSTER_MACOS
    BUSTER_TEST(arguments, compiler.length != 0);
#endif
    if (compiler.length)
    {
        bool admission = true;
        String8 original_path = buster_test_temporary_path(arena, S8("a64-printer-original"), S8(".s"));
        String8 printed_path = buster_test_temporary_path(arena, S8("a64-printer-printed"), S8(".s"));
        String8 reference_path = buster_test_temporary_path(arena, S8("a64-printer-reference"), S8(".o"));
        String8 reassembled_path = buster_test_temporary_path(arena, S8("a64-printer-reassembled"), S8(".o"));
        // Each literal case retains its original word after a fixed B +4 label seed.
        u32 words[] = {
            UINT32_C(0xd503201f), UINT32_C(0xd65f03c0), UINT32_C(0xd65f00a0), UINT32_C(0xd61f00a0), UINT32_C(0xd63f00a0),
            UINT32_C(0xd53bd043), UINT32_C(0x93407c43), UINT32_C(0x8a040043), UINT32_C(0xaa040043), UINT32_C(0xca040043),
            UINT32_C(0xaa0403e3), UINT32_C(0x8b040043), UINT32_C(0xcb040043), UINT32_C(0xeb04005f), UINT32_C(0x91000043),
            UINT32_C(0x910003e3), UINT32_C(0xf100005f), UINT32_C(0xd2800023), UINT32_C(0xf2800023), UINT32_C(0x92800023),
            UINT32_C(0x9ac42043), UINT32_C(0x9b047c43), UINT32_C(0x9a9f17e3), UINT32_C(0x39000043), UINT32_C(0x39400043),
            UINT32_C(0xb9000043), UINT32_C(0xf9000043), UINT32_C(0xf9400043), UINT32_C(0x29001043), UINT32_C(0xa9001043),
            UINT32_C(0xa9401043), UINT32_C(0x9adf23e3), UINT32_C(0x9b1f7fe3), UINT32_C(0x8b1f03ff), UINT32_C(0xd280003f),
            UINT32_C(0xa9407fe3), UINT32_C(0x93407c5f), UINT32_C(0x93407fe3), UINT32_C(0xd53bd05f), UINT32_C(0x390003ff),
            UINT32_C(0x14000000), UINT32_C(0x94000000), UINT32_C(0x54000000), UINT32_C(0xb4000003), UINT32_C(0xb5000003),
            UINT32_C(0x36000003), UINT32_C(0xb7000003), UINT32_C(0x14000001), UINT32_C(0x58000003), UINT32_C(0xd5033bbf),
            UINT32_C(0xd5033fbf), UINT32_C(0xd5033b9f), UINT32_C(0xd5033fdf), UINT32_C(0xd5033f5f), UINT32_C(0xd503305f),
            UINT32_C(0xd51bd040), UINT32_C(0xd5300000), UINT32_C(0x93401c43), UINT32_C(0x93403c43), UINT32_C(0x13001c43),
            UINT32_C(0x9347fc43), UINT32_C(0x93c21c43), UINT32_C(0x3d802780), UINT32_C(0xfd001380), UINT32_C(0xfd400380),
            UINT32_C(0x3dc02783), UINT32_C(0x39800843), UINT32_C(0x39c00843), UINT32_C(0xb9800843), UINT32_C(0xf9800843),
            UINT32_C(0x6d010440), UINT32_C(0xad010440), UINT32_C(0x69411043), UINT32_C(0xa8011043), UINT32_C(0xaa441c43),
            UINT32_C(0x8a041c43), UINT32_C(0xaa0407e3), UINT32_C(0x8b441c43), UINT32_C(0xeb041c5f), UINT32_C(0x91400043),
            UINT32_C(0x52c00023), UINT32_C(0x9a9fe7e3), UINT32_C(0x9a9ff7e3), UINT32_C(0x91800043), UINT32_C(0x0b048043),
            UINT32_C(0x72c00023), UINT32_C(0x12c00023), UINT32_C(0x14000001), UINT32_C(0x18000003), UINT32_C(0x14000001),
            UINT32_C(0x1c000003), UINT32_C(0x14000001), UINT32_C(0x5c000003), UINT32_C(0x14000001), UINT32_C(0x9c000003),
            UINT32_C(0x14000001), UINT32_C(0x98000003), UINT32_C(0x14000001), UINT32_C(0xd8000003),
        };
        u8 original_bytes[sizeof(words)];
        String8 original_source = S8(".text\n.p2align 2\n.global printer_original\nprinter_original:\n");
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(words); index += 1)
        {
            original_source = string_format(arena, S8("{S8}\t.word {u32}\n"), original_source, words[index]);
            for (u32 byte = 0; byte < 4; byte += 1)
            {
                original_bytes[index * 4 + byte] = (u8)(words[index] >> (byte * 8));
            }
        }
        bool original_ready = file_write(original_path, BUSTER_SLICE_TO_BYTE_SLICE(original_source));
        BUSTER_TEST(arguments, original_ready);
        FileReadResult original_readback = file_read_checked(arena, original_path, (FileReadOptions){0});
        BUSTER_TEST(arguments, original_readback.status == OS_FILE_READ_OK && original_readback.error.v == 0);
        BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, original_readback.bytes), original_source);
        original_ready &= original_readback.status == OS_FILE_READ_OK && original_readback.error.v == 0 &&
                          string_equal(BYTE_SLICE_TO_STRING(8, original_readback.bytes), original_source);
        bool reference_ready = original_ready && compiler_driver_aarch64_printer_assemble(arguments, arena, &admission, compiler, compiler_argument, clang,
                                                                                          original_path, reference_path);
        BUSTER_TEST(arguments, reference_ready);
        if (reference_ready)
        {
            ByteSlice original_image = file_read(arena, reference_path, (FileReadOptions){0});
            ByteSlice original_text = compiler_driver_aarch64_printer_text(original_image);
            BUSTER_TEST(arguments, original_text.pointer && original_text.length == sizeof(original_bytes));
            if (original_text.pointer && original_text.length == sizeof(original_bytes))
            {
                BUSTER_TEST(arguments, memcmp(original_text.pointer, original_bytes, sizeof(original_bytes)) == 0);
                ObjectSection section = {.name = S8(".text"), .kind = OBJECT_SECTION_TEXT, .alignment = 4,
                                         .data = {.pointer = original_bytes, .length = sizeof(original_bytes)}};
                ObjectSymbol symbol = {.name = S8("printer_original"), .section = 0, .size = sizeof(original_bytes),
                                       .kind = OBJECT_SYMBOL_FUNCTION, .global = true};
                ObjectFile object = {.target = {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX},
                                     .sections = &section, .section_count = 1, .symbols = &symbol, .symbol_count = 1};
                String8 printed = object_print_assembly(arena, &object);
                bool printed_ready = printed.length != 0 && file_write(printed_path, BUSTER_SLICE_TO_BYTE_SLICE(printed));
                BUSTER_TEST(arguments, printed_ready);
                bool reassembled = printed_ready && compiler_driver_aarch64_printer_assemble(arguments, arena, &admission, compiler, compiler_argument, clang,
                                                                                             printed_path, reassembled_path);
                BUSTER_TEST(arguments, reassembled);
                if (reassembled)
                {
                    ByteSlice actual = compiler_driver_aarch64_printer_text(file_read(arena, reassembled_path, (FileReadOptions){0}));
                    BUSTER_TEST(arguments, actual.pointer && actual.length == original_text.length);
                    if (actual.pointer && actual.length == original_text.length)
                    {
                        bool match = memcmp(actual.pointer, original_text.pointer, actual.length) == 0;
                        BUSTER_TEST(arguments, match);
                        arguments->show(arguments, S8("AARCH64_PRINTER_WORDS words={u32} bytes={u64} match={u32}\n"),
                            (u32)BUSTER_ARRAY_LENGTH(words), actual.length, (u32)match);
                    }
                }
            }
        }
        BUSTER_TEST(arguments, os_file_delete(original_path));
        BUSTER_TEST(arguments, os_file_delete(printed_path));
        BUSTER_TEST(arguments, os_file_delete(reference_path));
        BUSTER_TEST(arguments, os_file_delete(reassembled_path));

        // Original assembly establishes the section-base and public-function
        // relocation contract independently before the printer is observed.
        TemporalArena anchor_temporary = arena_begin_temporal(arena);
        String8 anchor_original = buster_test_temporary_path(arena, S8("a64-anchor-original"), S8(".s"));
        String8 anchor_printed = buster_test_temporary_path(arena, S8("a64-anchor-printed"), S8(".s"));
        String8 anchor_reference = buster_test_temporary_path(arena, S8("a64-anchor-reference"), S8(".o"));
        String8 anchor_observed = buster_test_temporary_path(arena, S8("a64-anchor-observed"), S8(".o"));
        String8 anchor_source = S8(
            ".text\n.p2align 2\n.globl anchor_function\n.type anchor_function,@function\nanchor_function:\n"
            ".type local_zero,@function\nlocal_zero:\n"
            ".word 0xd503201f\n.word 0xd65f03c0\n"
            ".word 0x110003e3\n.word 0x1100005f\n.word 0x11400443\n.word 0x91400443\n.word 0x9a9f17ff\n"
            ".word 0xa9c11063\n.word 0xa9c10c43\n.word 0xa8c11063\n.word 0xa9c17fe3\n"
            ".size anchor_function,44\n.size local_zero,0\n"
            ".section .rodata,\"a\",@progbits\n.p2align 3\n.globl anchor_refs\n.type anchor_refs,@object\nanchor_refs:\n"
            ".quad .text+4\n.quad anchor_function+4\n.size anchor_refs,16\n");
        u32 anchor_words[] = {UINT32_C(0xd503201f), UINT32_C(0xd65f03c0),
            UINT32_C(0x110003e3), UINT32_C(0x1100005f), UINT32_C(0x11400443), UINT32_C(0x91400443), UINT32_C(0x9a9f17ff),
            UINT32_C(0xa9c11063), UINT32_C(0xa9c10c43), UINT32_C(0xa8c11063), UINT32_C(0xa9c17fe3)};
        u8 anchor_bytes[44];
        for (u32 word = 0; word < BUSTER_ARRAY_LENGTH(anchor_words); word += 1)
        {
            for (u32 byte = 0; byte < 4; byte += 1)
            {
                anchor_bytes[word * 4 + byte] = (u8)(anchor_words[word] >> (8 * byte));
            }
        }
        bool anchor_written = file_write(anchor_original, BUSTER_SLICE_TO_BYTE_SLICE(anchor_source));
        BUSTER_TEST(arguments, anchor_written);
        if (anchor_written)
        {
            FileReadResult readback = file_read_checked(arena, anchor_original, (FileReadOptions){0});
            bool anchor_source_matches = readback.status == OS_FILE_READ_OK && readback.error.v == 0 &&
                                   string_equal(BYTE_SLICE_TO_STRING(8, readback.bytes), anchor_source);
            BUSTER_TEST(arguments, anchor_source_matches);
            if (anchor_source_matches)
            {
                bool reference_built = compiler_driver_aarch64_printer_assemble(arguments, arena, &admission,
                    compiler, compiler_argument, clang, anchor_original, anchor_reference);
                BUSTER_TEST(arguments, reference_built);
                if (reference_built)
                {
                    ByteSlice reference_image = file_read(arena, anchor_reference, (FileReadOptions){0});
                    ByteSlice reference_text = compiler_driver_aarch64_printer_text(reference_image);
                    bool original_text = reference_text.pointer && reference_text.length == sizeof(anchor_bytes) &&
                                         memcmp(reference_text.pointer, anchor_bytes, sizeof(anchor_bytes)) == 0;
                    bool original_metadata = compiler_driver_aarch64_printer_anchor_metadata(reference_image);
                    BUSTER_TEST(arguments, original_text);
                    BUSTER_TEST(arguments, original_metadata);
                    if (original_text && original_metadata)
                    {
                        ObjectSection anchor_sections[OBJECT_SECTION_COUNT] = {0};
                        for (u32 section = 0; section < OBJECT_SECTION_COUNT; section += 1)
                        {
                            anchor_sections[section].kind = (ObjectSectionKind)section;
                        }
                        u8 anchor_data[16] = {0};
                        anchor_sections[OBJECT_SECTION_TEXT] = (ObjectSection){.name = S8(".text"), .kind = OBJECT_SECTION_TEXT, .alignment = 4,
                            .data = {.pointer = anchor_bytes, .length = sizeof(anchor_bytes)}};
                        anchor_sections[OBJECT_SECTION_READ_ONLY_DATA] = (ObjectSection){.name = S8(".rodata"), .kind = OBJECT_SECTION_READ_ONLY_DATA,
                            .alignment = 8, .data = {.pointer = anchor_data, .length = sizeof(anchor_data)}};
                        ObjectSymbol anchor_symbols[] = {
                            {.name = S8(".text"), .section = OBJECT_SECTION_TEXT, .kind = OBJECT_SYMBOL_FUNCTION},
                            {.name = S8("anchor_function"), .section = OBJECT_SECTION_TEXT, .size = 44, .kind = OBJECT_SYMBOL_FUNCTION, .global = true},
                            {.name = S8("local_zero"), .section = OBJECT_SECTION_TEXT, .kind = OBJECT_SYMBOL_FUNCTION},
                            {.name = S8("anchor_refs"), .section = OBJECT_SECTION_READ_ONLY_DATA, .size = 16, .kind = OBJECT_SYMBOL_DATA, .global = true},
                        };
                        ObjectRelocation anchor_relocations[] = {
                            {.section = OBJECT_SECTION_READ_ONLY_DATA, .symbol = 0, .offset = 0, .addend = 4, .kind = OBJECT_RELOCATION_ABSOLUTE64},
                            {.section = OBJECT_SECTION_READ_ONLY_DATA, .symbol = 1, .offset = 8, .addend = 4, .kind = OBJECT_RELOCATION_ABSOLUTE64},
                        };
                        ObjectFile anchor_object = {.target = {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX},
                            .sections = anchor_sections, .section_count = BUSTER_ARRAY_LENGTH(anchor_sections),
                            .symbols = anchor_symbols, .symbol_count = BUSTER_ARRAY_LENGTH(anchor_symbols),
                            .relocations = anchor_relocations, .relocation_count = BUSTER_ARRAY_LENGTH(anchor_relocations)};
                        String8 assembly = object_print_assembly(arena, &anchor_object);
                        bool printed_written = assembly.length != 0 && file_write(anchor_printed, BUSTER_SLICE_TO_BYTE_SLICE(assembly));
                        BUSTER_TEST(arguments, printed_written);
                        if (printed_written)
                        {
                            FileReadResult printed_readback = file_read_checked(arena, anchor_printed, (FileReadOptions){0});
                            bool original_printed = printed_readback.status == OS_FILE_READ_OK && printed_readback.error.v == 0 &&
                                                    string_equal(BYTE_SLICE_TO_STRING(8, printed_readback.bytes), assembly);
                            BUSTER_TEST(arguments, original_printed);
                            if (original_printed)
                            {
                                bool observed_built = compiler_driver_aarch64_printer_assemble(arguments, arena, &admission,
                                    compiler, compiler_argument, clang, anchor_printed, anchor_observed);
                                BUSTER_TEST(arguments, observed_built);
                                if (observed_built)
                                {
                                    ByteSlice observed_image = file_read(arena, anchor_observed, (FileReadOptions){0});
                                    ByteSlice observed_text = compiler_driver_aarch64_printer_text(observed_image);
                                    bool exact_text = observed_text.pointer && observed_text.length == sizeof(anchor_bytes) &&
                                                      memcmp(observed_text.pointer, anchor_bytes, sizeof(anchor_bytes)) == 0;
                                    bool exact_metadata = compiler_driver_aarch64_printer_anchor_metadata(observed_image);
                                    BUSTER_TEST(arguments, exact_text);
                                    BUSTER_TEST(arguments, exact_metadata);
                                    arguments->show(arguments, S8("AARCH64_PRINTER_ANCHOR bytes={u64} text={u32} metadata={u32}\n"),
                                        observed_text.length, (u32)exact_text, (u32)exact_metadata);
                                }
                            }
                        }
                    }
                }
            }
        }
        BUSTER_TEST(arguments, os_file_delete(anchor_original));
        BUSTER_TEST(arguments, os_file_delete(anchor_printed));
        BUSTER_TEST(arguments, os_file_delete(anchor_reference));
        BUSTER_TEST(arguments, os_file_delete(anchor_observed));
        arena_set_position(arena, anchor_temporary.position);

        String8 source_path = buster_test_temporary_path(arena, S8("a64-printer-corpus"), S8(".c"));
        String8 source = S8(
            "void fence_sc(void) { __atomic_thread_fence(5); }\n"
            "int compare_failure(int *p, int value) { int expected = value; return __atomic_compare_exchange_n(p, &expected, value + 1, 0, 5, 5); }\n"
            "long sign_byte(signed char value) { return value; }\n"
            "long sign_half(short value) { return value; }\n"
            "long sign_word(int value) { return value; }\n"
            "struct H { double a, b; };\n"
            "struct H hfa_identity(struct H value) { return value; }\n"
            "struct H hfa_call(struct H value) { return hfa_identity(value); }\n"
            "double scalar_memory(volatile double *p, double value) { *p = value; return *p; }\n"
            "typedef float V4 __attribute__((vector_size(16)));\n"
            "void vector_store(volatile V4 *p, V4 value) { *p = value; }\n"
            "V4 vector_load(volatile V4 *p) { return *p; }\n"
            "double variadic_double(unsigned count, ...) { __builtin_va_list ap; __builtin_va_start(ap, count); double value = __builtin_va_arg(ap, double); __builtin_va_end(ap); return value; }\n");
        bool source_ready = file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(source));
        BUSTER_TEST(arguments, source_ready);
        FileReadResult readback = file_read_checked(arena, source_path, (FileReadOptions){0});
        BUSTER_TEST(arguments, readback.status == OS_FILE_READ_OK && readback.error.v == 0);
        BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, readback.bytes), source);
        source_ready &= readback.status == OS_FILE_READ_OK && readback.error.v == 0 &&
                        string_equal(BYTE_SLICE_TO_STRING(8, readback.bytes), source);
        String8 modes[] = {S8("-fregister-allocator=none"), S8("-fregister-allocator=mir-stack"),
                           S8("-fregister-allocator=fast"), S8("-fregister-allocator=quality")};
        String8 frontends[] = {S8("-fno-frontend-ssa"), S8("-ffrontend-ssa")};
        for (u32 mode = 0; admission && source_ready && mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
        {
            for (u32 form = 0; admission && form < BUSTER_ARRAY_LENGTH(frontends); form += 1)
            {
                TemporalArena row = arena_begin_temporal(arena);
                String8 object_path = buster_test_temporary_path(arena, S8("a64-printer-c-object"), S8(".o"));
                String8 assembly_path = buster_test_temporary_path(arena, S8("a64-printer-c-assembly"), S8(".s"));
                String8 observer_path = buster_test_temporary_path(arena, S8("a64-printer-c-observer"), S8(".o"));
                String8 outputs[] = {object_path, assembly_path};
                String8 actions[] = {S8("-c"), S8("-S")};
                bool ready = true;
                for (u32 action = 0; action < BUSTER_ARRAY_LENGTH(actions); action += 1)
                {
                    String8 command[] = {S8("-target"), S8("aarch64-linux"), S8("-nostdinc"), S8("-g0"), actions[action],
                                         modes[mode], frontends[form], S8("-fverify-codegen"), S8("-o"), outputs[action], source_path};
                    CompilerDriverInvocation invocation =
                        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                    CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, invocation);
                    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE,
                        string_format(arena, S8("AArch64 printer {S8} {S8} {S8}: {S8}"),
                            modes[mode], frontends[form], actions[action], compiled.diagnostic));
                    ready &= compiled.error == COMPILER_DRIVER_ERROR_NONE;
                }
                if (ready)
                {
                    ByteSlice encoded = compiler_driver_aarch64_printer_text(file_read(arena, object_path, (FileReadOptions){0}));
                    BUSTER_TEST(arguments, encoded.pointer && encoded.length != 0);
                    bool assembled = compiler_driver_aarch64_printer_assemble(arguments, arena, &admission, compiler, compiler_argument, clang,
                                                                              assembly_path, observer_path);
                    BUSTER_TEST(arguments, assembled);
                    if (assembled)
                    {
                        ByteSlice observed = compiler_driver_aarch64_printer_text(file_read(arena, observer_path, (FileReadOptions){0}));
                        BUSTER_TEST(arguments, observed.pointer && encoded.pointer && observed.length == encoded.length);
                        if (observed.pointer && encoded.pointer && observed.length == encoded.length)
                        {
                            bool match = memcmp(encoded.pointer, observed.pointer, encoded.length) == 0;
                            BUSTER_TEST_RAW(arguments, match,
                                string_format(arena, S8("AARCH64_PRINTER_ROUNDTRIP mode={u32} form={u32} bytes={u64} mismatch"),
                                    mode, form, encoded.length));
                            arguments->show(arguments, S8("AARCH64_PRINTER_ROUNDTRIP mode={u32} form={u32} bytes={u64} match={u32}\n"),
                                mode, form, encoded.length, (u32)match);
                        }
                    }
                }
                BUSTER_TEST(arguments, os_file_delete(object_path));
                BUSTER_TEST(arguments, os_file_delete(assembly_path));
                BUSTER_TEST(arguments, os_file_delete(observer_path));
                arena_set_position(arena, row.position);
            }
        }
        BUSTER_TEST(arguments, os_file_delete(source_path));
    }
    scratch_end(temporary);
#endif
    return result;
}


UnitTestResult compiler_driver_object_path_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_elf_empty_tests);
#endif
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_tls_export_tests);
#endif
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_aarch64_printer_roundtrip);
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
