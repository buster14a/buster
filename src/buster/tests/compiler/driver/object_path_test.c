#include <buster/tests/compiler/driver/object_path_test.h>
#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/compiler/assembly/assembly_unit.h>
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
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){0}, (OsFileAccess){ .read = 1 }, (OsFileCreateMode){0}, (OsFileShareFlags){0});
    bool result = file != 0;
    if (file)
    {
        BUSTER_CHECK(os_file_close(file));
    }
    return result;
}

// A failed compile names its command, every stage error and each published
// diagnostic record: hosted shards show only the log, and a failure whose
// flattened diagnostic is empty must still identify itself (#2789).
BUSTER_GLOBAL_LOCAL void compiler_driver_object_path_test_report(UnitTestArguments* arguments, Arena* arena, SliceString8 command_line,
                                                                  CompilerDriverResult compiled)
{
    String8 command = {0};
    for (u64 index = 0; index < command_line.length; index += 1)
    {
        command = string_format(arena, index ? S8("{S8} {S8}") : S8("{S8}{S8}"), command, command_line.pointer[index]);
    }
    arguments->show(arguments,
        S8("default object path compiler error: driver_error={u32} codegen_error={u32} object_error={u32} link_error={S8} link_symbol='{S8}' "
           "diagnostics={u32} fallbacks={u32} command='{S8}' diagnostic='{S8}'\n"),
        (u32)compiled.error, (u32)compiled.codegen_error, (u32)compiled.object_error, link_error_name(compiled.native_link.error),
        compiled.native_link.symbol, compiled.diagnostic_count, compiled.fallback_record_count, command, compiled.diagnostic);
    for (u32 index = 0; index < compiled.diagnostic_count; index += 1)
    {
        arguments->show(arguments, S8("default object path compiler diagnostic {u32}: {S8}\n"), index,
                        compiler_diagnostic_render(arena, compiled.diagnostics[index]));
    }
}

BUSTER_GLOBAL_LOCAL CompilerDriverError compiler_driver_object_path_test_compile(UnitTestArguments* arguments, SliceString8 command_line)
{
    CompilerDriverError result = COMPILER_DRIVER_ERROR_INVALID_INPUT;
    Arena* arena = arena_create((ArenaCreation){0});
    if (arena)
    {
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, command_line);
        CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, invocation);
        if (compiled.error != COMPILER_DRIVER_ERROR_NONE)
        {
            compiler_driver_object_path_test_report(arguments, arena, command_line, compiled);
        }
        result = compiled.error;
        BUSTER_CHECK(arena_destroy(arena, 1));
    }
    else
    {
        arguments->show(arguments, S8("default object path compiler error: cannot create the compile arena\n"));
    }
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

BUSTER_GLOBAL_LOCAL bool compiler_driver_aarch64_printer_process(UnitTestArguments* arguments, Arena* arena, bool* admission,
                                                                       SliceString8 command, String8 stage, String8 case_name,
                                                                       TestProcessObservation* saved_observation)
{
    bool success = false;
    u64 deadline = 30000000;
    u64 capture_mask = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR);
    TestProcessObservation observation = {
        .suite = S8("compiler-driver"),
        .fixture = S8("aarch64-printer-roundtrip"),
        .case_name = case_name,
        .stage = stage,
        .tool_role = command.length ? command.pointer[0] : S8("external assembler"),
        .expectation = S8("assembler exits successfully with complete capture and process-group cleanup"),
        .deadline_us = deadline,
        .capture_mask = capture_mask,
        .use_process_environment = true,
        .new_process_group = true,
        .search_path = true,
    };
    String8* argv_copy = arena_allocate(arena, String8, command.length);
    memcpy(argv_copy, command.pointer, sizeof(*argv_copy) * command.length);
    observation.argv = (SliceString8){.pointer = argv_copy, .length = command.length};
    if (*admission)
    {
        ProcessSpawnResult child = os_process_spawn(command, (SliceString8){0}, (SliceString8){0},
            (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                  .use_process_environment = true, .new_process_group = true, .search_path = true,
                                  .capture_limits = {.per_stream = {[STANDARD_STREAM_OUTPUT] = BUSTER_KB(64),
                                                                    [STANDARD_STREAM_ERROR] = BUSTER_KB(64)},
                                                     .total = BUSTER_KB(128)},
                                  .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL});
        observation.spawn_attempted = true;
        observation.process_observed = true;
        observation.spawn = child;
        if (child.handle)
        {
            u64 started = os_now_microseconds();
            ProcessWaitResult waited = os_process_wait_deadline(arena, child, deadline);
            observation.wait = waited;
            observation.wait_observed = true;
            observation.elapsed_us = os_now_microseconds() - started;
            observation.elapsed_observed = true;
            bool group_valid = !waited.process_tree_cleanup_failed && !waited.process_group_reservation_retained &&
                               !waited.process_group_ownership_lost;
            *admission &= group_valid;
            success = buster_test_process_observation_matches(&observation, PROCESS_RESULT_SUCCESS);
        }
        if (!success)
        {
            buster_test_process_failure_show(arguments, &observation);
        }
    }
    else
    {
        observation.expectation = S8("not launched because prior process cleanup failure blocked admission");
        buster_test_process_failure_show(arguments, &observation);
    }
    if (saved_observation)
    {
        *saved_observation = observation;
    }
    return success;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_aarch64_printer_assemble(UnitTestArguments* arguments, Arena* arena, bool* admission, String8 compiler,
                                                                  String8 compiler_argument, bool clang, String8 source, String8 output,
                                                                  String8 stage, String8 case_name, TestProcessObservation* observation)
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
    return compiler_driver_aarch64_printer_process(arguments, arena, admission,
        (SliceString8){.pointer = command, .length = count}, stage, case_name, observation);
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
#if BUSTER_LINUX || BUSTER_MACOS
        String8 unresolved_assembler[] = {S8("clang")};
        TestProcessObservation observation = {
            .suite = S8("compiler-driver"),
            .fixture = S8("aarch64-printer-roundtrip"),
            .case_name = S8("independent-assembler"),
            .stage = S8("resolve external assembler"),
            .tool_role = S8("independent AArch64 assembler"),
            .expectation = S8("clang resolves from the captured PATH for the required independent round trip"),
            .argv = BUSTER_ARRAY_TO_SLICE(unresolved_assembler),
            .deadline_us = 30000000,
            .capture_mask = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = true,
            .new_process_group = true,
            .search_path = true,
        };
        buster_test_process_failure_show(arguments, &observation);
#else
        arguments->show(arguments, S8("TEST_PREREQUISITE_NOT_RUN suite=compiler-driver fixture=aarch64-printer-roundtrip "
                                      "case=independent-assembler stage=resolve external assembler role=independent AArch64 assembler "
                                      "reason=optional oracle unavailable; argv[0] PATH token=clang\n"));
#endif
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
        TestProcessObservation original_observation = {0};
        bool reference_ready = original_ready && compiler_driver_aarch64_printer_assemble(arguments, arena, &admission, compiler, compiler_argument, clang,
                                                                                          original_path, reference_path, S8("reference assembly"),
                                                                                          S8("original-image"), &original_observation);
        BUSTER_TEST(arguments, reference_ready);
        if (reference_ready)
        {
            ByteSlice original_image = file_read(arena, reference_path, (FileReadOptions){0});
            ByteSlice original_text = compiler_driver_aarch64_printer_text(original_image);
            BUSTER_TEST(arguments, original_text.pointer && original_text.length == sizeof(original_bytes));
            if (original_text.pointer && original_text.length == sizeof(original_bytes))
            {
                bool original_matches = memcmp(original_text.pointer, original_bytes, sizeof(original_bytes)) == 0;
                BUSTER_TEST(arguments, original_matches);
                if (!original_matches)
                {
                    original_observation.stage = S8("semantic comparison");
                    original_observation.expectation = S8("reference assembler text matches the declared instruction words");
                    arguments->show(arguments, S8("AARCH64_PRINTER_COMPARE expected_bytes={u64} observed_bytes={u64} match={u32}\n"),
                                    sizeof(original_bytes), original_text.length, (u32)original_matches);
                    buster_test_process_failure_show(arguments, &original_observation);
                }
                ObjectSection section = {.name = S8(".text"), .kind = OBJECT_SECTION_TEXT, .alignment = 4,
                                         .data = {.pointer = original_bytes, .length = sizeof(original_bytes)}};
                ObjectSymbol symbol = {.name = S8("printer_original"), .section = 0, .size = sizeof(original_bytes),
                                       .kind = OBJECT_SYMBOL_FUNCTION, .global = true};
                ObjectFile object = {.target = {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX},
                                     .sections = &section, .section_count = 1, .symbols = &symbol, .symbol_count = 1};
                String8 printed = object_print_assembly(arena, &object);
                bool printed_ready = printed.length != 0 && file_write(printed_path, BUSTER_SLICE_TO_BYTE_SLICE(printed));
                BUSTER_TEST(arguments, printed_ready);
                TestProcessObservation reassembled_observation = {0};
                bool reassembled = printed_ready && compiler_driver_aarch64_printer_assemble(arguments, arena, &admission, compiler, compiler_argument, clang,
                                                                                              printed_path, reassembled_path, S8("printer output assembly"),
                                                                                              S8("printed-image"), &reassembled_observation);
                BUSTER_TEST(arguments, reassembled);
                if (reassembled)
                {
                    ByteSlice actual = compiler_driver_aarch64_printer_text(file_read(arena, reassembled_path, (FileReadOptions){0}));
                    bool actual_text_ready = actual.pointer && actual.length == original_text.length;
                    BUSTER_TEST(arguments, actual_text_ready);
                    if (!actual_text_ready)
                    {
                        reassembled_observation.stage = S8("semantic comparison");
                        reassembled_observation.expectation = S8("reassembled text has the reference byte count");
                        arguments->show(arguments, S8("AARCH64_PRINTER_COMPARE expected_bytes={u64} observed_bytes={u64}\n"),
                                        original_text.length, actual.length);
                        buster_test_process_failure_show(arguments, &reassembled_observation);
                    }
                    if (actual_text_ready)
                    {
                        bool match = memcmp(actual.pointer, original_text.pointer, actual.length) == 0;
                        BUSTER_TEST(arguments, match);
                        if (!match)
                        {
                            reassembled_observation.stage = S8("semantic comparison");
                            reassembled_observation.expectation = S8("reassembled bytes equal the reference assembler image");
                            arguments->show(arguments, S8("AARCH64_PRINTER_COMPARE expected_bytes={u64} observed_bytes={u64} match={u32}\n"),
                                            original_text.length, actual.length, (u32)match);
                            buster_test_process_failure_show(arguments, &reassembled_observation);
                        }
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
                TestProcessObservation anchor_reference_observation = {0};
                bool reference_built = compiler_driver_aarch64_printer_assemble(arguments, arena, &admission,
                    compiler, compiler_argument, clang, anchor_original, anchor_reference, S8("anchor reference assembly"),
                    S8("anchor-original-image"), &anchor_reference_observation);
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
                    if (!original_text || !original_metadata)
                    {
                        anchor_reference_observation.stage = S8("semantic comparison");
                        anchor_reference_observation.expectation = S8("reference assembler image has the declared anchor bytes and metadata");
                        arguments->show(arguments, S8("AARCH64_PRINTER_ANCHOR_COMPARE expected_bytes={u64} observed_bytes={u64} text={u32} metadata={u32}\n"),
                                        sizeof(anchor_bytes), reference_text.length, (u32)original_text, (u32)original_metadata);
                        buster_test_process_failure_show(arguments, &anchor_reference_observation);
                    }
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
                                TestProcessObservation anchor_observed_observation = {0};
                                bool observed_built = compiler_driver_aarch64_printer_assemble(arguments, arena, &admission,
                                    compiler, compiler_argument, clang, anchor_printed, anchor_observed, S8("anchor printer output assembly"),
                                    S8("anchor-printed-image"), &anchor_observed_observation);
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
                                    if (!exact_text || !exact_metadata)
                                    {
                                        anchor_observed_observation.stage = S8("semantic comparison");
                                        anchor_observed_observation.expectation = S8("printed anchor image preserves bytes, symbols, and relocations");
                                        arguments->show(arguments, S8("AARCH64_PRINTER_ANCHOR_COMPARE expected_bytes={u64} observed_bytes={u64} text={u32} metadata={u32}\n"),
                                                        sizeof(anchor_bytes), observed_text.length, (u32)exact_text, (u32)exact_metadata);
                                        buster_test_process_failure_show(arguments, &anchor_observed_observation);
                                    }
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
        String8 modes[] = {S8("-fregister-allocator=fast"), S8("-fregister-allocator=quality")};
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
                                         modes[mode], frontends[form], S8("-fno-machine-fallback"), S8("-fverify-codegen"), S8("-o"), outputs[action], source_path};
                    CompilerDriverInvocation invocation =
                        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                    CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, invocation);
                    if (compiled.error != COMPILER_DRIVER_ERROR_NONE)
                    {
                        String8 case_name = string_format(arena, S8("{S8}/{S8}/{S8}"),
                            modes[mode], frontends[form], actions[action]);
                        arguments->show(arguments,
                            S8("BUSTER_DRIVER_FAILURE suite=compiler-driver fixture=aarch64-printer-roundtrip case={S8} "
                               "stage=compiler-driver role=Buster compiler error={u32} argument_count={u64}\n"),
                            case_name, (u32)compiled.error, (u64)BUSTER_ARRAY_LENGTH(command));
                        for (u64 argument_index = 0; argument_index < BUSTER_ARRAY_LENGTH(command); argument_index += 1)
                        {
                            arguments->show(arguments, S8("driver_arg[{u64}]={S8}\n"), argument_index, command[argument_index]);
                        }
                        arguments->show(arguments, S8("driver diagnostic: {S8}\n"), compiled.diagnostic);
                    }
                    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE,
                        string_format(arena, S8("AArch64 printer {S8} {S8} {S8}: {S8}"),
                            modes[mode], frontends[form], actions[action], compiled.diagnostic));
                    ready &= compiled.error == COMPILER_DRIVER_ERROR_NONE;
                }
                if (ready)
                {
                    ByteSlice encoded = compiler_driver_aarch64_printer_text(file_read(arena, object_path, (FileReadOptions){0}));
                    BUSTER_TEST(arguments, encoded.pointer && encoded.length != 0);
                    String8 case_name = string_format(arena, S8("{S8}/{S8}/assembly"),
                        modes[mode], frontends[form]);
                    TestProcessObservation observed_process = {0};
                    bool assembled = compiler_driver_aarch64_printer_assemble(arguments, arena, &admission, compiler, compiler_argument, clang,
                                                                               assembly_path, observer_path, S8("generated assembly"),
                                                                               case_name, &observed_process);
                    BUSTER_TEST(arguments, assembled);
                    if (assembled)
                    {
                        ByteSlice observed = compiler_driver_aarch64_printer_text(file_read(arena, observer_path, (FileReadOptions){0}));
                        bool observed_ready = observed.pointer && encoded.pointer && observed.length == encoded.length;
                        BUSTER_TEST(arguments, observed_ready);
                        if (!observed_ready)
                        {
                            observed_process.stage = S8("semantic comparison");
                            observed_process.expectation = S8("assembled printer output has the reference object byte count");
                            arguments->show(arguments, S8("AARCH64_PRINTER_ROUNDTRIP_COMPARE expected_bytes={u64} observed_bytes={u64}\n"),
                                            encoded.length, observed.length);
                            buster_test_process_failure_show(arguments, &observed_process);
                        }
                        if (observed_ready)
                        {
                            bool match = memcmp(encoded.pointer, observed.pointer, encoded.length) == 0;
                            BUSTER_TEST_RAW(arguments, match,
                                string_format(arena, S8("AARCH64_PRINTER_ROUNDTRIP mode={u32} form={u32} bytes={u64} mismatch"),
                                    mode, form, encoded.length));
                            if (!match)
                            {
                                observed_process.stage = S8("semantic comparison");
                                observed_process.expectation = S8("assembled printer output matches the generated object bytes");
                                arguments->show(arguments, S8("AARCH64_PRINTER_ROUNDTRIP_COMPARE expected_bytes={u64} observed_bytes={u64} match={u32}\n"),
                                                encoded.length, observed.length, (u32)match);
                                buster_test_process_failure_show(arguments, &observed_process);
                            }
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


#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
BUSTER_GLOBAL_LOCAL bool compiler_driver_stack_test_host(UnitTestArguments* arguments, SliceString8 options)
{
    String8 command[16] = {S8(BUSTER_HOST_C_COMPILER)};
    u32 count = 1;
    String8 argument = S8(BUSTER_HOST_C_COMPILER_ARG1);
    if (argument.length) command[count++] = argument;
    for (u64 index = 0; index < options.length && count < BUSTER_ARRAY_LENGTH(command); index += 1)
    {
        command[count++] = options.pointer[index];
    }
    ProcessSpawnResult spawned = os_process_spawn((SliceString8){.pointer = command, .length = count}, (SliceString8){0}, (SliceString8){0},
        (ProcessSpawnOptions){.use_process_environment = true, .new_process_group = true, .search_path = true,
                              .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
    bool result = spawned.handle != 0;
    if (result)
    {
        ProcessWaitResult waited = os_process_wait_deadline(arguments->arena, spawned, 30000000);
        result = waited.result == PROCESS_RESULT_SUCCESS;
        if (!result)
        {
            arguments->show(arguments, S8("ELF stack host compiler failed: {S8}\n"), BYTE_SLICE_TO_STRING(8, waited.streams[STANDARD_STREAM_ERROR]));
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_stack_test_image(ByteSlice image)
{
    bool result = image.pointer && image.length >= 64 && memcmp(image.pointer, "\177ELF\2\1", 6) == 0;
    u64 table = 0;
    u16 size = 0;
    u16 count = 0;
    if (result)
    {
        memcpy(&table, image.pointer + 32, sizeof(table));
        memcpy(&size, image.pointer + 54, sizeof(size));
        memcpy(&count, image.pointer + 56, sizeof(count));
        result = size == 56 && table <= image.length && (u64)count * size <= image.length - table;
    }
    u32 found = 0;
    for (u16 index = 0; result && index < count; index += 1)
    {
        u64 header = table + (u64)index * size;
        u32 type = 0;
        u32 flags = 0;
        memcpy(&type, image.pointer + header, sizeof(type));
        memcpy(&flags, image.pointer + header + 4, sizeof(flags));
        if (type == UINT32_C(0x6474e551))
        {
            found += 1;
            result = flags == 6;
        }
    }
    return result && found == 1;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_elf_stack_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 compiler = S8(BUSTER_HOST_C_COMPILER_ID);
    bool supported = string_equal(compiler, S8("GNU")) || string_equal(compiler, S8("Clang")) || string_equal(compiler, S8("AppleClang"));
    BUSTER_TEST(arguments, supported);
    if (supported)
    {
        String8 root = buster_test_temporary_path(arena, S8("buster-elf-stack"), S8(""));
        os_make_directory(root);
        String8 plugin = string_format_z(arena, S8("{S8}/plugin.c"), root);
        String8 main_source = string_format_z(arena, S8("{S8}/main.c"), root);
        String8 input = string_format_z(arena, S8("{S8}/stack.s"), root);
        String8 object = string_format_z(arena, S8("{S8}/plugin.o"), root);
        String8 shared = string_format_z(arena, S8("{S8}/plugin.so"), root);
        String8 input_object = string_format_z(arena, S8("{S8}/stack.o"), root);
        String8 executable = string_format_z(arena, S8("{S8}/program"), root);
        BUSTER_TEST(arguments, file_write(plugin, BUSTER_SLICE_TO_BYTE_SLICE(S8("int plugin_value(void) { return 42; }\n"))));
        BUSTER_TEST(arguments, file_write(main_source, BUSTER_SLICE_TO_BYTE_SLICE(S8("extern int puts(const char*); int main(void) { return puts(\"stack-contract\") < 0; }\n"))));
        String8 allocators[] = {S8("fast"), S8("quality")};
        for (u32 allocator = 0; allocator < BUSTER_ARRAY_LENGTH(allocators); allocator += 1)
        {
            for (u32 pic = 0; pic < 2; pic += 1)
            {
                String8 mode = string_format_z(arena, S8("-fregister-allocator={S8}"), allocators[allocator]);
                String8 compile[] = {mode, pic ? S8("-fPIC") : S8("-fno-pic"), S8("-c"), plugin, S8("-o"), object};
                CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(compile));
                CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, invocation);
                BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
                if (compiled.error == COMPILER_DRIVER_ERROR_NONE)
                {
                    String8 host[] = {S8("-shared"), object, S8("-o"), shared};
                    bool linked = compiler_driver_stack_test_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host));
                    BUSTER_TEST(arguments, linked);
                    if (linked) BUSTER_TEST(arguments, compiler_driver_stack_test_image(file_read(arena, shared, (FileReadOptions){0})));
                }
            }
        }
        for (u32 requested = 0; requested < 2; requested += 1)
        {
            String8 assembly = string_format(arena, S8(".text\n.globl stack_entry\nstack_entry:\nret\n.section .note.GNU-stack,\"{S8}\",@progbits\n"), requested ? S8("x") : S8(""));
            BUSTER_TEST(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(assembly)));
            String8 host[] = {S8("-c"), S8("-x"), S8("assembler"), input, S8("-o"), input_object};
            bool produced = compiler_driver_stack_test_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host));
            BUSTER_TEST(arguments, produced);
            if (produced)
            {
                Target target = target_native;
                ObjectFile read = object_read(arena, file_read(arena, input_object, (FileReadOptions){0}), target);
                BUSTER_TEST(arguments, read.error == OBJECT_ERROR_NONE && read.requires_executable_stack == (requested != 0));
                String8 output = requested ? string_format_z(arena, S8("{S8}/refused"), root) : executable;
                String8 link[] = {main_source, input_object, S8("-o"), output};
                CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(link));
                CompilerDriverResult linked = compiler_driver_execute_invocation(arena, invocation);
                if (requested)
                {
                    BUSTER_TEST(arguments, linked.error == COMPILER_DRIVER_ERROR_LINK && linked.native_link.error == LINK_ERROR_UNSUPPORTED_FEATURE);
                    BUSTER_STRING_TEST(arguments, linked.native_link.symbol, string_format(arena, S8("{S8}: executable-stack request (.note.GNU-stack) is unsupported"), input_object));
                    BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(output));
                }
                else
                {
                    BUSTER_TEST(arguments, linked.error == COMPILER_DRIVER_ERROR_NONE && compiler_driver_stack_test_image(linked.native_link.executable));
                    if (linked.error == COMPILER_DRIVER_ERROR_NONE)
                    {
                        String8 run[] = {output};
                        ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                            (ProcessSpawnOptions){.use_process_environment = true, .new_process_group = true});
                        BUSTER_TEST(arguments, spawned.handle && os_process_wait_deadline(arena, spawned, 30000000).result == PROCESS_RESULT_SUCCESS);
                    }
                }
                String8 assemble[] = {S8("-c"), input, S8("-o"), object};
                invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assemble));
                CompilerDriverResult assembled = compiler_driver_execute_invocation(arena, invocation);
                BUSTER_TEST(arguments, assembled.error == COMPILER_DRIVER_ERROR_NONE && assembled.object.requires_executable_stack == (requested != 0));
                if (assembled.error == COMPILER_DRIVER_ERROR_NONE)
                {
                    read = object_read(arena, file_read(arena, object, (FileReadOptions){0}), target);
                    BUSTER_TEST(arguments, read.error == OBJECT_ERROR_NONE && read.requires_executable_stack == (requested != 0));
                }
            }
        }
        BUSTER_TEST(arguments, os_directory_delete(root));
    }
    return result;
}

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

#if BUSTER_CPU_ARCH_X86_64
BUSTER_GLOBAL_LOCAL String8 compiler_driver_elf_symbol_section_name(ObjectFile object, ObjectSymbol symbol)
{
    return symbol.section < object.section_count ? object.sections[symbol.section].name : (String8){0};
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_string_contains(String8 text, String8 value)
{
    bool found = false;
    if (value.length <= text.length)
    {
        for (u64 index = 0; index <= text.length - value.length && !found; index += 1)
        {
            found = memcmp(text.pointer + index, value.pointer, value.length) == 0;
        }
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_modifier_rejection(String8 source, String8 output,
                                                                 String8 assembly, String8 diagnostic)
{
    bool result = file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(assembly));
    Arena* compile_arena = result ? arena_create((ArenaCreation){0}) : 0;
    if (compile_arena)
    {
        String8 command[] = {S8("-g0"), S8("-c"), source, S8("-o"), output};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(compile_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        CompilerDriverResult compiled = compiler_driver_execute_invocation(compile_arena, invocation);
        result = compiled.error != COMPILER_DRIVER_ERROR_NONE &&
                 compiler_driver_elf_string_contains(compiled.diagnostic, diagnostic);
        BUSTER_CHECK(arena_destroy(compile_arena, 1));
    }
    else
    {
        result = false;
    }
    return result;
}

// GOT type relaxation can be lost when the printer has to use a raw data
// directive. The family may differ, but every field and symbol property stays
// exact in the comparison below.
BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_assembly_relocation_objects_match(Arena* arena, String8 expected_path, String8 actual_path,
                                                                                       String8* mismatch_description)
{
    Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX};
    ObjectFile expected = object_read(arena, file_read(arena, expected_path, (FileReadOptions){0}), target);
    ObjectFile actual = object_read(arena, file_read(arena, actual_path, (FileReadOptions){0}), target);
    String8 mismatch = string_format(arena, S8("object reader errors expected={u32} actual={u32}"),
                                     (u32)expected.error, (u32)actual.error);
    bool valid = expected.error == OBJECT_ERROR_NONE && actual.error == OBJECT_ERROR_NONE &&
                 expected.section_count == actual.section_count && expected.symbol_count == actual.symbol_count &&
                 expected.relocation_count == actual.relocation_count;
    if (expected.error == OBJECT_ERROR_NONE && actual.error == OBJECT_ERROR_NONE)
    {
        if (expected.section_count == actual.section_count && expected.symbol_count == actual.symbol_count &&
            expected.relocation_count == actual.relocation_count)
        {
            mismatch = S8("object metadata matches");
        }
        else
        {
            mismatch = string_format(arena, S8("counts sections={u32}/{u32} symbols={u32}/{u32} relocations={u32}/{u32}"),
                                     expected.section_count, actual.section_count, expected.symbol_count, actual.symbol_count,
                                     expected.relocation_count, actual.relocation_count);
        }
    }
    for (u32 index = 0; index < expected.section_count && valid; index += 1)
    {
        ObjectSection left = expected.sections[index];
        u32 match = UINT32_MAX;
        for (u32 candidate = 0; candidate < actual.section_count; candidate += 1)
        {
            if (string_equal(left.name, actual.sections[candidate].name))
            {
                match = candidate;
                break;
            }
        }
        if (match == UINT32_MAX)
        {
            valid = false;
            mismatch = string_format(arena, S8("section {S8} is absent"), left.name);
        }
        else
        {
            ObjectSection right = actual.sections[match];
            valid = left.kind == right.kind && left.alignment == right.alignment && left.virtual_size == right.virtual_size &&
                    left.data.length == right.data.length &&
                    (!left.data.length || memcmp(left.data.pointer, right.data.pointer, left.data.length) == 0);
            if (!valid)
            {
                mismatch = string_format(arena, S8("section {S8}: kind={u32}/{u32} align={u32}/{u32} vsize={u64}/{u64} bytes={u64}/{u64}"),
                                         left.name, (u32)left.kind, (u32)right.kind, left.alignment, right.alignment,
                                         left.virtual_size, right.virtual_size, left.data.length, right.data.length);
            }
        }
    }
    for (u32 index = 0; index < expected.symbol_count && valid; index += 1)
    {
        ObjectSymbol left = expected.symbols[index];
        u32 match = UINT32_MAX;
        for (u32 candidate = 0; candidate < actual.symbol_count; candidate += 1)
        {
            if (string_equal(left.name, actual.symbols[candidate].name))
            {
                match = candidate;
                break;
            }
        }
        if (match == UINT32_MAX)
        {
            valid = false;
            mismatch = string_format(arena, S8("symbol {S8} is absent"), left.name);
        }
        else
        {
            ObjectSymbol right = actual.symbols[match];
            String8 left_section = compiler_driver_elf_symbol_section_name(expected, left);
            String8 right_section = compiler_driver_elf_symbol_section_name(actual, right);
            valid = left.value == right.value && left.size == right.size &&
                    string_equal(left_section, right_section) &&
                    left.kind == right.kind && left.global == right.global && left.weak == right.weak &&
                    left.hidden == right.hidden && left.thread_local_state == right.thread_local_state &&
                    left.untyped == right.untyped && left.undefined_untyped == right.undefined_untyped &&
                    left.indirect == right.indirect;
            if (!valid)
            {
                mismatch = string_format(arena, S8("symbol {S8}: value={u64}/{u64} size={u64}/{u64} section={S8}/{S8} kind={u32}/{u32} global={u32}/{u32} weak={u32}/{u32} hidden={u32}/{u32} tls={u32}/{u32} untyped={u32}/{u32} undefined_untyped={u32}/{u32} indirect={u32}/{u32}"),
                                         left.name, left.value, right.value, left.size, right.size, left_section, right_section,
                                         (u32)left.kind, (u32)right.kind, (u32)left.global, (u32)right.global,
                                         (u32)left.weak, (u32)right.weak, (u32)left.hidden, (u32)right.hidden,
                                         (u32)left.thread_local_state, (u32)right.thread_local_state, (u32)left.untyped,
                                         (u32)right.untyped, (u32)left.undefined_untyped, (u32)right.undefined_untyped,
                                         (u32)left.indirect, (u32)right.indirect);
            }
        }
    }
    bool* matched_relocations = arena_allocate_zeroed(arena, bool, actual.relocation_count ? actual.relocation_count : 1);
    for (u32 index = 0; index < expected.relocation_count && valid; index += 1)
    {
        ObjectRelocation left = expected.relocations[index];
        String8 left_section = expected.sections[left.section].name;
        String8 left_symbol = expected.symbols[left.symbol].name;
        bool found = false;
        for (u32 candidate = 0; candidate < actual.relocation_count && !found; candidate += 1)
        {
            ObjectRelocation right = actual.relocations[candidate];
            bool same_got_family = object_relocation_kind_is_x86_got(left.kind) && object_relocation_kind_is_x86_got(right.kind);
            bool kind_matches = left.kind == right.kind || same_got_family;
            bool match = !matched_relocations[candidate] && left.offset == right.offset && left.addend == right.addend &&
                         kind_matches && string_equal(left_section, actual.sections[right.section].name) &&
                         string_equal(left_symbol, actual.symbols[right.symbol].name);
            if (match)
            {
                matched_relocations[candidate] = true;
                found = true;
            }
        }
        valid = found;
        if (!found)
        {
            mismatch = string_format(arena, S8("relocation {S8}+{u64} symbol={S8} addend_bits={u32} kind={u32} is absent"),
                                     left_section, left.offset, left_symbol, (u32)left.addend, (u32)left.kind);
        }
    }
    *mismatch_description = mismatch;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_object_undefined_symbol_is(ObjectFile object, String8 name, ObjectSymbolKind kind,
                                                                              u8 thread_local_state)
{
    bool found = false;
    bool matches = false;
    bool valid = object.error == OBJECT_ERROR_NONE && (!object.symbol_count || object.symbols);
    for (u32 index = 0; index < object.symbol_count && !found && valid; index += 1)
    {
        ObjectSymbol symbol = object.symbols[index];
        if (string_equal(symbol.name, name))
        {
            found = true;
            matches = symbol.section == OBJECT_SECTION_UNDEFINED && symbol.kind == kind &&
                      symbol.thread_local_state == thread_local_state && !symbol.untyped && !symbol.undefined_untyped;
        }
    }
    return valid && found && matches;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_object_has_relocation(ObjectFile object, ObjectRelocationKind kind)
{
    bool found = false;
    for (u32 index = 0; index < object.relocation_count; index += 1)
    {
        found = found || object.relocations[index].kind == kind;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_object_has_got_relocation(ObjectFile object)
{
    return compiler_driver_elf_object_has_relocation(object, OBJECT_RELOCATION_X86_64_GOTPCREL) ||
           compiler_driver_elf_object_has_relocation(object, OBJECT_RELOCATION_X86_64_GOTPCRELX) ||
           compiler_driver_elf_object_has_relocation(object, OBJECT_RELOCATION_X86_64_REX_GOTPCRELX);
}

// The C object's own GOT/TLS/PLT listing must be accepted by both assemblers
// and retain its ELF relocation metadata before an independent link and run.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_x86_elf_modifier_roundtrip(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 compiler = S8(BUSTER_HOST_C_COMPILER_ID);
    bool supported = string_equal(compiler, S8("GNU")) || string_equal(compiler, S8("Clang"));
    BUSTER_TEST(arguments, supported);
    if (!supported) arguments->show(arguments, S8("x86-64 ELF relocation round-trip requires a configured GNU or Clang host compiler\n"));
    if (supported)
    {
        String8 root = buster_test_temporary_path(arena, S8("buster-x86-elf-reloc-modifiers"), S8(""));
        OsDirectoryCreateResult created = os_make_directory(root);
        BUSTER_TEST(arguments, created.error.v == 0);
        if (created.error.v == 0)
        {
            Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX};
            String8 source = string_format_z(arena, S8("{S8}/program.c"), root);
            String8 provider_source = string_format_z(arena, S8("{S8}/provider.c"), root);
            String8 direct_object = string_format_z(arena, S8("{S8}/program-direct.o"), root);
            String8 listing = string_format_z(arena, S8("{S8}/program.s"), root);
            String8 buster_object = string_format_z(arena, S8("{S8}/program-buster.o"), root);
            String8 host_object = string_format_z(arena, S8("{S8}/program-host.o"), root);
            String8 provider_object = string_format_z(arena, S8("{S8}/provider.o"), root);
            String8 direct_image = string_format_z(arena, S8("{S8}/program-direct"), root);
            String8 buster_image = string_format_z(arena, S8("{S8}/program-buster"), root);
            String8 host_image = string_format_z(arena, S8("{S8}/program-host"), root);
            String8 program_text = S8("extern int roundtrip_data;\n"
                                      "extern __thread int roundtrip_tls;\n"
                                      "__attribute__((section(\"roundtrip_text\"))) int roundtrip_sum(void) {\n"
                                      "    return roundtrip_data + roundtrip_tls;\n"
                                      "}\n"
                                      "int main(void) { return roundtrip_sum() != 42; }\n");
            String8 provider_text = S8("int roundtrip_data = 39;\n"
                                       "__thread int roundtrip_tls = 3;\n");
            BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(program_text)));
            BUSTER_TEST(arguments, file_write(provider_source, BUSTER_SLICE_TO_BYTE_SLICE(provider_text)));
            // Exercise unwind-only -g0 CFI section bases, including the
            // named executable section retained by roundtrip_sum.
            String8 direct_args[] = {S8("-g0"), S8("-fPIC"), S8("-O2"), S8("-c"), source, S8("-o"), direct_object};
            String8 listing_args[] = {S8("-g0"), S8("-fPIC"), S8("-O2"), S8("-S"), source, S8("-o"), listing};
            String8 buster_args[] = {S8("-c"), listing, S8("-o"), buster_object};
            String8 host_assembly_args[] = {S8("-fPIC"), S8("-c"), S8("-x"), S8("assembler"), listing, S8("-o"), host_object};
            String8 provider_args[] = {S8("-fPIC"), S8("-c"), provider_source, S8("-o"), provider_object};
            bool direct_ready = compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(direct_args)) ==
                                COMPILER_DRIVER_ERROR_NONE;
            bool listing_ready = compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(listing_args)) ==
                                 COMPILER_DRIVER_ERROR_NONE;
            bool buster_ready = listing_ready &&
                                compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_args)) ==
                                    COMPILER_DRIVER_ERROR_NONE;
            bool host_ready = listing_ready &&
                              compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_assembly_args));
            bool provider_ready = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(provider_args));
            BUSTER_TEST(arguments, direct_ready && listing_ready && buster_ready && host_ready && provider_ready);
            if (direct_ready && buster_ready)
            {
                String8 relocation_mismatch = {0};
                bool relocation_objects_match = compiler_driver_elf_assembly_relocation_objects_match(
                    arena, direct_object, buster_object, &relocation_mismatch);
                BUSTER_TEST_RAW(arguments, relocation_objects_match,
                                string_format(arena, S8("direct vs reassembled object mismatch: {S8}"), relocation_mismatch));
                ObjectFile direct = object_read(arena, file_read(arena, direct_object, (FileReadOptions){0}), target);
                ObjectFile buster = object_read(arena, file_read(arena, buster_object, (FileReadOptions){0}), target);
                bool text_base = false;
                bool named_text_base = false;
                bool main_function = false;
                bool sum_function = false;
                for (u32 index = 0; index < direct.symbol_count && direct.error == OBJECT_ERROR_NONE; index += 1)
                {
                    ObjectSymbol symbol = direct.symbols[index];
                    bool section_base = !symbol.global && !symbol.weak && !symbol.value && !symbol.size &&
                                        symbol.kind == OBJECT_SYMBOL_DATA &&
                                        string_equal(symbol.name, compiler_driver_elf_symbol_section_name(direct, symbol));
                    text_base |= section_base && string_equal(symbol.name, S8(".text"));
                    named_text_base |= section_base && string_equal(symbol.name, S8("roundtrip_text"));
                    main_function |= symbol.global && symbol.kind == OBJECT_SYMBOL_FUNCTION && string_equal(symbol.name, S8("main"));
                    sum_function |= symbol.global && symbol.kind == OBJECT_SYMBOL_FUNCTION && string_equal(symbol.name, S8("roundtrip_sum"));
                }
                bool text_cfi = false;
                bool named_text_cfi = false;
                for (u32 index = 0; index < direct.relocation_count && direct.error == OBJECT_ERROR_NONE; index += 1)
                {
                    ObjectRelocation relocation = direct.relocations[index];
                    if (relocation.section < direct.section_count && relocation.symbol < direct.symbol_count &&
                        string_equal(direct.sections[relocation.section].name, S8(".eh_frame")))
                    {
                        String8 symbol = direct.symbols[relocation.symbol].name;
                        text_cfi |= string_equal(symbol, S8(".text"));
                        named_text_cfi |= string_equal(symbol, S8("roundtrip_text"));
                    }
                }
                BUSTER_TEST(arguments, text_base && named_text_base);
                BUSTER_TEST(arguments, main_function && sum_function);
                BUSTER_TEST(arguments, text_cfi && named_text_cfi);
                BUSTER_TEST(arguments, direct.error == OBJECT_ERROR_NONE &&
                                      compiler_driver_elf_object_has_relocation(direct, OBJECT_RELOCATION_X86_64_REX_GOTPCRELX) &&
                                      compiler_driver_elf_object_has_relocation(direct, OBJECT_RELOCATION_X86_64_TLSGD) &&
                                      compiler_driver_elf_object_has_relocation(direct, OBJECT_RELOCATION_X86_64_PLT32));
                BUSTER_TEST(arguments, compiler_driver_elf_object_undefined_symbol_is(buster, S8("roundtrip_data"), OBJECT_SYMBOL_DATA,
                                                                                      OBJECT_SYMBOL_THREAD_LOCAL_NO));
                BUSTER_TEST(arguments, compiler_driver_elf_object_undefined_symbol_is(buster, S8("roundtrip_tls"), OBJECT_SYMBOL_DATA,
                                                                                      OBJECT_SYMBOL_THREAD_LOCAL_YES));
                BUSTER_TEST(arguments, compiler_driver_elf_object_undefined_symbol_is(buster, S8("__tls_get_addr"), OBJECT_SYMBOL_FUNCTION,
                                                                                      OBJECT_SYMBOL_THREAD_LOCAL_NO));
            }
            if (host_ready)
            {
                ObjectFile host = object_read(arena, file_read(arena, host_object, (FileReadOptions){0}), target);
                BUSTER_TEST(arguments, host.error == OBJECT_ERROR_NONE &&
                                      compiler_driver_elf_object_has_got_relocation(host) &&
                                      compiler_driver_elf_object_has_relocation(host, OBJECT_RELOCATION_X86_64_TLSGD) &&
                                      compiler_driver_elf_object_has_relocation(host, OBJECT_RELOCATION_X86_64_PLT32));
            }
            if (direct_ready && host_ready && provider_ready)
            {
                String8 direct_link[] = {S8("-no-pie"), direct_object, provider_object, S8("-o"), direct_image};
                bool linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(direct_link));
                BUSTER_TEST(arguments, linked);
                if (linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, direct_image));
            }
            if (buster_ready && provider_ready)
            {
                String8 buster_link[] = {S8("-no-pie"), buster_object, provider_object, S8("-o"), buster_image};
                bool linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_link));
                BUSTER_TEST(arguments, linked);
                if (linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, buster_image));
            }
            if (host_ready && provider_ready)
            {
                String8 host_link[] = {S8("-no-pie"), host_object, provider_object, S8("-o"), host_image};
                bool linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_link));
                BUSTER_TEST(arguments, linked);
                if (linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, host_image));
            }

            String8 modifier_source = string_format_z(arena, S8("{S8}/integer-modifiers-input.s"), root);
            String8 modifier_direct = string_format_z(arena, S8("{S8}/integer-direct.o"), root);
            String8 modifier_listing = string_format_z(arena, S8("{S8}/integer-modifiers.s"), root);
            String8 modifier_buster = string_format_z(arena, S8("{S8}/integer-buster.o"), root);
            String8 modifier_host = string_format_z(arena, S8("{S8}/integer-host.o"), root);
            // Raw data directives belong to the standalone assembler; the C
            // module-assembly vocabulary emits text only.
            String8 modifier_text = S8(".extern assembly_modifier_tls\n"
                ".type assembly_modifier_tls,@tls_object\n"
                ".data\n"
                ".long assembly_modifier_tls@TLSGD - ., assembly_modifier_tls@TLSLD - .\n"
                ".long assembly_modifier_tls@GOTTPOFF - .\n"
                ".long assembly_modifier_tls@TPOFF\n"
                ".long assembly_modifier_tls@DTPOFF\n"
                ".quad assembly_modifier_tls@DTPOFF\n"
                ".text\n"
                ".globl assembly_modifier_anchor\n"
                ".type assembly_modifier_anchor,@function\n"
                "assembly_modifier_anchor: ret\n");
            BUSTER_TEST(arguments, file_write(modifier_source, BUSTER_SLICE_TO_BYTE_SLICE(modifier_text)));
            String8 modifier_direct_args[] = {S8("-g0"), S8("-fPIC"), S8("-c"), modifier_source, S8("-o"), modifier_direct};
            String8 modifier_listing_args[] = {S8("-g0"), S8("-fPIC"), S8("-S"), modifier_source, S8("-o"), modifier_listing};
            String8 modifier_buster_args[] = {S8("-c"), modifier_listing, S8("-o"), modifier_buster};
            String8 modifier_host_args[] = {S8("-fPIC"), S8("-c"), S8("-x"), S8("assembler"), modifier_listing, S8("-o"), modifier_host};
            bool modifier_direct_ready =
                compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(modifier_direct_args)) ==
                COMPILER_DRIVER_ERROR_NONE;
            bool modifier_listing_ready =
                compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(modifier_listing_args)) ==
                COMPILER_DRIVER_ERROR_NONE;
            bool modifier_buster_ready = modifier_listing_ready &&
                compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(modifier_buster_args)) ==
                    COMPILER_DRIVER_ERROR_NONE;
            bool modifier_host_ready = modifier_listing_ready &&
                compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(modifier_host_args));
            BUSTER_TEST(arguments, modifier_direct_ready && modifier_listing_ready && modifier_buster_ready && modifier_host_ready);
            if (modifier_direct_ready && modifier_buster_ready)
            {
                String8 modifier_mismatch = {0};
                bool modifier_objects_match = compiler_driver_elf_assembly_relocation_objects_match(
                    arena, modifier_direct, modifier_buster, &modifier_mismatch);
                BUSTER_TEST_RAW(arguments, modifier_objects_match,
                                string_format(arena, S8("direct vs reassembled modifier object mismatch: {S8}"), modifier_mismatch));
                ObjectFile direct = object_read(arena, file_read(arena, modifier_direct, (FileReadOptions){0}), target);
                BUSTER_TEST(arguments, direct.error == OBJECT_ERROR_NONE &&
                                      compiler_driver_elf_object_has_relocation(direct, OBJECT_RELOCATION_X86_64_TLSGD) &&
                                      compiler_driver_elf_object_has_relocation(direct, OBJECT_RELOCATION_X86_64_TLSLD) &&
                                      compiler_driver_elf_object_has_relocation(direct, OBJECT_RELOCATION_X86_64_GOTTPOFF) &&
                                      compiler_driver_elf_object_has_relocation(direct, OBJECT_RELOCATION_X86_64_TPOFF32) &&
                                      compiler_driver_elf_object_has_relocation(direct, OBJECT_RELOCATION_X86_64_DTPOFF32) &&
                                      compiler_driver_elf_object_has_relocation(direct, OBJECT_RELOCATION_X86_64_DTPOFF64));
            }
            if (modifier_host_ready)
            {
                ObjectFile host = object_read(arena, file_read(arena, modifier_host, (FileReadOptions){0}), target);
                BUSTER_TEST(arguments, host.error == OBJECT_ERROR_NONE &&
                                      compiler_driver_elf_object_has_relocation(host, OBJECT_RELOCATION_X86_64_TLSGD) &&
                                      compiler_driver_elf_object_has_relocation(host, OBJECT_RELOCATION_X86_64_TLSLD) &&
                                      compiler_driver_elf_object_has_relocation(host, OBJECT_RELOCATION_X86_64_GOTTPOFF) &&
                                      compiler_driver_elf_object_has_relocation(host, OBJECT_RELOCATION_X86_64_TPOFF32) &&
                                      compiler_driver_elf_object_has_relocation(host, OBJECT_RELOCATION_X86_64_DTPOFF32) &&
                                      compiler_driver_elf_object_has_relocation(host, OBJECT_RELOCATION_X86_64_DTPOFF64));
            }
            String8 boundary_source = string_format_z(arena, S8("{S8}/got-boundary.s"), root);
            String8 boundary_object = string_format_z(arena, S8("{S8}/got-boundary.o"), root);
            String8 boundary_text = S8(".text\n"
                                       ".extern boundary_target\n"
                                       ".byte 0x41, 0x8b, 0x05\n"
                                       ".long boundary_target@GOTPCREL - .\n"
                                       ".Lboundary_local:\n"
                                       ".byte 0x90\n"
                                       ".long .Lboundary_local@GOTPCREL - .\n");
            String8 boundary_args[] = {S8("-g0"), S8("-c"), boundary_source, S8("-o"), boundary_object};
            bool boundary_ready = file_write(boundary_source, BUSTER_SLICE_TO_BYTE_SLICE(boundary_text)) &&
                                  compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(boundary_args)) ==
                                      COMPILER_DRIVER_ERROR_NONE;
            BUSTER_TEST(arguments, boundary_ready);
            if (boundary_ready)
            {
                ObjectFile boundary = object_read(arena, file_read(arena, boundary_object, (FileReadOptions){0}), target);
                bool external_generic = false;
                bool local_generic = false;
                for (u32 index = 0; index < boundary.relocation_count && boundary.error == OBJECT_ERROR_NONE; index += 1)
                {
                    ObjectRelocation relocation = boundary.relocations[index];
                    if (relocation.section < boundary.section_count && relocation.symbol < boundary.symbol_count &&
                        relocation.kind == OBJECT_RELOCATION_X86_64_GOTPCREL && relocation.addend == 0 &&
                        string_equal(boundary.sections[relocation.section].name, S8(".text")))
                    {
                        String8 symbol = boundary.symbols[relocation.symbol].name;
                        external_generic = external_generic ||
                                           (relocation.offset == 3 && string_equal(symbol, S8("boundary_target")));
                        local_generic = local_generic ||
                                        (relocation.offset == 8 && string_equal(symbol, S8(".Lboundary_local")));
                    }
                }
                BUSTER_TEST(arguments, boundary.error == OBJECT_ERROR_NONE && boundary.relocation_count == 2 &&
                                      external_generic && local_generic);
            }

            String8 prefixed_got_sources[] = {
                S8(".extern got_word_target\n"
                   ".text\n"
                   "mov ax, word ptr [rip + got_word_target@GOTPCREL]\n"),
                S8(".extern got_fs_target\n"
                   ".text\n"
                   "mov eax, dword ptr fs:[rip + got_fs_target@GOTPCREL]\n"),
            };
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(prefixed_got_sources); index += 1)
            {
                AssemblyUnitResult prefixed_got = assembly_unit_encode(
                    arena, prefixed_got_sources[index],
                    (AssemblyEncodeOptions){.target = target, .syntax = ASSEMBLY_SYNTAX_INTEL});
                bool prefix_refused = prefixed_got.diagnostic_count &&
                                      prefixed_got.diagnostics[0].kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE &&
                                      string_equal(prefixed_got.diagnostics[0].message,
                                                   S8("unsupported symbol modifier '@GOTPCREL' for this instruction field"));
                u32 prefix_diagnostic_kind = prefixed_got.diagnostic_count ? (u32)prefixed_got.diagnostics[0].kind : UINT32_MAX;
                String8 prefix_diagnostic_message = prefixed_got.diagnostic_count ? prefixed_got.diagnostics[0].message : S8("<none>");
                BUSTER_TEST_RAW(arguments, prefix_refused,
                                string_format(arena, S8("GOTPCREL prefix case {u32}: diagnostics={u32} kind={u32} message={S8}"),
                                              index, prefixed_got.diagnostic_count, prefix_diagnostic_kind,
                                              prefix_diagnostic_message));
            }

            String8 raw_got_source = string_format_z(arena, S8("{S8}/generic-got.s"), root);
            String8 raw_got_object = string_format_z(arena, S8("{S8}/generic-got.o"), root);
            String8 raw_got_main = string_format_z(arena, S8("{S8}/generic-got-main.c"), root);
            String8 raw_got_provider = string_format_z(arena, S8("{S8}/generic-got-provider.c"), root);
            String8 raw_got_image = string_format_z(arena, S8("{S8}/generic-got-image"), root);
            String8 raw_got_assembly = S8(".extern raw_got_target\n"
                                          ".data\n"
                                          ".byte 0x41, 0x8b, 0x05\n"
                                          ".globl raw_got_slot\n"
                                          ".type raw_got_slot,@object\n"
                                          "raw_got_slot:\n"
                                          ".long raw_got_target@GOTPCREL - .\n"
                                          ".size raw_got_slot,4\n");
            String8 raw_got_main_text = S8("extern int raw_got_slot;\n"
                                           "int main(void) {\n"
                                           "    long slot_address = (long)&raw_got_slot + (long)raw_got_slot;\n"
                                           "    long target_address = *(long *)slot_address;\n"
                                           "    return *(int *)target_address != 42;\n"
                                           "}\n");
            String8 raw_got_provider_text = S8("int raw_got_target = 42;\n");
            String8 raw_got_args[] = {S8("-g0"), S8("-c"), raw_got_source, S8("-o"), raw_got_object};
            bool raw_got_ready = file_write(raw_got_source, BUSTER_SLICE_TO_BYTE_SLICE(raw_got_assembly)) &&
                                 compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(raw_got_args)) ==
                                     COMPILER_DRIVER_ERROR_NONE;
            BUSTER_TEST(arguments, raw_got_ready);
            if (raw_got_ready)
            {
                ObjectFile raw_got = object_read(arena, file_read(arena, raw_got_object, (FileReadOptions){0}), target);
                bool generic_data_relocation = false;
                for (u32 index = 0; index < raw_got.relocation_count && raw_got.error == OBJECT_ERROR_NONE; index += 1)
                {
                    ObjectRelocation relocation = raw_got.relocations[index];
                    if (relocation.section < raw_got.section_count && relocation.symbol < raw_got.symbol_count)
                    {
                        generic_data_relocation = generic_data_relocation ||
                            (relocation.kind == OBJECT_RELOCATION_X86_64_GOTPCREL && relocation.addend == 0 &&
                             string_equal(raw_got.sections[relocation.section].name, S8(".data")) &&
                             string_equal(raw_got.symbols[relocation.symbol].name, S8("raw_got_target")));
                    }
                }
                BUSTER_TEST(arguments, raw_got.error == OBJECT_ERROR_NONE && raw_got.relocation_count == 1 &&
                                      generic_data_relocation);
                bool sources_ready = file_write(raw_got_main, BUSTER_SLICE_TO_BYTE_SLICE(raw_got_main_text)) &&
                                     file_write(raw_got_provider, BUSTER_SLICE_TO_BYTE_SLICE(raw_got_provider_text));
                String8 raw_got_link_args[] = {S8("-no-pie"), raw_got_main, raw_got_provider, raw_got_object, S8("-o"), raw_got_image};
                bool linked = sources_ready &&
                              compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(raw_got_link_args));
                BUSTER_TEST(arguments, linked);
                if (linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, raw_got_image));
            }

            String8 replay_source = string_format_z(arena, S8("{S8}/modifier-replay.s"), root);
            String8 replay_object = string_format_z(arena, S8("{S8}/modifier-replay.o"), root);
            String8 replay_text = S8(".intel_syntax noprefix\n"
                                     ".extern replay_got\n"
                                     ".extern replay_tls\n"
                                     ".type replay_tls,@tls_object\n"
                                     ".text\n"
                                     ".globl modifier_replay\n"
                                     ".type modifier_replay,@function\n"
                                     "modifier_replay:\n"
                                     "jmp .Lmodifier_replay_done\n"
                                     "mov rax, qword ptr [rip + replay_got@GOTPCREL]\n"
                                     "push replay_tls@TPOFF\n"
                                     "mov eax, replay_tls@TPOFF\n"
                                     "push replay_tls@DTPOFF\n"
                                     "mov eax, replay_tls@DTPOFF\n"
                                     ".Lmodifier_replay_done:\n"
                                     "ret\n");
            String8 replay_args[] = {S8("-g0"), S8("-c"), replay_source, S8("-o"), replay_object};
            bool replay_ready = file_write(replay_source, BUSTER_SLICE_TO_BYTE_SLICE(replay_text)) &&
                                compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(replay_args)) ==
                                    COMPILER_DRIVER_ERROR_NONE;
            BUSTER_TEST(arguments, replay_ready);
            if (replay_ready)
            {
                ObjectFile replay = object_read(arena, file_read(arena, replay_object, (FileReadOptions){0}), target);
                bool short_jump = false;
                for (u32 index = 0; index < replay.section_count && replay.error == OBJECT_ERROR_NONE; index += 1)
                {
                    if (string_equal(replay.sections[index].name, S8(".text")))
                    {
                        ByteSlice text = replay.sections[index].data;
                        short_jump = text.length && text.pointer[0] == 0xeb;
                    }
                }
                u32 got_count = 0;
                u32 tpoff_count = 0;
                u32 dtpoff_count = 0;
                bool got_addend = false;
                for (u32 index = 0; index < replay.relocation_count && replay.error == OBJECT_ERROR_NONE; index += 1)
                {
                    ObjectRelocation relocation = replay.relocations[index];
                    if (relocation.kind == OBJECT_RELOCATION_X86_64_REX_GOTPCRELX)
                    {
                        got_count += 1;
                        got_addend = got_addend || relocation.addend == -4;
                    }
                    tpoff_count += relocation.kind == OBJECT_RELOCATION_X86_64_TPOFF32;
                    dtpoff_count += relocation.kind == OBJECT_RELOCATION_X86_64_DTPOFF32;
                }
                BUSTER_TEST(arguments, replay.error == OBJECT_ERROR_NONE && short_jump && got_count == 1 && got_addend &&
                                      tpoff_count == 2 && dtpoff_count == 2);
                BUSTER_TEST(arguments, compiler_driver_elf_object_undefined_symbol_is(replay, S8("replay_tls"), OBJECT_SYMBOL_DATA,
                                                                                       OBJECT_SYMBOL_THREAD_LOCAL_YES));
            }

            String8 invalid_source = string_format_z(arena, S8("{S8}/modifier-multisymbol.s"), root);
            String8 invalid_output = string_format_z(arena, S8("{S8}/modifier-invalid.o"), root);
            BUSTER_TEST(arguments, compiler_driver_elf_modifier_rejection(
                invalid_source, invalid_output,
                S8(".text\n.extern modifier_target\n.extern unrelated_target\n.long modifier_target@TLSGD - unrelated_target\n"),
                S8("unsupported symbol modifier '@TLSGD' in integer expression")));
            BUSTER_TEST(arguments, compiler_driver_elf_modifier_rejection(
                invalid_source, invalid_output,
                S8(".text\n.extern modifier_target\n.long modifier_target@TLSGD@DTPOFF\n"),
                S8("unsupported symbol modifier '@DTPOFF' in integer directive")));
            BUSTER_TEST(arguments, compiler_driver_elf_modifier_rejection(
                invalid_source, invalid_output,
                S8(".text\n.extern modifier_target\n.quad modifier_target@TLSGD - .\n"),
                S8("unsupported symbol modifier '@TLSGD' for this integer field")));
            BUSTER_TEST(arguments, os_directory_delete(root));
        }
    }
    return result;
}
#endif

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
                "int main(void) { return scale(7) != 21; }\n"), {0}},
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
            {S8("gnu-property-control"), S8("__asm__(\".pushsection .note.gnu.property,\\\"a\\\",@note\\n.balign 8\\n.long 4,16,5\\n.asciz \\\"GNU\\\"\\n.long 0xc0000002,4,0\\n.long 0\\n.popsection\");\n"
                "int main(void) { return 0; }\n"), {0}},
#else
            {S8("init"), S8("static int ran; void semantic_hook(void) { ran = 1; }\n"
                "__asm__(\".pushsection .init,\\\"ax\\\",%progbits\\nbl semantic_hook\\n.popsection\");\n"
                "int main(void) { return ran != 1; }\n"), S8("unsupported ELF section .init (type 1)")},
            {S8("fini"), S8("extern void _Exit(int); void semantic_hook(void) { _Exit(0); }\n"
                "__asm__(\".pushsection .fini,\\\"ax\\\",%progbits\\nbl semantic_hook\\n.popsection\");\n"
                "int main(void) { return 1; }\n"), S8("unsupported ELF section .fini (type 1)")},
            {S8("allocated-note"), S8("__asm__(\".pushsection .note.vendor,\\\"a\\\",%note\\n.balign 4\\n.long 4,4,1\\n.asciz \\\"VND\\\"\\n.long 0\\n.popsection\");\n"
                "int main(void) { return 0; }\n"), S8("unsupported ELF section .note.vendor (type 7)")},
            {S8("gnu-property-control"), S8("__asm__(\".pushsection .note.gnu.property,\\\"a\\\",%note\\n.balign 8\\n.long 4,16,5\\n.asciz \\\"GNU\\\"\\n.long 0xc0000000,4,0\\n.long 0\\n.popsection\");\n"
                "int main(void) { return 0; }\n"), {0}},
#endif
            // Put the absolute address in data: AArch64 instruction fixups
            // cannot encode this small SHN_ABS value in every host compiler.
            {S8("absolute"), S8("extern const unsigned long semantic_absolute_address;\n"
                "__asm__(\".globl semantic_absolute\\n.set semantic_absolute,0x1234\\n"
                ".pushsection .data\\n.balign 8\\n.globl semantic_absolute_address\\nsemantic_absolute_address:\\n.quad semantic_absolute\\n.popsection\");\n"
                "int main(void) { return semantic_absolute_address != 0x1234; }\n"),
                S8("unsupported ELF symbol semantic_absolute (section index 65521): SHN_ABS")},
            {S8("weak-absolute"), S8("extern const unsigned long semantic_absolute_address;\n"
                "__asm__(\".weak semantic_absolute\\n.set semantic_absolute,0x1234\\n"
                ".pushsection .data\\n.balign 8\\n.globl semantic_absolute_address\\nsemantic_absolute_address:\\n.quad semantic_absolute\\n.popsection\");\n"
                "int main(void) { return semantic_absolute_address != 0x1234; }\n"),
                S8("unsupported ELF symbol semantic_absolute (section index 65521): SHN_ABS")},
            {S8("preinit-control"), S8("static volatile int ran; static void preinit(void) { ran = 1; }\n"
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
            String8 compile[] = {S8("-O2"), S8("-fno-pie"), S8("-c"), source, S8("-o"), object,
#if BUSTER_CPU_ARCH_X86_64
                S8("-fcf-protection=none"), // The GNU property control emits its own single record.
#endif
            };
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
                // An IFUNC-only member is selected from an archive and still
                // resolves through its resolver (issue 1243).
                String8 member_source = string_format_z(arena, S8("{S8}/ifunc-member.c"), root);
                String8 member_object = string_format_z(arena, S8("{S8}/ifunc-member.o"), root);
                String8 member_compile[] = {S8("-O2"), S8("-fno-pie"), S8("-c"), member_source, S8("-o"), member_object};
                bool member_produced = index == 0 &&
                    file_write(member_source, BUSTER_SLICE_TO_BYTE_SLICE(S8("static int implementation(int x) { return x * 3; }\n"
                        "static int (*resolve_scale(void))(int) { return implementation; }\n"
                        "int scale(int) __attribute__((ifunc(\"resolve_scale\")));\n"))) &&
                    compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(member_compile));
                if (index == 0) BUSTER_TEST(arguments, member_produced);
                if (member_produced)
                {
                    ByteSlice member = file_read(arena, member_object, (FileReadOptions){0});
                    ByteSlice archive_bytes = compiler_driver_elf_semantic_archive(arena, member);
                    ObjectArchive eager = object_archive_read(arena, archive_bytes, target_native);
                    BUSTER_TEST(arguments, eager.error == OBJECT_ERROR_NONE && eager.object_count == 1);
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
                        BUSTER_TEST(arguments, archived.error == COMPILER_DRIVER_ERROR_NONE);
                        if (archived.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, image));
                    }
                }
            }
        }
        BUSTER_TEST(arguments, os_directory_delete(root));
    }
    return result;
}

// Issue 1243: `ld` and `lld` run `.preinit_array` before every constructor of
// every priority, `constructor(0)` and a dependency's included, whichever
// input names it first; only the section of exactly that name is that array;
// and a shared object cannot hold one. The two objects are host-compiled and
// linked by this linker in both orders and by the host linker as the oracle.
// The entries append digits to a trace, which main compares with the order
// `ld` runs them in: 9 (preinit), 1 (`constructor(0)`), 3 (101), 5
// (`.init_array.00150`), then the unprioritized 2 and 4 (GCC emits
// `constructor(65535)` unprioritized). An assembly input, which the driver
// assembles in memory, carries the same preinit entry and must be ordered and
// refused in a shared output alike. `-shared` exists only on Linux x86-64, so
// the refusal is checked everywhere but its message only there.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_elf_preinit_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 compiler = S8(BUSTER_HOST_C_COMPILER_ID);
    bool supported = string_equal(compiler, S8("GNU")) || string_equal(compiler, S8("Clang")) || string_equal(compiler, S8("AppleClang"));
    if (supported)
    {
        String8 root = buster_test_temporary_path(arena, S8("buster-elf-preinit"), S8(""));
        OsDirectoryCreateResult created = os_make_directory(root);
        BUSTER_TEST(arguments, created.error.v == 0);
        String8 dependency_source = string_format_z(arena, S8("{S8}/dependency.c"), root);
        String8 dependency_object = string_format_z(arena, S8("{S8}/dependency.o"), root);
        String8 main_source = string_format_z(arena, S8("{S8}/main.c"), root);
        String8 main_object = string_format_z(arena, S8("{S8}/main.o"), root);
        String8 pic_object = string_format_z(arena, S8("{S8}/main-pic.o"), root);
        String8 pic_source = string_format_z(arena, S8("{S8}/main-pic.c"), root);
        String8 data_source = string_format_z(arena, S8("{S8}/data.c"), root);
        String8 data_object = string_format_z(arena, S8("{S8}/data.o"), root);
        BUSTER_TEST(arguments, file_write(dependency_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "extern volatile int trace;\n"
            "static void middle(void) { trace = trace * 10 + 5; }\n"
            "__attribute__((constructor(0))) static void zero(void) { trace = trace * 10 + 1; }\n"
            "__attribute__((constructor)) static void plain(void) { trace = trace * 10 + 2; }\n"
            "__attribute__((constructor(101))) static void low(void) { trace = trace * 10 + 3; }\n"
            "__attribute__((constructor(65535))) static void high(void) { trace = trace * 10 + 4; }\n"
            "__attribute__((section(\".init_array.00150\"), used)) static void (*named)(void) = middle;\n"))));
        BUSTER_TEST(arguments, file_write(main_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "volatile int trace;\n"
            "static void early(void) { trace = trace * 10 + 9; }\n"
            "__attribute__((section(\".preinit_array\"), used)) static void (*entry)(void) = early;\n"
            "int main(void) { return trace != 913524; }\n"))));
        // The shared-output input touches no global data, so the refusal is the
        // only reason a link of it could fail on any architecture.
        BUSTER_TEST(arguments, file_write(pic_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "static void early(void) {}\n"
            "__attribute__((section(\".preinit_array\"), used)) static void (*entry)(void) = early;\n"))));
        // Neither an `.preinit_array.5` nor a type-16 section of another name
        // is run by the host linkers, so neither may run here.
        BUSTER_TEST(arguments, file_write(data_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "volatile int trace;\n"
            "static void hook(void) { trace = 7; }\n"
            "__attribute__((section(\".preinit_array.5\"), used)) static void (*suffixed)(void) = hook;\n"
            "__asm__(\".section .mypre,\\\"aw\\\",@preinit_array\\n.quad hook\\n.previous\");\n"
            "int main(void) { return trace != 0; }\n"))));
        String8 dependency_compile[] = {S8("-w"), S8("-O2"), S8("-fno-pie"), S8("-c"), dependency_source, S8("-o"), dependency_object};
        String8 main_compile[] = {S8("-w"), S8("-O2"), S8("-fno-pie"), S8("-c"), main_source, S8("-o"), main_object};
        String8 pic_compile[] = {S8("-w"), S8("-O2"), S8("-fPIC"), S8("-c"), pic_source, S8("-o"), pic_object};
        String8 data_compile[] = {S8("-w"), S8("-O2"), S8("-fno-pie"), S8("-c"), data_source, S8("-o"), data_object};
        bool produced = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(dependency_compile)) &&
                        compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(main_compile)) &&
                        compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(pic_compile)) &&
                        compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(data_compile));
        BUSTER_TEST(arguments, produced);
        if (produced)
        {
            struct
            {
                String8 name;
                String8 first;
                String8 second;
            } orders[] = {
                {S8("dependency-first"), dependency_object, main_object},
                {S8("main-first"), main_object, dependency_object},
            };
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(orders); index += 1)
            {
                String8 oracle = string_format_z(arena, S8("{S8}/{S8}-oracle"), root, orders[index].name);
                String8 output = string_format_z(arena, S8("{S8}/{S8}-buster"), root, orders[index].name);
                String8 host_link[] = {S8("-no-pie"), orders[index].first, orders[index].second, S8("-o"), oracle};
                bool linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_link));
                BUSTER_TEST(arguments, linked);
                if (linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, oracle));
                String8 command[] = {S8("-no-pie"), orders[index].first, orders[index].second, S8("-o"), output};
                CompilerDriverResult compiled = compiler_driver_execute_invocation(
                    arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
                if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, output));
            }
            // The same entry from an assembly input assembled in this
            // invocation, linked ahead of and behind the objects.
            String8 asm_source = string_format_z(arena, S8("{S8}/hook.s"), root);
            String8 hook_source = string_format_z(arena, S8("{S8}/hook.c"), root);
            String8 hook_object = string_format_z(arena, S8("{S8}/hook.o"), root);
            BUSTER_TEST(arguments, file_write(asm_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
                "\t.section .preinit_array,\"aw\",@preinit_array\n\t.balign 8\n\t.quad early_hook\n"))));
            BUSTER_TEST(arguments, file_write(hook_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
                "volatile int trace;\n"
                "void early_hook(void) { trace = trace * 10 + 9; }\n"
                "int main(void) { return trace != 913524; }\n"))));
            String8 hook_compile[] = {S8("-w"), S8("-O2"), S8("-fno-pie"), S8("-c"), hook_source, S8("-o"), hook_object};
            bool hook_produced = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(hook_compile));
            BUSTER_TEST(arguments, hook_produced);
            for (u32 index = 0; hook_produced && index < 2; index += 1)
            {
                String8 oracle = string_format_z(arena, S8("{S8}/asm-{u32}-oracle"), root, index);
                String8 output = string_format_z(arena, S8("{S8}/asm-{u32}-buster"), root, index);
                String8 first = index ? dependency_object : asm_source;
                String8 second = index ? hook_object : dependency_object;
                String8 third = index ? asm_source : hook_object;
                String8 host_link[] = {S8("-no-pie"), first, second, third, S8("-o"), oracle};
                bool linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_link));
                BUSTER_TEST(arguments, linked);
                if (linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, oracle));
                String8 command[] = {S8("-no-pie"), first, second, third, S8("-o"), output};
                CompilerDriverResult compiled = compiler_driver_execute_invocation(
                    arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
                if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, output));
            }
            // A self-contained copy, so nothing but the preinit entry can fail it.
            // `-shared` is a Linux x86-64 image kind and the driver refuses it as an
            // unsupported option elsewhere, so the message is checked only there.
            String8 shared_asm_source = string_format_z(arena, S8("{S8}/hook-shared.s"), root);
#if BUSTER_CPU_ARCH_X86_64
            String8 shared_asm_text = S8("\t.text\nshared_hook:\n\tretq\n\t.section .preinit_array,\"aw\",@preinit_array\n\t.balign 8\n\t.quad shared_hook\n");
#else
            String8 shared_asm_text = S8("\t.text\nshared_hook:\n\tret\n\t.section .preinit_array,\"aw\",@preinit_array\n\t.balign 8\n\t.quad shared_hook\n");
#endif
            BUSTER_TEST(arguments, file_write(shared_asm_source, BUSTER_SLICE_TO_BYTE_SLICE(shared_asm_text)));
            String8 asm_shared_output = string_format_z(arena, S8("{S8}/asm-preinit.so"), root);
            String8 asm_shared_command[] = {S8("-shared"), shared_asm_source, S8("-o"), asm_shared_output};
            CompilerDriverResult asm_shared = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(asm_shared_command)));
            BUSTER_TEST(arguments, asm_shared.error != COMPILER_DRIVER_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64
            BUSTER_TEST(arguments, string_first_sequence(asm_shared.diagnostic, S8(".preinit_array")) < asm_shared.diagnostic.length);
#endif
            BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(asm_shared_output));
            // The host's own data sections stay data.
            String8 data_oracle = string_format_z(arena, S8("{S8}/data-oracle"), root);
            String8 data_output = string_format_z(arena, S8("{S8}/data-buster"), root);
            String8 data_host_link[] = {S8("-no-pie"), data_object, S8("-o"), data_oracle};
            bool data_linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(data_host_link));
            BUSTER_TEST(arguments, data_linked);
            if (data_linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, data_oracle));
            String8 data_command[] = {S8("-no-pie"), data_object, S8("-o"), data_output};
            CompilerDriverResult data_compiled = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(data_command)));
            BUSTER_TEST(arguments, data_compiled.error == COMPILER_DRIVER_ERROR_NONE);
            if (data_compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, data_output));
            // A shared object cannot hold a preinit array: GNU ld refuses it,
            // and so does this linker, naming the section, with no output.
            String8 shared_oracle = string_format_z(arena, S8("{S8}/preinit-oracle.so"), root);
            String8 shared_output = string_format_z(arena, S8("{S8}/preinit-buster.so"), root);
            String8 shared_host[] = {S8("-shared"), pic_object, S8("-o"), shared_oracle};
            BUSTER_TEST(arguments, !compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(shared_host)));
            String8 shared_command[] = {S8("-shared"), pic_object, S8("-o"), shared_output};
            CompilerDriverResult shared = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(shared_command)));
            BUSTER_TEST(arguments, shared.error != COMPILER_DRIVER_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64
            BUSTER_TEST(arguments, string_first_sequence(shared.diagnostic, S8(".preinit_array")) < shared.diagnostic.length);
#endif
            BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(shared_output));
        }
        // A dynamic executable's preinit entries are the loader's: ld.so runs
        // DT_PREINIT_ARRAY, with argc, argv and envp, before the constructors
        // of the shared libraries it loads, and those before the executable's
        // own. The trace is 9 (preinit), 2 (the library), 3 (the program);
        // an entry stub call would give 293. `-pie` exists only on x86-64.
        String8 library_source = string_format_z(arena, S8("{S8}/preinitdep.c"), root);
        String8 library = string_format_z(arena, S8("{S8}/libpreinitdep.so"), root);
        String8 dynamic_source = string_format_z(arena, S8("{S8}/dynamic.c"), root);
        String8 dynamic_object = string_format_z(arena, S8("{S8}/dynamic.o"), root);
        BUSTER_TEST(arguments, file_write(library_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "extern volatile int trace;\n"
            "__attribute__((constructor)) static void library_constructor(void) { trace = trace * 10 + 2; }\n"
            "int preinit_dependency_touch(void) { return 0; }\n"))));
        BUSTER_TEST(arguments, file_write(dynamic_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "volatile int trace;\n"
            "static void early(int argc, char **argv, char **envp) { trace = trace * 10 + (argc == 1 && argv[0] && envp ? 9 : 8); }\n"
            "__attribute__((section(\".preinit_array\"), used)) static void (*entry)(int, char **, char **) = early;\n"
            "__attribute__((constructor)) static void own(void) { trace = trace * 10 + 3; }\n"
            "int preinit_dependency_touch(void);\n"
            "int main(void) { return preinit_dependency_touch() + (trace != 923); }\n"))));
        String8 library_compile[] = {S8("-w"), S8("-O2"), S8("-fPIC"), S8("-shared"), library_source, S8("-o"), library};
        String8 dynamic_compile[] = {S8("-w"), S8("-O2"), S8("-fPIE"), S8("-c"), dynamic_source, S8("-o"), dynamic_object};
        bool dynamic_produced = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(library_compile)) &&
                                compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(dynamic_compile));
        BUSTER_TEST(arguments, dynamic_produced);
        if (dynamic_produced)
        {
            String8 library_directory = string_format_z(arena, S8("-L{S8}"), root);
            String8 dynamic_oracle = string_format_z(arena, S8("{S8}/dynamic-oracle"), root);
            String8 dynamic_host_link[] = {S8("-no-pie"), dynamic_object, library_directory, S8("-lpreinitdep"), S8("-o"), dynamic_oracle};
            bool linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(dynamic_host_link));
            BUSTER_TEST(arguments, linked);
            if (linked) BUSTER_TEST(arguments, compiler_driver_tls_export_run(arguments, dynamic_oracle, root));
#if BUSTER_CPU_ARCH_X86_64
            String8 kinds[] = {S8("-no-pie"), S8("-pie")};
#else
            String8 kinds[] = {S8("-no-pie")};
#endif
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(kinds); index += 1)
            {
                String8 output = string_format_z(arena, S8("{S8}/dynamic{S8}-buster"), root, kinds[index]);
                String8 command[] = {kinds[index], dynamic_object, library_directory, S8("-lpreinitdep"), S8("-o"), output};
                CompilerDriverResult compiled = compiler_driver_execute_invocation(
                    arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
                if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_tls_export_run(arguments, output, root));
            }
        }
        BUSTER_TEST(arguments, os_directory_delete(root));
    }
    return result;
}

// Issue 1243: a GNU IFUNC from a host-compiled object resolves through its
// resolver, exactly once, and every reference -- calls from either object, a
// local function pointer, a pointer in initialized data and one taken inside
// the defining object -- sees one address. The resolver returns a different
// function on any later call, so a second call or a direct call of the
// resolver changes the result. Both link orders are linked by this linker, as
// a static image (no libc reference) and a dynamic one, `-pie` on x86-64, and
// against the host linker as the oracle. On x86-64 the defining object is
// also linked `-shared` by this linker and loaded by a host-linked program.
// On AArch64 a resolver records its arguments, and the program checks them
// against its own auxiliary vector: glibc's AT_HWCAP | _IFUNC_ARG_HWCAP and
// __ifunc_arg_t, from the static and dynamic runners as from ld.so.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_elf_ifunc_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 compiler = S8(BUSTER_HOST_C_COMPILER_ID);
    bool supported = string_equal(compiler, S8("GNU")) || string_equal(compiler, S8("Clang"));
    if (supported)
    {
        String8 root = buster_test_temporary_path(arena, S8("buster-elf-ifunc"), S8(""));
        OsDirectoryCreateResult created = os_make_directory(root);
        BUSTER_TEST(arguments, created.error.v == 0);
        String8 library_source = string_format_z(arena, S8("{S8}/scale.c"), root);
        String8 library_object = string_format_z(arena, S8("{S8}/scale.o"), root);
        String8 shared_object = string_format_z(arena, S8("{S8}/scale-pic.o"), root);
        BUSTER_TEST(arguments, file_write(library_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "static int twice(int x) { return x * 2; }\n"
            "static int thrice(int x) { return x * 3; }\n"
            "static int resolutions;\n"
            "static int (*resolve_scale(void))(int) { resolutions += 1; return resolutions == 1 ? thrice : twice; }\n"
            "int scale(int) __attribute__((ifunc(\"resolve_scale\")));\n"
            "int (*library_pointer(void))(int) { return scale; }\n"
            "int library_call(int x) { return scale(x); }\n"))));
        String8 mains[] = {S8("static"), S8("dynamic")};
        // The dynamic program differs only in the libc call that makes the image dynamic.
        String8 main_programs[] = {
            S8("int scale(int);\n"
               "int (*library_pointer(void))(int);\n"
               "int library_call(int);\n"
               "int (*volatile data_pointer)(int) = scale;\n"
               "int main(void)\n"
               "{\n"
               "    int (*local)(int) = scale;\n"
               "    int ok = scale(7) == 21 && library_call(7) == 21 && local(7) == 21 && data_pointer(7) == 21 &&\n"
               "             local == library_pointer() && local == data_pointer;\n"
               "    return !ok;\n"
               "}\n"),
            S8("int puts(const char *);\n"
               "int scale(int);\n"
               "int (*library_pointer(void))(int);\n"
               "int library_call(int);\n"
               "int (*volatile data_pointer)(int) = scale;\n"
               "int main(void)\n"
               "{\n"
               "    int (*local)(int) = scale;\n"
               "    int ok = scale(7) == 21 && library_call(7) == 21 && local(7) == 21 && data_pointer(7) == 21 &&\n"
               "             local == library_pointer() && local == data_pointer;\n"
               "    puts(ok ? \"ifunc ok\" : \"ifunc wrong\");\n"
               "    return !ok;\n"
               "}\n"),
        };
        String8 main_sources[2];
        String8 main_objects[2];
        for (u32 index = 0; index < 2; index += 1)
        {
            main_sources[index] = string_format_z(arena, S8("{S8}/main-{S8}.c"), root, mains[index]);
            main_objects[index] = string_format_z(arena, S8("{S8}/main-{S8}.o"), root, mains[index]);
            BUSTER_TEST(arguments, file_write(main_sources[index], BUSTER_SLICE_TO_BYTE_SLICE(main_programs[index])));
        }
        String8 library_compile[] = {S8("-w"), S8("-O2"), S8("-fPIE"), S8("-c"), library_source, S8("-o"), library_object};
        String8 shared_compile[] = {S8("-w"), S8("-O2"), S8("-fPIC"), S8("-c"), library_source, S8("-o"), shared_object};
        bool produced = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(library_compile)) &&
                        compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(shared_compile));
        for (u32 index = 0; produced && index < 2; index += 1)
        {
            String8 main_compile[] = {S8("-w"), S8("-O2"), S8("-fPIE"), S8("-c"), main_sources[index], S8("-o"), main_objects[index]};
            produced = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(main_compile));
        }
        BUSTER_TEST(arguments, produced);
#if BUSTER_CPU_ARCH_X86_64
        String8 kinds[] = {S8("-no-pie"), S8("-pie")};
#else
        String8 kinds[] = {S8("-no-pie")};
#endif
        for (u32 index = 0; produced && index < 2; index += 1)
        {
            String8 oracle = string_format_z(arena, S8("{S8}/{S8}-oracle"), root, mains[index]);
            String8 host_link[] = {S8("-no-pie"), main_objects[index], library_object, S8("-o"), oracle};
            bool linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_link));
            BUSTER_TEST(arguments, linked);
            if (linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, oracle));
            for (u32 kind = 0; kind < BUSTER_ARRAY_LENGTH(kinds); kind += 1)
            {
                for (u32 order = 0; order < 2; order += 1)
                {
                    String8 output = string_format_z(arena, S8("{S8}/{S8}{S8}-{u32}-buster"), root, mains[index], kinds[kind], order);
                    String8 first = order ? library_object : main_objects[index];
                    String8 second = order ? main_objects[index] : library_object;
                    String8 command[] = {kinds[kind], first, second, S8("-o"), output};
                    CompilerDriverResult compiled = compiler_driver_execute_invocation(
                        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                    BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
                    if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, output));
                }
            }
        }
        // `ide cc` compiles the same C itself: its frontend lowers the
        // attribute to an indirect IrSymbolAlias, the object carries
        // STT_GNU_IFUNC, and its image must behave as the host-built oracle
        // above does, at both optimization levels. A static ifunc is a local
        // indirect symbol, which the host oracle resolves the same way.
        String8 local_source = string_format_z(arena, S8("{S8}/local.c"), root);
        String8 local_object = string_format_z(arena, S8("{S8}/local.o"), root);
        String8 local_oracle = string_format_z(arena, S8("{S8}/local-oracle"), root);
        BUSTER_TEST(arguments, file_write(local_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "static int resolutions;\n"
            "static int increment(int x) { return x + 1; }\n"
            "static int (*resolve_bump(void))(int) { resolutions += 1; return increment; }\n"
            "static int bump(int) __attribute__((ifunc(\"resolve_bump\")));\n"
            "int (*volatile data_bump)(int) = bump;\n"
            "int main(void)\n"
            "{\n"
            "    int (*local)(int) = bump;\n"
            "    return !(bump(1) == 2 && local(2) == 3 && data_bump(3) == 4 && local == data_bump && resolutions == 1);\n"
            "}\n"))));
        String8 local_compile[] = {S8("-w"), S8("-O2"), S8("-fno-pie"), S8("-c"), local_source, S8("-o"), local_object};
        String8 local_link[] = {S8("-no-pie"), local_object, S8("-o"), local_oracle};
        bool local_linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(local_compile)) &&
                            compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(local_link));
        BUSTER_TEST(arguments, local_linked);
        if (local_linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, local_oracle));
        String8 levels[] = {S8("-O0"), S8("-O2")};
        for (u32 index = 0; produced && index < 3; index += 1)
        {
            for (u32 kind = 0; kind < BUSTER_ARRAY_LENGTH(kinds); kind += 1)
            {
                for (u32 level = 0; level < BUSTER_ARRAY_LENGTH(levels); level += 1)
                {
                    String8 name = index < 2 ? mains[index] : S8("local");
                    String8 output = string_format_z(arena, S8("{S8}/{S8}{S8}{S8}-cc"), root, name, kinds[kind], levels[level]);
                    // The local program is one translation unit.
                    String8 command[6];
                    u64 count = 0;
                    command[count++] = kinds[kind];
                    command[count++] = levels[level];
                    command[count++] = index < 2 ? main_sources[index] : local_source;
                    if (index < 2) command[count++] = library_source;
                    command[count++] = S8("-o");
                    command[count++] = output;
                    CompilerDriverResult compiled = compiler_driver_execute_invocation(
                        arena, compiler_driver_parse_arguments(arena, (SliceString8){.pointer = command, .length = count}));
                    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE, compiled.diagnostic);
                    if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, output));
                }
            }
        }
#if BUSTER_CPU_ARCH_AARCH64
        if (produced)
        {
            // AT_HWCAP is never 0 on AArch64 Linux (FP and ASIMD at least), so
            // the old hwcap-0 runner fails this. A missing AT_HWCAP2 is 0 on
            // both sides; glibc 2.40 grows _size past 24 with _hwcap3/_hwcap4.
            String8 hwcap_source = string_format_z(arena, S8("{S8}/hwcap.c"), root);
            BUSTER_TEST(arguments, file_write(hwcap_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
                "typedef struct { unsigned long size, hwcap, hwcap2; } Argument;\n"
                "static unsigned long seen_hwcap, seen_size, seen_arg_hwcap, seen_arg_hwcap2;\n"
                "static int seen_arg, resolutions;\n"
                "static int one(void) { return 1; }\n"
                "static int (*resolve_probe(unsigned long hwcap, const Argument *arg))(void)\n"
                "{\n"
                "    resolutions += 1;\n"
                "    seen_hwcap = hwcap;\n"
                "    seen_arg = arg != 0;\n"
                "    if (arg) { seen_size = arg->size; seen_arg_hwcap = arg->hwcap; seen_arg_hwcap2 = arg->hwcap2; }\n"
                "    return one;\n"
                "}\n"
                "int probe(void) __attribute__((ifunc(\"resolve_probe\")));\n"
                "#ifdef DYNAMIC\n"
                "int puts(const char *);\n"
                "#endif\n"
                "int main(int argc, char **argv, char **envp)\n"
                "{\n"
                "    unsigned long hwcap = 0, hwcap2 = 0;\n"
                "    char **cursor = envp;\n"
                "    while (*cursor) cursor += 1;\n"
                "    for (unsigned long *aux = (unsigned long *)(cursor + 1); aux[0]; aux += 2)\n"
                "    {\n"
                "        if (aux[0] == 16) hwcap = aux[1];\n"
                "        if (aux[0] == 26) hwcap2 = aux[1];\n"
                "    }\n"
                "    int ok = probe() == 1 && resolutions == 1 && hwcap != 0 && seen_hwcap == (hwcap | (1UL << 62)) && seen_arg &&\n"
                "             seen_size >= 24 && seen_arg_hwcap == hwcap && seen_arg_hwcap2 == hwcap2;\n"
                "#ifdef DYNAMIC\n"
                "    puts(ok ? \"hwcap ok\" : \"hwcap wrong\");\n"
                "#endif\n"
                "    return !ok;\n"
                "}\n"))));
            for (u32 index = 0; index < 2; index += 1)
            {
                String8 object_path = string_format_z(arena, S8("{S8}/hwcap-{S8}.o"), root, mains[index]);
                String8 compile[] = {S8("-w"), S8("-O2"), S8("-fPIE"), index ? S8("-DDYNAMIC") : S8("-UDYNAMIC"), S8("-c"), hwcap_source,
                                     S8("-o"), object_path};
                bool compiled_host = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(compile));
                BUSTER_TEST(arguments, compiled_host);
                String8 oracle = string_format_z(arena, S8("{S8}/hwcap-{S8}-oracle"), root, mains[index]);
                String8 host_link[] = {S8("-no-pie"), object_path, S8("-o"), oracle};
                bool linked = compiled_host && compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_link));
                BUSTER_TEST(arguments, linked);
                if (linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, oracle));
                if (compiled_host)
                {
                    String8 output = string_format_z(arena, S8("{S8}/hwcap-{S8}-buster"), root, mains[index]);
                    String8 command[] = {S8("-no-pie"), object_path, S8("-o"), output};
                    CompilerDriverResult compiled = compiler_driver_execute_invocation(
                        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                    BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
                    if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, output));
                }
            }
        }
#endif
#if BUSTER_CPU_ARCH_X86_64
        if (produced)
        {
            // A `-shared` library `ide cc` compiles from the same C publishes
            // its IFUNC slot as R_X86_64_IRELATIVE, and a host-linked program
            // binds to the exported thunk.
            String8 cc_directory = string_format_z(arena, S8("{S8}/cc-shared"), root);
            BUSTER_TEST(arguments, os_make_directory(cc_directory).error.v == 0);
            String8 cc_library = string_format_z(arena, S8("{S8}/libbusterifunc.so"), cc_directory);
            String8 cc_command[] = {S8("-shared"), S8("-fPIC"), S8("-O2"), library_source, S8("-o"), cc_library};
            CompilerDriverResult cc_shared = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(cc_command)));
            BUSTER_TEST_RAW(arguments, cc_shared.error == COMPILER_DRIVER_ERROR_NONE, cc_shared.diagnostic);
            String8 cc_consumer = string_format_z(arena, S8("{S8}/shared-consumer"), cc_directory);
            String8 cc_search = string_format_z(arena, S8("-L{S8}"), cc_directory);
            String8 cc_link[] = {main_objects[1], cc_search, S8("-lbusterifunc"), S8("-o"), cc_consumer};
            bool cc_linked = cc_shared.error == COMPILER_DRIVER_ERROR_NONE &&
                             compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(cc_link));
            BUSTER_TEST(arguments, cc_linked);
            if (cc_linked) BUSTER_TEST(arguments, compiler_driver_tls_export_run(arguments, cc_consumer, cc_directory));

            // The shared object's slots are R_X86_64_IRELATIVE, and the
            // exported name is the thunk the host program binds to.
            String8 library = string_format_z(arena, S8("{S8}/libbusterifunc.so"), root);
            String8 shared_command[] = {S8("-shared"), shared_object, S8("-o"), library};
            CompilerDriverResult shared = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(shared_command)));
            BUSTER_TEST(arguments, shared.error == COMPILER_DRIVER_ERROR_NONE);
            String8 consumer = string_format_z(arena, S8("{S8}/shared-consumer"), root);
            String8 library_directory = string_format_z(arena, S8("-L{S8}"), root);
            String8 host_link[] = {main_objects[1], library_directory, S8("-lbusterifunc"), S8("-o"), consumer};
            bool linked = shared.error == COMPILER_DRIVER_ERROR_NONE &&
                          compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_link));
            BUSTER_TEST(arguments, linked);
            if (linked) BUSTER_TEST(arguments, compiler_driver_tls_export_run(arguments, consumer, root));
            // An executable's DT_PREINIT_ARRAY runs before its dependencies'
            // DT_INIT_ARRAY, so a preinit callback that calls a library's
            // IFUNC needs the slot filled during relocation: the library's
            // R_X86_64_IRELATIVE. The resolver here is idempotent, so the
            // host-built library, which resolves at each binding, is the oracle.
            String8 simple_source = string_format_z(arena, S8("{S8}/simple.c"), root);
            String8 simple_object = string_format_z(arena, S8("{S8}/simple-pic.o"), root);
            String8 preinit_source = string_format_z(arena, S8("{S8}/preinit-consumer.c"), root);
            String8 preinit_object = string_format_z(arena, S8("{S8}/preinit-consumer.o"), root);
            BUSTER_TEST(arguments, file_write(simple_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
                "static int thrice(int x) { return x * 3; }\n"
                "static int (*resolve_scale(void))(int) { return thrice; }\n"
                "int scale(int) __attribute__((ifunc(\"resolve_scale\")));\n"))));
            BUSTER_TEST(arguments, file_write(preinit_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
                "int scale(int);\n"
                "static volatile int early;\n"
                "static void before_constructors(void) { early = scale(7); }\n"
                "__attribute__((section(\".preinit_array\"), used)) static void (*entry)(void) = before_constructors;\n"
                "int main(void) { return early != 21; }\n"))));
            String8 simple_compile[] = {S8("-w"), S8("-O2"), S8("-fPIC"), S8("-c"), simple_source, S8("-o"), simple_object};
            String8 preinit_compile[] = {S8("-w"), S8("-O2"), S8("-fPIE"), S8("-c"), preinit_source, S8("-o"), preinit_object};
            bool preinit_produced = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(simple_compile)) &&
                                    compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(preinit_compile));
            BUSTER_TEST(arguments, preinit_produced);
            for (u32 linker = 0; preinit_produced && linker < 2; linker += 1)
            {
                // A directory per library, so each consumer loads its own libpreinitifunc.so.
                String8 directory = string_format_z(arena, S8("{S8}/preinit-{S8}"), root, linker ? S8("buster") : S8("host"));
                OsDirectoryCreateResult made = os_make_directory(directory);
                BUSTER_TEST(arguments, made.error.v == 0);
                String8 preinit_library = string_format_z(arena, S8("{S8}/libpreinitifunc.so"), directory);
                bool built = false;
                if (linker)
                {
                    String8 command[] = {S8("-shared"), simple_object, S8("-o"), preinit_library};
                    CompilerDriverResult compiled = compiler_driver_execute_invocation(
                        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                    built = compiled.error == COMPILER_DRIVER_ERROR_NONE;
                }
                else
                {
                    String8 command[] = {S8("-shared"), simple_object, S8("-o"), preinit_library};
                    built = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                }
                BUSTER_TEST(arguments, built);
                String8 program = string_format_z(arena, S8("{S8}/preinit-consumer"), directory);
                String8 search = string_format_z(arena, S8("-L{S8}"), directory);
                String8 link[] = {preinit_object, search, S8("-lpreinitifunc"), S8("-o"), program};
                bool program_linked = built && compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(link));
                BUSTER_TEST(arguments, program_linked);
                if (program_linked) BUSTER_TEST(arguments, compiler_driver_tls_export_run(arguments, program, directory));
            }
        }
#endif
        BUSTER_TEST(arguments, os_directory_delete(root));
    }
    return result;
}

// A GNU archive of `count` members named m0.o, m1.o, ... in order, with the
// symbol index GNU ld requires: member `index` defines names[index][0..].
BUSTER_GLOBAL_LOCAL ByteSlice compiler_driver_elf_cpu_runtime_archive(Arena* arena, ByteSlice const* members, String8 const* const* names,
                                                                     u32 const* name_counts, u32 count)
{
    u32 symbol_count = 0;
    u64 string_bytes = 0;
    for (u32 index = 0; index < count; index += 1)
    {
        symbol_count += name_counts[index];
        for (u32 name = 0; name < name_counts[index]; name += 1) string_bytes += names[index][name].length + 1;
    }
    u64 index_length = 4 + 4 * (u64)symbol_count + string_bytes;
    u64 length = 8 + 60 + index_length + (index_length & 1);
    for (u32 index = 0; index < count; index += 1) length += 60 + members[index].length + (members[index].length & 1);
    ByteSlice bytes = {.pointer = arena_allocate(arena, u8, length), .length = length};
    memcpy(bytes.pointer, "!<arch>\n", 8);
    u64 cursor = 8;
    u64 member_offsets[2];
    BUSTER_CHECK(count <= BUSTER_ARRAY_LENGTH(member_offsets));
    u64 offset = 8 + 60 + index_length + (index_length & 1);
    for (u32 index = 0; index < count; index += 1)
    {
        member_offsets[index] = offset;
        offset += 60 + members[index].length + (members[index].length & 1);
    }
    for (u32 index = 0; index <= count; index += 1)
    {
        u8* header = bytes.pointer + cursor;
        memset(header, ' ', 60);
        u64 member_length = index ? members[index - 1].length : index_length;
        String8 name = index ? string_format(arena, S8("m{u32}.o/"), index - 1) : S8("/");
        String8 size = string_format(arena, S8("{u64}"), member_length);
        BUSTER_CHECK(name.length <= 16 && size.length <= 10);
        memcpy(header, name.pointer, name.length);
        header[16] = '0';
        header[28] = '0';
        header[34] = '0';
        memcpy(header + 40, "644", 3);
        memcpy(header + 48, size.pointer, size.length);
        memcpy(header + 58, "`\n", 2);
        cursor += 60;
        if (index)
        {
            memcpy(bytes.pointer + cursor, members[index - 1].pointer, member_length);
            cursor += member_length;
        }
        else
        {
            u8* table = bytes.pointer + cursor;
            table[0] = (u8)(symbol_count >> 24);
            table[1] = (u8)(symbol_count >> 16);
            table[2] = (u8)(symbol_count >> 8);
            table[3] = (u8)symbol_count;
            u64 slot = 4;
            u64 string_cursor = 4 + 4 * (u64)symbol_count;
            for (u32 member = 0; member < count; member += 1)
            {
                for (u32 symbol = 0; symbol < name_counts[member]; symbol += 1)
                {
                    u32 member_offset = (u32)member_offsets[member];
                    table[slot + 0] = (u8)(member_offset >> 24);
                    table[slot + 1] = (u8)(member_offset >> 16);
                    table[slot + 2] = (u8)(member_offset >> 8);
                    table[slot + 3] = (u8)member_offset;
                    slot += 4;
                    String8 symbol_name = names[member][symbol];
                    memcpy(table + string_cursor, symbol_name.pointer, symbol_name.length);
                    table[string_cursor + symbol_name.length] = 0;
                    string_cursor += symbol_name.length + 1;
                }
            }
            cursor += member_length;
        }
        if (member_length & 1) bytes.pointer[cursor++] = '\n';
    }
    BUSTER_CHECK(cursor == length);
    return bytes;
}

#if BUSTER_CPU_ARCH_X86_64
// Issue 3399: GCC and Clang target_clones and __builtin_cpu_* code calls
// __cpu_indicator_init and reads __cpu_model from the static libgcc.a, which
// the host compiler driver links implicitly. This linker selects the
// CPU-feature members only when such a reference remains. The host link is the
// oracle; the program also fails if __cpu_model was never initialized, since
// every x86-64 CPU has SSE2. A -L libgcc.a that lacks the runtime, and a
// sysroot with no GCC installation, refuse the link with the archive and
// symbol named and leave no output. Runtime members that depend on each other
// are selected in either archive order.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_elf_cpu_runtime_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 compiler = S8(BUSTER_HOST_C_COMPILER_ID);
    bool supported = string_equal(compiler, S8("GNU")) || string_equal(compiler, S8("Clang"));
    if (supported)
    {
        String8 root = buster_test_temporary_path(arena, S8("buster-elf-cpu-runtime"), S8(""));
        OsDirectoryCreateResult created = os_make_directory(root);
        BUSTER_TEST(arguments, created.error.v == 0);
        String8 source = string_format_z(arena, S8("{S8}/clones.c"), root);
        String8 object = string_format_z(arena, S8("{S8}/clones.o"), root);
        String8 unrelated_source = string_format_z(arena, S8("{S8}/unrelated.c"), root);
        String8 unrelated_object = string_format_z(arena, S8("{S8}/unrelated.o"), root);
        BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "int puts(const char *);\n"
            "__attribute__((target_clones(\"default\", \"avx2\"))) int work(int x) { return x + 1; }\n"
            "int main(void)\n"
            "{\n"
            "    int ok = work(41) == 42 && __builtin_cpu_supports(\"sse2\");\n"
            "    puts(ok ? \"cpu runtime ok\" : \"cpu runtime wrong\");\n"
            "    return !ok;\n"
            "}\n"))));
        BUSTER_TEST(arguments, file_write(unrelated_source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int unrelated_runtime_member;\n"))));
        String8 compile[] = {S8("-w"), S8("-O2"), S8("-c"), source, S8("-o"), object};
        String8 unrelated_compile[] = {S8("-w"), S8("-O2"), S8("-c"), unrelated_source, S8("-o"), unrelated_object};
        bool produced = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(compile)) &&
                        compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(unrelated_compile));
        BUSTER_TEST(arguments, produced);
        if (produced)
        {
            String8 oracle = string_format_z(arena, S8("{S8}/oracle"), root);
            String8 host_link[] = {object, S8("-o"), oracle};
            bool linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_link));
            BUSTER_TEST(arguments, linked);
            if (linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, oracle));
            String8 kinds[] = {S8("-no-pie"), S8("-pie")};
            for (u32 kind = 0; kind < BUSTER_ARRAY_LENGTH(kinds); kind += 1)
            {
                String8 output = string_format_z(arena, S8("{S8}/buster{S8}"), root, kinds[kind]);
                String8 command[] = {kinds[kind], object, S8("-o"), output};
                CompilerDriverResult compiled = compiler_driver_execute_invocation(
                    arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
                if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, output));
            }

            String8 fake = string_format_z(arena, S8("{S8}/fake"), root);
            created = os_make_directory(fake);
            BUSTER_TEST(arguments, created.error.v == 0);
            String8 fake_archive = string_format_z(arena, S8("{S8}/libgcc.a"), fake);
            ByteSlice member = file_read(arena, unrelated_object, (FileReadOptions){0});
            BUSTER_TEST(arguments, file_write(fake_archive, compiler_driver_elf_semantic_archive(arena, member)));
            String8 search = string_format_z(arena, S8("-L{S8}"), fake);
            String8 lacking_output = string_format_z(arena, S8("{S8}/lacking"), root);
            String8 lacking[] = {search, object, S8("-o"), lacking_output};
            CompilerDriverResult refused = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(lacking)));
            BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_LINK && refused.native_link.error == LINK_ERROR_UNRESOLVED_SYMBOL);
            BUSTER_TEST(arguments, string_equal(refused.native_link.symbol, S8("__cpu_indicator_init")) ||
                                   string_equal(refused.native_link.symbol, S8("__cpu_model")));
            BUSTER_STRING_TEST(arguments, refused.diagnostic,
                               string_format(arena, S8("GCC runtime {S8} does not define {S8}"), fake_archive, refused.native_link.symbol));
            BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(lacking_output));

            String8 empty = string_format_z(arena, S8("{S8}/empty-sysroot"), root);
            created = os_make_directory(empty);
            BUSTER_TEST(arguments, created.error.v == 0);
            String8 sysroot = string_format_z(arena, S8("--sysroot={S8}"), empty);
            String8 missing_output = string_format_z(arena, S8("{S8}/missing"), root);
            String8 missing[] = {sysroot, object, S8("-o"), missing_output};
            refused = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(missing)));
            BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_LINK && refused.native_link.error == LINK_ERROR_UNRESOLVED_SYMBOL);
            BUSTER_STRING_TEST(arguments, refused.diagnostic,
                               string_format(arena, S8("{S8} is defined by the GCC runtime libgcc.a, which was not found in -L directories or the "
                                                       "target's GCC installation"),
                                             refused.native_link.symbol));
            BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(missing_output));
        }

        // A runtime member can need a name that another member defines, in
        // either archive order: here the program references only
        // __cpu_indicator_init, whose member references __cpu_model.
        String8 init_source = string_format_z(arena, S8("{S8}/runtime-init.c"), root);
        String8 init_object = string_format_z(arena, S8("{S8}/runtime-init.o"), root);
        String8 model_source = string_format_z(arena, S8("{S8}/runtime-model.c"), root);
        String8 model_object = string_format_z(arena, S8("{S8}/runtime-model.o"), root);
        String8 caller_source = string_format_z(arena, S8("{S8}/runtime-caller.c"), root);
        String8 caller_object = string_format_z(arena, S8("{S8}/runtime-caller.o"), root);
        BUSTER_TEST(arguments, file_write(init_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "struct model { unsigned vendor, type, subtype, features[1]; };\n"
            "extern struct model __cpu_model;\n"
            "int __cpu_indicator_init(void) { __cpu_model.vendor = 7; return 0; }\n"))));
        BUSTER_TEST(arguments, file_write(model_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "struct model { unsigned vendor, type, subtype, features[1]; };\n"
            "struct model __cpu_model;\n"
            "int model_vendor(void) { return (int)__cpu_model.vendor; }\n"))));
        BUSTER_TEST(arguments, file_write(caller_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "int __cpu_indicator_init(void);\n"
            "int model_vendor(void);\n"
            "int main(void) { return __cpu_indicator_init() != 0 || model_vendor() != 7; }\n"))));
        String8 init_compile[] = {S8("-w"), S8("-O2"), S8("-c"), init_source, S8("-o"), init_object};
        String8 model_compile[] = {S8("-w"), S8("-O2"), S8("-c"), model_source, S8("-o"), model_object};
        String8 caller_compile[] = {S8("-w"), S8("-O2"), S8("-c"), caller_source, S8("-o"), caller_object};
        bool runtime_produced = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(init_compile)) &&
                                compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(model_compile)) &&
                                compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(caller_compile));
        BUSTER_TEST(arguments, runtime_produced);
        for (u32 order = 0; runtime_produced && order < 2; order += 1)
        {
            ByteSlice init_bytes = file_read(arena, init_object, (FileReadOptions){0});
            ByteSlice model_bytes = file_read(arena, model_object, (FileReadOptions){0});
            ByteSlice members[] = {order ? init_bytes : model_bytes, order ? model_bytes : init_bytes};
            static String8 const init_names[] = {S8_INITIALIZER("__cpu_indicator_init")};
            static String8 const model_names[] = {S8_INITIALIZER("__cpu_model"), S8_INITIALIZER("model_vendor")};
            String8 const* member_names[] = {order ? init_names : model_names, order ? model_names : init_names};
            u32 member_name_counts[] = {order ? 1u : 2u, order ? 2u : 1u};
            String8 directory = string_format_z(arena, S8("{S8}/order-{u32}"), root, order);
            created = os_make_directory(directory);
            BUSTER_TEST(arguments, created.error.v == 0);
            String8 runtime_archive = string_format_z(arena, S8("{S8}/libgcc.a"), directory);
            BUSTER_TEST(arguments, file_write(runtime_archive, compiler_driver_elf_cpu_runtime_archive(arena, members, member_names, member_name_counts, BUSTER_ARRAY_LENGTH(members))));
            String8 oracle = string_format_z(arena, S8("{S8}/oracle"), directory);
            String8 host_link[] = {caller_object, runtime_archive, S8("-o"), oracle};
            bool linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_link));
            BUSTER_TEST(arguments, linked);
            if (linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, oracle));
            String8 output = string_format_z(arena, S8("{S8}/buster"), directory);
            String8 search = string_format_z(arena, S8("-L{S8}"), directory);
            String8 command[] = {search, caller_object, S8("-o"), output};
            CompilerDriverResult compiled = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
            BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
            if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, output));
        }

        // Cases ported from Devin's duplicate branch for this issue. A
        // malformed -L libgcc.a is read only by a link that needs the runtime.
        // Ordinary links and weak references must succeed without touching it.
        // A refusal preserves an existing output byte for byte.
        String8 garbage_directory = string_format_z(arena, S8("{S8}/garbage"), root);
        created = os_make_directory(garbage_directory);
        String8 garbage_archive = string_format_z(arena, S8("{S8}/libgcc.a"), garbage_directory);
        bool garbage_written = created.error.v == 0 && file_write(garbage_archive, BUSTER_SLICE_TO_BYTE_SLICE(S8("not an archive")));
        BUSTER_TEST(arguments, garbage_written);
        String8 garbage_search = string_format_z(arena, S8("-L{S8}"), garbage_directory);
        String8 sentinel_output = string_format_z(arena, S8("{S8}/sentinel"), root);
        String8 sentinel = S8("preserve this output");
        if (produced && garbage_written)
        {
            BUSTER_TEST(arguments, file_write(sentinel_output, BUSTER_SLICE_TO_BYTE_SLICE(sentinel)));
            String8 malformed[] = {garbage_search, object, S8("-o"), sentinel_output};
            CompilerDriverResult refused = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(malformed)));
            BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_LINK &&
                                   string_starts_with_sequence(refused.diagnostic, string_format(arena, S8("could not read archive {S8}:"), garbage_archive)));
            BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, file_read(arena, sentinel_output, (FileReadOptions){0})), sentinel);
        }
        String8 ordinary_source = string_format_z(arena, S8("{S8}/ordinary.c"), root);
        String8 ordinary_object = string_format_z(arena, S8("{S8}/ordinary.o"), root);
        String8 weak_source = string_format_z(arena, S8("{S8}/weak.c"), root);
        String8 weak_object = string_format_z(arena, S8("{S8}/weak.o"), root);
        BUSTER_TEST(arguments, file_write(ordinary_source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int main(void) { return 0; }\n"))));
        BUSTER_TEST(arguments, file_write(weak_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "extern struct { unsigned vendor; } __cpu_model __attribute__((weak));\n"
            "int main(void) { return &__cpu_model != 0; }\n"))));
        // The weak object is position-independent, so it reads the absent
        // name through the GOT (issue 3414); it links as both image kinds.
        String8 ordinary_compile[] = {S8("-w"), S8("-O2"), S8("-c"), ordinary_source, S8("-o"), ordinary_object};
        String8 weak_compile[] = {S8("-w"), S8("-O0"), S8("-fPIE"), S8("-c"), weak_source, S8("-o"), weak_object};
        bool unneeded_produced = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(ordinary_compile)) &&
                                 compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(weak_compile));
        BUSTER_TEST(arguments, unneeded_produced);
        for (u32 index = 0; unneeded_produced && garbage_written && index < 3; index += 1)
        {
            String8 output = string_format_z(arena, S8("{S8}/unneeded-{u32}"), root, index);
            String8 command[] = {garbage_search, index == 2 ? S8("-pie") : S8("-no-pie"), index ? weak_object : ordinary_object, S8("-o"), output};
            CompilerDriverResult compiled = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
            BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
            if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, output));
        }

        // The newest GCC version directory wins, and names that are not
        // versions are skipped. The newest archive here is malformed, so the
        // diagnostic names it. Once it is removed, the older unrelated archive
        // is the one reported.
        String8 versions_sysroot = string_format_z(arena, S8("{S8}/versions"), root);
        String8 triple = string_format_z(arena, S8("{S8}/usr/lib/gcc/x86_64-linux-gnu"), versions_sysroot);
        String8 directories[] = {
            versions_sysroot,
            string_format_z(arena, S8("{S8}/usr"), versions_sysroot),
            string_format_z(arena, S8("{S8}/usr/lib"), versions_sysroot),
            string_format_z(arena, S8("{S8}/usr/lib/gcc"), versions_sysroot),
            triple,
            string_format_z(arena, S8("{S8}/9"), triple),
            string_format_z(arena, S8("{S8}/13.2.1"), triple),
            string_format_z(arena, S8("{S8}/include"), triple),
        };
        bool directories_made = true;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(directories); index += 1)
        {
            directories_made = directories_made && os_make_directory(directories[index]).error.v == 0;
        }
        BUSTER_TEST(arguments, directories_made);
        if (produced && directories_made)
        {
            String8 older_archive = string_format_z(arena, S8("{S8}/9/libgcc.a"), triple);
            String8 newest_archive = string_format_z(arena, S8("{S8}/13.2.1/libgcc.a"), triple);
            String8 include_archive = string_format_z(arena, S8("{S8}/include/libgcc.a"), triple);
            ByteSlice member = file_read(arena, unrelated_object, (FileReadOptions){0});
            BUSTER_TEST(arguments, file_write(older_archive, compiler_driver_elf_semantic_archive(arena, member)));
            BUSTER_TEST(arguments, file_write(newest_archive, BUSTER_SLICE_TO_BYTE_SLICE(S8("not an archive"))));
            BUSTER_TEST(arguments, file_write(include_archive, BUSTER_SLICE_TO_BYTE_SLICE(S8("not an archive either"))));
            String8 sysroot = string_format_z(arena, S8("--sysroot={S8}"), versions_sysroot);
            String8 command[] = {sysroot, object, S8("-o"), sentinel_output};
            for (u32 attempt = 0; attempt < 2; attempt += 1)
            {
                BUSTER_TEST(arguments, file_write(sentinel_output, BUSTER_SLICE_TO_BYTE_SLICE(sentinel)));
                CompilerDriverResult refused = compiler_driver_execute_invocation(
                    arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_LINK);
                if (attempt)
                {
                    BUSTER_STRING_TEST(arguments, refused.diagnostic,
                                       string_format(arena, S8("GCC runtime {S8} does not define {S8}"), older_archive, refused.native_link.symbol));
                }
                else
                {
                    BUSTER_TEST(arguments, string_starts_with_sequence(refused.diagnostic,
                                                                       string_format(arena, S8("could not read archive {S8}:"), newest_archive)));
                    BUSTER_TEST(arguments, os_file_delete(newest_archive));
                }
                BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, file_read(arena, sentinel_output, (FileReadOptions){0})), sentinel);
            }
        }
        BUSTER_TEST(arguments, os_directory_delete(root));
    }
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_elf_aarch64_runtime_compile(Arena* arena, String8 target, String8 source, String8 object)
{
    String8 command[] = {S8("-target"), target, S8("-nostdinc"), S8("-g0"), S8("-c"), source, S8("-o"), object};
    return compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
}

// Issue 3508: the AArch64 runtime names are keyed by target and selected on
// any Linux host. This compiler builds AArch64 objects shaped like libgcc.a's
// lse members: an outline-atomic helper that reads __aarch64_have_lse_atomics,
// and the lse-init-like member that defines it. A link that needs the helper
// selects both, in either archive order. A sysroot with no GCC installation,
// or a -L libgcc.a that lacks the helper, refuses the link with the symbol
// named and leaves no output. A name outside the helper family (there is no
// 16-byte ldadd) and each target's runtime names linked for the other target
// never read libgcc.a. On an AArch64 host the selected program also runs.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_elf_aarch64_cpu_runtime_selection_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 root = buster_test_temporary_path(arena, S8("buster-elf-aarch64-cpu-runtime-selection"), S8(""));
    OsDirectoryCreateResult created = os_make_directory(root);
    BUSTER_TEST(arguments, created.error.v == 0);
    String8 aarch64 = S8("aarch64-linux-gnu");
    String8 sources[] = {
        S8("extern _Bool __aarch64_have_lse_atomics;\n"
           "int __aarch64_ldadd4_acq_rel(int value, int *pointer) { int old = *pointer; *pointer = old + value; return old + __aarch64_have_lse_atomics; }\n"),
        S8("_Bool __aarch64_have_lse_atomics = 1;\n"),
        S8("int __aarch64_ldadd4_acq_rel(int, int *);\n"
           "int counter;\n"
           "int main(void) { return __aarch64_ldadd4_acq_rel(1, &counter) != 1 || counter != 1; }\n"),
        S8("int __aarch64_ldadd16_acq(int);\n"
           "int main(void) { return __aarch64_ldadd16_acq(0); }\n"),
        S8("extern int __cpu_model;\n"
           "int main(void) { return __cpu_model; }\n"),
    };
    String8 targets[] = {aarch64, aarch64, aarch64, aarch64, aarch64, S8("x86_64-linux-gnu")};
    // The last object is the AArch64 caller built for x86-64.
    u32 source_indices[] = {0, 1, 2, 3, 4, 2};
    String8 objects[BUSTER_ARRAY_LENGTH(targets)] = {0};
    bool produced = created.error.v == 0;
    for (u32 index = 0; produced && index < BUSTER_ARRAY_LENGTH(sources); index += 1)
    {
        String8 source = string_format_z(arena, S8("{S8}/source-{u32}.c"), root, index);
        produced = file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(sources[index]));
    }
    for (u32 index = 0; produced && index < BUSTER_ARRAY_LENGTH(targets); index += 1)
    {
        String8 source = string_format_z(arena, S8("{S8}/source-{u32}.c"), root, source_indices[index]);
        objects[index] = string_format_z(arena, S8("{S8}/object-{u32}.o"), root, index);
        CompilerDriverResult compiled = compiler_driver_elf_aarch64_runtime_compile(arena, targets[index], source, objects[index]);
        BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE, compiled.diagnostic);
        produced = compiled.error == COMPILER_DRIVER_ERROR_NONE;
    }
    BUSTER_TEST(arguments, produced);
    if (produced)
    {
        String8 helper = S8("__aarch64_ldadd4_acq_rel");
        String8 empty = string_format_z(arena, S8("{S8}/empty-sysroot"), root);
        created = os_make_directory(empty);
        BUSTER_TEST(arguments, created.error.v == 0);
        String8 sysroot = string_format_z(arena, S8("--sysroot={S8}"), empty);
        String8 missing_output = string_format_z(arena, S8("{S8}/missing"), root);
        String8 missing[] = {S8("-target"), aarch64, sysroot, objects[2], S8("-o"), missing_output};
        CompilerDriverResult refused = compiler_driver_execute_invocation(
            arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(missing)));
        BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_LINK && refused.native_link.error == LINK_ERROR_UNRESOLVED_SYMBOL);
        BUSTER_STRING_TEST(arguments, refused.native_link.symbol, helper);
        BUSTER_STRING_TEST(arguments, refused.diagnostic,
                           string_format(arena, S8("{S8} is defined by the GCC runtime libgcc.a, which was not found in -L directories or the "
                                                   "target's GCC installation"),
                                         helper));
        BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(missing_output));

        ByteSlice helper_bytes = file_read(arena, objects[0], (FileReadOptions){0});
        ByteSlice probe_bytes = file_read(arena, objects[1], (FileReadOptions){0});
        static String8 const helper_names[] = {S8_INITIALIZER("__aarch64_ldadd4_acq_rel")};
        static String8 const probe_names[] = {S8_INITIALIZER("__aarch64_have_lse_atomics")};
        for (u32 order = 0; order < 2; order += 1)
        {
            ByteSlice members[] = {order ? probe_bytes : helper_bytes, order ? helper_bytes : probe_bytes};
            String8 const* member_names[] = {order ? probe_names : helper_names, order ? helper_names : probe_names};
            u32 member_name_counts[] = {1, 1};
            String8 directory = string_format_z(arena, S8("{S8}/order-{u32}"), root, order);
            created = os_make_directory(directory);
            BUSTER_TEST(arguments, created.error.v == 0);
            String8 runtime_archive = string_format_z(arena, S8("{S8}/libgcc.a"), directory);
            BUSTER_TEST(arguments, file_write(runtime_archive, compiler_driver_elf_cpu_runtime_archive(arena, members, member_names, member_name_counts,
                                                                                                       BUSTER_ARRAY_LENGTH(members))));
            String8 output = string_format_z(arena, S8("{S8}/buster"), directory);
            String8 search = string_format_z(arena, S8("-L{S8}"), directory);
            String8 command[] = {S8("-target"), aarch64, search, objects[2], S8("-o"), output};
            CompilerDriverResult compiled = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
            BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE, compiled.diagnostic);
#if BUSTER_CPU_ARCH_AARCH64
            if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, output));
#endif
        }

        String8 lacking_directory = string_format_z(arena, S8("{S8}/lacking"), root);
        created = os_make_directory(lacking_directory);
        BUSTER_TEST(arguments, created.error.v == 0);
        String8 lacking_archive = string_format_z(arena, S8("{S8}/libgcc.a"), lacking_directory);
        BUSTER_TEST(arguments, file_write(lacking_archive, compiler_driver_elf_semantic_archive(arena, probe_bytes)));
        String8 lacking_output = string_format_z(arena, S8("{S8}/lacking-output"), root);
        String8 lacking_search = string_format_z(arena, S8("-L{S8}"), lacking_directory);
        String8 lacking[] = {S8("-target"), aarch64, lacking_search, objects[2], S8("-o"), lacking_output};
        refused = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(lacking)));
        BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_LINK && refused.native_link.error == LINK_ERROR_UNRESOLVED_SYMBOL);
        BUSTER_STRING_TEST(arguments, refused.native_link.symbol, helper);
        BUSTER_STRING_TEST(arguments, refused.diagnostic, string_format(arena, S8("GCC runtime {S8} does not define {S8}"), lacking_archive, helper));
        BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(lacking_output));

        // Whether these links succeed depends on whether the host has the
        // target's libc; either way the malformed libgcc.a is never read.
        String8 garbage_directory = string_format_z(arena, S8("{S8}/garbage"), root);
        created = os_make_directory(garbage_directory);
        String8 garbage_archive = string_format_z(arena, S8("{S8}/libgcc.a"), garbage_directory);
        bool garbage_written = created.error.v == 0 && file_write(garbage_archive, BUSTER_SLICE_TO_BYTE_SLICE(S8("not an archive")));
        BUSTER_TEST(arguments, garbage_written);
        String8 garbage_search = string_format_z(arena, S8("-L{S8}"), garbage_directory);
        String8 unread[] = {aarch64, aarch64, S8("x86_64-linux-gnu")};
        u32 unread_objects[] = {3, 4, 5};
        for (u32 index = 0; garbage_written && index < BUSTER_ARRAY_LENGTH(unread); index += 1)
        {
            String8 output = string_format_z(arena, S8("{S8}/unread-{u32}"), root, index);
            String8 command[] = {S8("-target"), unread[index], garbage_search, objects[unread_objects[index]], S8("-o"), output};
            CompilerDriverResult linked = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
            BUSTER_TEST_RAW(arguments, !string_starts_with_sequence(linked.diagnostic, S8("could not read archive")) &&
                                       !string_starts_with_sequence(linked.diagnostic, S8("GCC runtime")),
                            linked.diagnostic);
        }
    }
    BUSTER_TEST(arguments, os_directory_delete(root));
    return result;
}

#if BUSTER_CPU_ARCH_AARCH64
// Issue 3508: GCC 10+ and Clang default to -moutline-atomics on AArch64
// Linux, so __atomic_* builtins call __aarch64_<operation><bytes>_<order>
// helpers. GCC 14+ and compiler-rt FMV resolvers read __aarch64_cpu_features.
// Both live only in the static libgcc.a: the lse members with lse-init.o and
// its constructor, and cpuinfo.o. The host link is the oracle. The atomics
// program checks its own results. The FMV probe fails unless cpuinfo.o's
// constructor set the initialized bit (63). An older libgcc.a has no
// cpuinfo.o; then the host link fails, and this linker must refuse with the
// archive and symbol named. As on x86-64, a missing runtime refuses the link.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_elf_aarch64_cpu_runtime_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 compiler = S8(BUSTER_HOST_C_COMPILER_ID);
    bool supported = string_equal(compiler, S8("GNU")) || string_equal(compiler, S8("Clang"));
    if (supported)
    {
        String8 root = buster_test_temporary_path(arena, S8("buster-elf-aarch64-cpu-runtime"), S8(""));
        OsDirectoryCreateResult created = os_make_directory(root);
        BUSTER_TEST(arguments, created.error.v == 0);
        String8 atomics_source = string_format_z(arena, S8("{S8}/atomics.c"), root);
        String8 atomics_object = string_format_z(arena, S8("{S8}/atomics.o"), root);
        String8 features_source = string_format_z(arena, S8("{S8}/features.c"), root);
        String8 features_object = string_format_z(arena, S8("{S8}/features.o"), root);
        String8 unrelated_source = string_format_z(arena, S8("{S8}/unrelated.c"), root);
        String8 unrelated_object = string_format_z(arena, S8("{S8}/unrelated.o"), root);
        BUSTER_TEST(arguments, file_write(atomics_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "int puts(const char *);\n"
            "long counter;\n"
            "int flag;\n"
            "int main(void)\n"
            "{\n"
            "    __atomic_fetch_add(&counter, 41, __ATOMIC_ACQ_REL);\n"
            "    long expected = 41;\n"
            "    int ok = __atomic_compare_exchange_n(&counter, &expected, 42, 0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE) && counter == 42;\n"
            "    __atomic_fetch_or(&flag, 1, __ATOMIC_RELAXED);\n"
            "    ok = ok && flag == 1;\n"
            "    puts(ok ? \"outline atomics ok\" : \"outline atomics wrong\");\n"
            "    return !ok;\n"
            "}\n"))));
        BUSTER_TEST(arguments, file_write(features_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
            "extern struct { unsigned long long features; } __aarch64_cpu_features;\n"
            "int main(void) { return (int)(__aarch64_cpu_features.features >> 63) != 1; }\n"))));
        BUSTER_TEST(arguments, file_write(unrelated_source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int unrelated_runtime_member;\n"))));
        // Baseline ARMv8.0 keeps the helpers out of line even where the host
        // compiler defaults to an LSE-capable architecture.
        String8 atomics_compile[] = {S8("-w"), S8("-O2"), S8("-march=armv8-a"), S8("-moutline-atomics"), S8("-c"), atomics_source, S8("-o"), atomics_object};
        String8 features_compile[] = {S8("-w"), S8("-O2"), S8("-c"), features_source, S8("-o"), features_object};
        String8 unrelated_compile[] = {S8("-w"), S8("-O2"), S8("-c"), unrelated_source, S8("-o"), unrelated_object};
        bool produced = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(atomics_compile)) &&
                        compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(features_compile)) &&
                        compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(unrelated_compile));
        BUSTER_TEST(arguments, produced);
        if (produced)
        {
            String8 oracle = string_format_z(arena, S8("{S8}/atomics-oracle"), root);
            String8 host_link[] = {atomics_object, S8("-o"), oracle};
            bool linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_link));
            BUSTER_TEST(arguments, linked);
            if (linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, oracle));
            String8 output = string_format_z(arena, S8("{S8}/atomics-buster"), root);
            String8 command[] = {atomics_object, S8("-o"), output};
            CompilerDriverResult compiled = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
            BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE, compiled.diagnostic);
            if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, output));

            // The host libgcc.a decides whether FMV's runtime exists; the
            // failed host link's stderr is expected output in that case.
            String8 features_oracle = string_format_z(arena, S8("{S8}/features-oracle"), root);
            String8 features_host_link[] = {features_object, S8("-o"), features_oracle};
            bool features_linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(features_host_link));
            if (features_linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, features_oracle));
            String8 features_output = string_format_z(arena, S8("{S8}/features-buster"), root);
            String8 features_command[] = {features_object, S8("-o"), features_output};
            compiled = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(features_command)));
            if (features_linked)
            {
                BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE, compiled.diagnostic);
                if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, features_output));
            }
            else
            {
                BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_LINK && compiled.native_link.error == LINK_ERROR_UNRESOLVED_SYMBOL);
                BUSTER_STRING_TEST(arguments, compiled.native_link.symbol, S8("__aarch64_cpu_features"));
                BUSTER_TEST_RAW(arguments, string_starts_with_sequence(compiled.diagnostic, S8("GCC runtime ")) &&
                                           string_ends_with_sequence(compiled.diagnostic, S8(" does not define __aarch64_cpu_features")),
                                compiled.diagnostic);
                BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(features_output));
            }

            String8 fake = string_format_z(arena, S8("{S8}/fake"), root);
            created = os_make_directory(fake);
            BUSTER_TEST(arguments, created.error.v == 0);
            String8 fake_archive = string_format_z(arena, S8("{S8}/libgcc.a"), fake);
            ByteSlice member = file_read(arena, unrelated_object, (FileReadOptions){0});
            BUSTER_TEST(arguments, file_write(fake_archive, compiler_driver_elf_semantic_archive(arena, member)));
            String8 search = string_format_z(arena, S8("-L{S8}"), fake);
            String8 lacking_output = string_format_z(arena, S8("{S8}/lacking"), root);
            String8 lacking[] = {search, atomics_object, S8("-o"), lacking_output};
            CompilerDriverResult refused = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(lacking)));
            BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_LINK && refused.native_link.error == LINK_ERROR_UNRESOLVED_SYMBOL);
            BUSTER_TEST(arguments, string_starts_with_sequence(refused.native_link.symbol, S8("__aarch64_")));
            BUSTER_STRING_TEST(arguments, refused.diagnostic,
                               string_format(arena, S8("GCC runtime {S8} does not define {S8}"), fake_archive, refused.native_link.symbol));
            BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(lacking_output));

            String8 empty = string_format_z(arena, S8("{S8}/empty-sysroot"), root);
            created = os_make_directory(empty);
            BUSTER_TEST(arguments, created.error.v == 0);
            String8 sysroot = string_format_z(arena, S8("--sysroot={S8}"), empty);
            String8 missing_output = string_format_z(arena, S8("{S8}/missing"), root);
            String8 missing[] = {sysroot, atomics_object, S8("-o"), missing_output};
            refused = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(missing)));
            BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_LINK && refused.native_link.error == LINK_ERROR_UNRESOLVED_SYMBOL);
            BUSTER_STRING_TEST(arguments, refused.diagnostic,
                               string_format(arena, S8("{S8} is defined by the GCC runtime libgcc.a, which was not found in -L directories or the "
                                                       "target's GCC installation"),
                                             refused.native_link.symbol));
            BUSTER_TEST(arguments, !compiler_driver_object_path_test_file_exists(missing_output));
        }
        BUSTER_TEST(arguments, os_directory_delete(root));
    }
    return result;
}
#endif

// Issue 3414: default PIE GCC and Clang objects read an absent weak name's
// address through the GOT. On x86-64 that is R_X86_64_GOTPCREL or
// REX_GOTPCRELX, a site that cannot always be rewritten into a direct form
// (`cmpq $0, name@GOTPCREL(%rip)`); on AArch64 it is the ADR_GOT_PAGE and
// LD64_GOT_LO12_NC pair. The libc-free programs take the static writer, the
// libc one the dynamic writer, and every program must read both names as null
// in each output kind the target writes, as the host linker does. AArch64
// Linux writes only fixed-address executables, so it links -no-pie alone.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_elf_weak_got_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 compiler = S8(BUSTER_HOST_C_COMPILER_ID);
    bool supported = string_equal(compiler, S8("GNU")) || string_equal(compiler, S8("Clang"));
    if (supported)
    {
        typedef struct ElfWeakGotCase ElfWeakGotCase;
        struct ElfWeakGotCase
        {
            String8 name;
            String8 program;
        };
        ElfWeakGotCase cases[] = {
            {S8("compare"), S8("extern void weak_got_hook(void) __attribute__((weak));\n"
                "extern int weak_got_data __attribute__((weak));\n"
                "int main(void) { return (weak_got_hook ? 1 : 0) | (&weak_got_data ? 2 : 0); }\n")},
            {S8("load"), S8("extern void weak_got_hook(void) __attribute__((weak));\n"
                "extern int weak_got_data __attribute__((weak));\n"
                "__attribute__((noinline)) int weak_got_count(void const* a, void const* b) { return (a != 0) + (b != 0); }\n"
                "int main(void) { return weak_got_count((void const*)weak_got_hook, &weak_got_data); }\n")},
            {S8("libc"), S8("int puts(const char*);\n"
                "extern void weak_got_hook(void) __attribute__((weak));\n"
                "extern int weak_got_data __attribute__((weak));\n"
                "int main(void)\n"
                "{\n"
                "    int absent = !weak_got_hook && !&weak_got_data;\n"
                "    puts(absent ? \"weak absent\" : \"weak present\");\n"
                "    return !absent;\n"
                "}\n")},
        };
        String8 root = buster_test_temporary_path(arena, S8("buster-elf-weak-got"), S8(""));
        OsDirectoryCreateResult created = os_make_directory(root);
        BUSTER_TEST(arguments, created.error.v == 0);
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
        {
            ElfWeakGotCase test = cases[index];
            String8 source = string_format_z(arena, S8("{S8}/{S8}.c"), root, test.name);
            String8 object = string_format_z(arena, S8("{S8}/{S8}.o"), root, test.name);
            BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(test.program)));
            String8 compile[] = {S8("-w"), S8("-O2"), S8("-fPIE"), S8("-c"), source, S8("-o"), object};
            bool produced = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(compile));
            BUSTER_TEST(arguments, produced);
            if (produced)
            {
                // The input must exercise the GOT form, not a direct one.
                ObjectFile read = object_read(arena, file_read(arena, object, (FileReadOptions){0}), target_native);
                BUSTER_TEST(arguments, read.error == OBJECT_ERROR_NONE);
                u32 got_hook = 0;
                u32 got_data = 0;
                for (u32 relocation = 0; read.error == OBJECT_ERROR_NONE && relocation < read.relocation_count; relocation += 1)
                {
                    ObjectRelocation* entry = read.relocations + relocation;
                    String8 name = entry->symbol < read.symbol_count ? read.symbols[entry->symbol].name : (String8){0};
#if BUSTER_CPU_ARCH_X86_64
                    bool got = object_relocation_kind_is_x86_got(entry->kind);
#else
                    bool got = entry->kind == OBJECT_RELOCATION_AARCH64_ELF_GOT_PAGE21 || entry->kind == OBJECT_RELOCATION_AARCH64_ELF_GOT_LD64_LO12;
#endif
                    got_hook += got && string_equal(name, S8("weak_got_hook"));
                    got_data += got && string_equal(name, S8("weak_got_data"));
                }
                BUSTER_TEST(arguments, got_hook != 0 && got_data != 0);
#if BUSTER_CPU_ARCH_X86_64
                String8 kinds[] = {S8("-pie"), S8("-no-pie")};
#else
                String8 kinds[] = {S8("-no-pie")};
#endif
                for (u32 kind = 0; kind < BUSTER_ARRAY_LENGTH(kinds); kind += 1)
                {
                    String8 oracle = string_format_z(arena, S8("{S8}/{S8}-oracle{S8}"), root, test.name, kinds[kind]);
                    String8 host_link[] = {kinds[kind], object, S8("-o"), oracle};
                    bool linked = compiler_driver_elf_semantic_host(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_link));
                    BUSTER_TEST(arguments, linked);
                    if (linked) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, oracle));
                    String8 output = string_format_z(arena, S8("{S8}/{S8}-buster{S8}"), root, test.name, kinds[kind]);
                    String8 command[] = {kinds[kind], object, S8("-o"), output};
                    CompilerDriverResult compiled = compiler_driver_execute_invocation(
                        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                    BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
                    if (compiled.error != COMPILER_DRIVER_ERROR_NONE) arguments->show(arguments, S8("weak GOT {S8}{S8}: {S8}\n"), test.name, kinds[kind], compiled.diagnostic);
                    if (compiled.error == COMPILER_DRIVER_ERROR_NONE) BUSTER_TEST(arguments, compiler_driver_elf_semantic_run(arena, output));
                }
            }
        }
        BUSTER_TEST(arguments, os_directory_delete(root));
    }
    return result;
}
#endif

#if !BUSTER_ANDROID && !BUSTER_IOS
// Reads `width` little-endian bytes of an ELF image the test does not trust.
BUSTER_GLOBAL_LOCAL bool compiler_driver_android_preinit_field(ByteSlice bytes, u64 offset, u32 width, u64* value)
{
    bool result = width <= sizeof(*value) && offset <= bytes.length && width <= bytes.length - offset;
    *value = 0;
    for (u32 index = 0; result && index < width; index += 1) *value |= (u64)bytes.pointer[offset + index] << (index * 8);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_android_symbol_value(ByteSlice bytes, String8 wanted, u64* value)
{
    u64 section_table = 0;
    u64 section_stride = 0;
    u64 section_count = 0;
    bool valid = compiler_driver_android_preinit_field(bytes, 40, 8, &section_table) &&
                 compiler_driver_android_preinit_field(bytes, 58, 2, &section_stride) &&
                 compiler_driver_android_preinit_field(bytes, 60, 2, &section_count) && section_stride >= 64 &&
                 section_table <= bytes.length && section_count <= (bytes.length - section_table) / section_stride;
    bool found = false;
    for (u64 section = 0; valid && !found && section < section_count; section += 1)
    {
        u64 header = section_table + section * section_stride;
        u64 type = 0;
        u64 offset = 0;
        u64 size = 0;
        u64 strings_index = 0;
        u64 entry_size = 0;
        valid = compiler_driver_android_preinit_field(bytes, header + 4, 4, &type) &&
                compiler_driver_android_preinit_field(bytes, header + 24, 8, &offset) &&
                compiler_driver_android_preinit_field(bytes, header + 32, 8, &size) &&
                compiler_driver_android_preinit_field(bytes, header + 40, 4, &strings_index) &&
                compiler_driver_android_preinit_field(bytes, header + 56, 8, &entry_size);
        if (valid && type == 2)
        {
            valid = strings_index < section_count && entry_size >= 24 && offset <= bytes.length && size <= bytes.length - offset;
            u64 strings_header = section_table + strings_index * section_stride;
            u64 strings_offset = 0;
            u64 strings_size = 0;
            valid = valid && compiler_driver_android_preinit_field(bytes, strings_header + 24, 8, &strings_offset) &&
                    compiler_driver_android_preinit_field(bytes, strings_header + 32, 8, &strings_size) &&
                    strings_offset <= bytes.length && strings_size <= bytes.length - strings_offset;
            for (u64 entry = 0; valid && !found && entry + entry_size <= size; entry += entry_size)
            {
                u64 name_offset = 0;
                u64 symbol_value = 0;
                valid = compiler_driver_android_preinit_field(bytes, offset + entry, 4, &name_offset) &&
                        compiler_driver_android_preinit_field(bytes, offset + entry + 8, 8, &symbol_value) && name_offset < strings_size;
                u64 name_length = 0;
                while (valid && name_offset + name_length < strings_size &&
                       bytes.pointer[strings_offset + name_offset + name_length] != 0)
                {
                    name_length += 1;
                }
                found = valid && name_offset + name_length < strings_size && name_length == wanted.length &&
                        memcmp(bytes.pointer + strings_offset + name_offset, wanted.pointer, wanted.length) == 0;
                if (found) *value = symbol_value;
            }
        }
    }
    return found;
}

// The DT_PREINIT_ARRAY/DT_PREINIT_ARRAYSZ tags (bits 0 and 1 of the result)
// that precede the first DT_NULL, which is where the loader stops reading.
BUSTER_GLOBAL_LOCAL u32 compiler_driver_android_preinit_dynamic(ByteSlice bytes, u64 offset, u64 size, u64* address, u64* length)
{
    u32 tags = 0;
    bool terminated = false;
    for (u64 entry = 0; !terminated && entry + 16 <= size; entry += 16)
    {
        u64 tag = 0;
        u64 value = 0;
        terminated = !compiler_driver_android_preinit_field(bytes, offset + entry, 8, &tag) ||
                     !compiler_driver_android_preinit_field(bytes, offset + entry + 8, 8, &value) || tag == 0;
        if (!terminated && tag == 32)
        {
            *address = value;
            tags |= 1;
        }
        else if (!terminated && tag == 33)
        {
            *length = value;
            tags |= 2;
        }
    }
    return tags;
}

// The value of the first `tag` before DT_NULL, or false without one.
BUSTER_GLOBAL_LOCAL bool compiler_driver_android_dynamic_value(ByteSlice bytes, u64 offset, u64 size, u64 wanted, u64* value)
{
    bool found = false;
    bool terminated = false;
    for (u64 entry = 0; !found && !terminated && entry + 16 <= size; entry += 16)
    {
        u64 tag = 0;
        terminated = !compiler_driver_android_preinit_field(bytes, offset + entry, 8, &tag) || tag == 0;
        found = !terminated && tag == wanted && compiler_driver_android_preinit_field(bytes, offset + entry + 8, 8, value);
    }
    return found;
}

// The file offset of `address` through the load that maps it.
BUSTER_GLOBAL_LOCAL bool compiler_driver_android_address_offset(ByteSlice bytes, u64 program_table, u64 program_count, u64 address, u64* offset)
{
    bool found = false;
    for (u64 header = 0; !found && header < program_count; header += 1)
    {
        u64 base = program_table + header * 56;
        u64 type = 0;
        u64 file_offset = 0;
        u64 virtual_address = 0;
        u64 file_size = 0;
        bool valid = compiler_driver_android_preinit_field(bytes, base, 4, &type) &&
                     compiler_driver_android_preinit_field(bytes, base + 8, 8, &file_offset) &&
                     compiler_driver_android_preinit_field(bytes, base + 16, 8, &virtual_address) &&
                     compiler_driver_android_preinit_field(bytes, base + 32, 8, &file_size);
        found = valid && type == 1 && address >= virtual_address && address - virtual_address < file_size;
        if (found) *offset = file_offset + (address - virtual_address);
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_android_weak_zero_evaluate(ByteSlice bytes, u64 main_address, u64 load_bias, u64* value,
                                                                   u64* loaded_slot, u32* slot_relocation_type)
{
    if (value) *value = 0;
    if (loaded_slot) *loaded_slot = 0;
    if (slot_relocation_type) *slot_relocation_type = 0;
    u64 program_table = 0;
    u64 program_count = 0;
    bool valid = value && bytes.length >= 64 &&
                 compiler_driver_android_preinit_field(bytes, 32, 8, &program_table) &&
                 compiler_driver_android_preinit_field(bytes, 56, 2, &program_count) &&
                 program_table <= bytes.length && program_count <= (bytes.length - program_table) / 56;
    u64 dynamic_offset = 0;
    u64 dynamic_size = 0;
    bool dynamic_found = false;
    for (u64 header = 0; valid && !dynamic_found && header < program_count; header += 1)
    {
        u64 base = program_table + header * 56;
        u64 type = 0;
        valid = compiler_driver_android_preinit_field(bytes, base, 4, &type);
        if (valid && type == 2)
        {
            dynamic_found = compiler_driver_android_preinit_field(bytes, base + 8, 8, &dynamic_offset) &&
                            compiler_driver_android_preinit_field(bytes, base + 32, 8, &dynamic_size);
        }
    }
    u64 rela_address = 0;
    u64 rela_size = 0;
    u64 rela_offset = 0;
    bool has_rela_address = dynamic_found && compiler_driver_android_dynamic_value(bytes, dynamic_offset, dynamic_size, 7, &rela_address);
    bool has_rela_size = dynamic_found && compiler_driver_android_dynamic_value(bytes, dynamic_offset, dynamic_size, 8, &rela_size);
    valid = valid && dynamic_found && has_rela_address == has_rela_size;
    if (valid && has_rela_address && rela_size)
    {
        valid = compiler_driver_android_address_offset(bytes, program_table, program_count, rela_address, &rela_offset);
    }
    u64 main_offset = 0;
    valid = valid && compiler_driver_android_address_offset(bytes, program_table, program_count, main_address, &main_offset);
    bool register_known[31] = {0};
    u64 registers[31] = {0};
    bool started = false;
    bool done = false;
    for (u32 instruction_index = 0; valid && !done && instruction_index < 32; instruction_index += 1)
    {
        u64 instruction_offset = main_offset + (u64)instruction_index * sizeof(u32);
        u64 raw_instruction = 0;
        valid = compiler_driver_android_preinit_field(bytes, instruction_offset, sizeof(u32), &raw_instruction);
        u32 instruction = (u32)raw_instruction;
        u32 opcode = 0;
        if ((instruction & UINT32_C(0x9f000000)) == UINT32_C(0x90000000)) opcode = 1;
        else if ((instruction & UINT32_C(0x9f000000)) == UINT32_C(0x10000000)) opcode = 2;
        else if ((instruction & UINT32_C(0xff800000)) == UINT32_C(0xd2800000)) opcode = 3;
        else if ((instruction & UINT32_C(0xff000000)) == UINT32_C(0x91000000)) opcode = 4;
        else if ((instruction & UINT32_C(0xffc00000)) == UINT32_C(0xf9400000)) opcode = 5;
        switch (opcode)
        {
        case 1:
        case 2:
        {
            started = true;
            u32 destination = instruction & 31;
            u32 immediate = (((instruction >> 29) & 3) | (((instruction >> 5) & 0x7ffff) << 2));
            s64 signed_immediate = immediate & (UINT32_C(1) << 20) ? (s64)immediate - (INT64_C(1) << 21) : immediate;
            u64 place_offset = main_address + (u64)instruction_index * sizeof(u32);
            bool address_valid = main_address <= UINT64_MAX - (u64)instruction_index * sizeof(u32) &&
                                 load_bias <= UINT64_MAX - place_offset;
            u64 place = address_valid ? load_bias + place_offset : 0;
            u64 address = 0;
            if (address_valid && opcode == 1)
            {
                u64 page = place & ~UINT64_C(0xfff);
                s64 displacement = signed_immediate * 4096;
                if (displacement < 0)
                {
                    u64 magnitude = (u64)-displacement;
                    address_valid = page >= magnitude;
                    if (address_valid) address = page - magnitude;
                }
                else
                {
                    address_valid = page <= UINT64_MAX - (u64)displacement;
                    if (address_valid) address = page + (u64)displacement;
                }
            }
            else if (address_valid)
            {
                if (signed_immediate < 0)
                {
                    u64 magnitude = (u64)-signed_immediate;
                    address_valid = place >= magnitude;
                    if (address_valid) address = place - magnitude;
                }
                else
                {
                    address_valid = place <= UINT64_MAX - (u64)signed_immediate;
                    if (address_valid) address = place + (u64)signed_immediate;
                }
            }
            valid = valid && address_valid;
            if (destination < BUSTER_ARRAY_LENGTH(registers))
            {
                register_known[destination] = address_valid;
                registers[destination] = address;
            }
        }
        break;
        case 3:
        {
            started = true;
            u32 destination = instruction & 31;
            if (destination < BUSTER_ARRAY_LENGTH(registers))
            {
                u32 shift = ((instruction >> 21) & 3) * 16;
                register_known[destination] = true;
                registers[destination] = (u64)((instruction >> 5) & 0xffff) << shift;
            }
        }
        break;
        case 4:
        {
            if (started)
            {
                u32 destination = instruction & 31;
                u32 source = (instruction >> 5) & 31;
                u32 shift = (instruction >> 22) & 1 ? 12 : 0;
                u64 immediate = (u64)((instruction >> 10) & 0xfff) << shift;
                bool known = source < BUSTER_ARRAY_LENGTH(registers) && register_known[source] &&
                             registers[source] <= UINT64_MAX - immediate;
                if (destination < BUSTER_ARRAY_LENGTH(registers))
                {
                    register_known[destination] = known;
                    if (known) registers[destination] = registers[source] + immediate;
                }
            }
        }
        break;
        case 5:
        {
            if (started)
            {
                u32 destination = instruction & 31;
                u32 source = (instruction >> 5) & 31;
                u64 immediate = (u64)((instruction >> 10) & 0xfff) * 8;
                bool known = false;
                u64 loaded_value = 0;
                u64 slot_address = 0;
                u32 relocation_type = 0;
                bool source_known = source < BUSTER_ARRAY_LENGTH(registers) && register_known[source] &&
                                   registers[source] <= UINT64_MAX - immediate;
                if (source_known && registers[source] + immediate >= load_bias)
                {
                    slot_address = registers[source] + immediate - load_bias;
                    bool relocation_found = false;
                    for (u64 entry = 0; valid && !relocation_found && entry + 24 <= rela_size; entry += 24)
                    {
                        u64 relocation_address = 0;
                        u64 information = 0;
                        u64 addend = 0;
                        valid = compiler_driver_android_preinit_field(bytes, rela_offset + entry, 8, &relocation_address) &&
                                compiler_driver_android_preinit_field(bytes, rela_offset + entry + 8, 8, &information) &&
                                compiler_driver_android_preinit_field(bytes, rela_offset + entry + 16, 8, &addend);
                        if (valid && relocation_address == slot_address)
                        {
                            relocation_found = true;
                            relocation_type = (u32)information;
                            if (relocation_type == 1027)
                            {
                                s64 signed_addend = (s64)addend;
                                if (signed_addend < 0)
                                {
                                    u64 magnitude = (u64)(-(signed_addend + 1)) + 1;
                                    known = load_bias >= magnitude;
                                    if (known) loaded_value = load_bias - magnitude;
                                }
                                else
                                {
                                    known = load_bias <= UINT64_MAX - (u64)signed_addend;
                                    if (known) loaded_value = load_bias + (u64)signed_addend;
                                }
                            }
                        }
                    }
                    if (valid && !relocation_found)
                    {
                        u64 slot_offset = 0;
                        valid = compiler_driver_android_address_offset(bytes, program_table, program_count, slot_address, &slot_offset) &&
                                compiler_driver_android_preinit_field(bytes, slot_offset, sizeof(u64), &loaded_value);
                        known = valid;
                    }
                }
                else if (source_known)
                {
                    valid = false;
                }
                if (loaded_slot) *loaded_slot = slot_address;
                if (slot_relocation_type) *slot_relocation_type = relocation_type;
                if (destination < BUSTER_ARRAY_LENGTH(registers))
                {
                    register_known[destination] = known;
                    if (known) registers[destination] = loaded_value;
                }
            }
        }
        break;
        default:
            done = started;
            break;
        }
    }
    if (valid && register_known[0]) *value = registers[0];
    return valid && started && register_known[0];
}

// Issue 1243: Android PIEs publish `.preinit_array` through DT_PREINIT_ARRAY
// for the bionic linker. This link-only control checks the image itself on
// every host: both tags are present, the array is one 8-byte entry inside a
// writable load, and that entry addresses code inside an executable load.
// Both architectures require a relative relocation and bionic's linker.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_android_preinit_tags(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    String8 source = buster_test_temporary_path(arena, S8("buster-android-preinit"), S8(".c"));
    BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
        "int puts(const char *);\n"
        "static void early(int argc, char **argv, char **envp) { (void)argc; (void)argv; (void)envp; }\n"
        "__attribute__((section(\".preinit_array\"), used)) static void (*entry)(int, char **, char **) = early;\n"
        "int main(void) { return puts(\"x\") < 0; }\n"))));
    // Tags past the first DT_NULL are invisible to the loader and must not count.
    u8 tail[48] = {0};
    tail[16] = 32;
    tail[32] = 33;
    tail[40] = 8;
    u64 tail_address = 0;
    u64 tail_size = 0;
    BUSTER_TEST(arguments, compiler_driver_android_preinit_dynamic((ByteSlice)BUSTER_ARRAY_TO_SLICE(tail), 0, sizeof(tail), &tail_address, &tail_size) == 0);
    tail[0] = 32;
    BUSTER_TEST(arguments, compiler_driver_android_preinit_dynamic((ByteSlice)BUSTER_ARRAY_TO_SLICE(tail), 0, sizeof(tail), &tail_address, &tail_size) == 3);
    String8 targets[] = {S8("x86_64-linux-android"), S8("aarch64-linux-android")};
    for (u32 target = 0; target < BUSTER_ARRAY_LENGTH(targets); target += 1)
    {
        String8 output = buster_test_temporary_path(arena, S8("buster-android-preinit"), S8(""));
        String8 command[] = {S8("-target"), targets[target], S8("-nostdinc"), S8("-g0"), source, S8("-o"), output};
        CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
        BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
        ByteSlice bytes = compiled.error == COMPILER_DRIVER_ERROR_NONE ? file_read(arena, output, (FileReadOptions){0}) : (ByteSlice){0};
        u64 program_table = 0;
        u64 program_count = 0;
        u64 image_type = 0;
        bool valid = bytes.length >= 64 && memcmp(bytes.pointer, "\177ELF\2\1", 6) == 0 &&
                     compiler_driver_android_preinit_field(bytes, 16, 2, &image_type) &&
                     compiler_driver_android_preinit_field(bytes, 32, 8, &program_table) &&
                     compiler_driver_android_preinit_field(bytes, 56, 2, &program_count);
        u64 lowest_load = UINT64_MAX;
        u64 dynamic_offset = 0;
        u64 dynamic_size = 0;
        u64 preinit_address = 0;
        u64 preinit_size = 0;
        u32 tags = 0;
        for (u64 header = 0; valid && header < program_count; header += 1)
        {
            u64 base = program_table + header * 56;
            u64 type = 0;
            u64 address = 0;
            valid = compiler_driver_android_preinit_field(bytes, base, 4, &type) &&
                    compiler_driver_android_preinit_field(bytes, base + 16, 8, &address);
            if (valid && type == 2)
            {
                valid = compiler_driver_android_preinit_field(bytes, base + 8, 8, &dynamic_offset) &&
                        compiler_driver_android_preinit_field(bytes, base + 32, 8, &dynamic_size);
            }
            if (valid && type == 1) lowest_load = BUSTER_MIN(lowest_load, address);
        }
        BUSTER_TEST(arguments, valid && image_type == 3 && lowest_load == 0);
        if (valid) tags = compiler_driver_android_preinit_dynamic(bytes, dynamic_offset, dynamic_size, &preinit_address, &preinit_size);
        BUSTER_TEST(arguments, valid && tags == 3 && preinit_size == 8);
        // Map the array through the load that holds it, then the entry
        // through the load that executes it.
        u64 function = 0;
        bool writable = false;
        bool executable = false;
        for (u32 pass = 0; pass < 2; pass += 1)
        {
            for (u64 header = 0; valid && header < program_count; header += 1)
            {
                u64 base = program_table + header * 56;
                u64 type = 0;
                u64 flags = 0;
                u64 offset = 0;
                u64 address = 0;
                u64 file_size = 0;
                valid = compiler_driver_android_preinit_field(bytes, base, 4, &type) && compiler_driver_android_preinit_field(bytes, base + 4, 4, &flags) &&
                        compiler_driver_android_preinit_field(bytes, base + 8, 8, &offset) &&
                        compiler_driver_android_preinit_field(bytes, base + 16, 8, &address) &&
                        compiler_driver_android_preinit_field(bytes, base + 32, 8, &file_size);
                u64 wanted = pass ? function : preinit_address;
                u64 width = pass ? 1 : preinit_size;
                if (valid && type == 1 && wanted >= address && wanted - address <= file_size && width <= file_size - (wanted - address))
                {
                    if (pass) executable = executable || (flags & 1) != 0;
                    else
                    {
                        writable = writable || (flags & 2) != 0;
                        valid = compiler_driver_android_preinit_field(bytes, offset + (preinit_address - address), 8, &function);
                    }
                }
            }
        }
        BUSTER_TEST(arguments, valid && writable && function != 0 && executable);
        if (valid)
        {
            u64 flags = 0;
            u64 relocations = 0;
            u64 relocation_size = 0;
            u64 relocation_offset = 0;
            u64 interpreter_offset = 0;
            bool relocated = false;
            bool pie = compiler_driver_android_dynamic_value(bytes, dynamic_offset, dynamic_size, 0x6ffffffb, &flags) && (flags & 0x08000000) != 0;
            bool listed = compiler_driver_android_dynamic_value(bytes, dynamic_offset, dynamic_size, 7, &relocations) &&
                          compiler_driver_android_dynamic_value(bytes, dynamic_offset, dynamic_size, 8, &relocation_size) &&
                          compiler_driver_android_address_offset(bytes, program_table, program_count, relocations, &relocation_offset);
            for (u64 entry = 0; listed && !relocated && entry + 24 <= relocation_size; entry += 24)
            {
                u64 slot = 0;
                u64 information = 0;
                u64 addend = 0;
                relocated = compiler_driver_android_preinit_field(bytes, relocation_offset + entry, 8, &slot) &&
                            compiler_driver_android_preinit_field(bytes, relocation_offset + entry + 8, 8, &information) &&
                            compiler_driver_android_preinit_field(bytes, relocation_offset + entry + 16, 8, &addend) &&
                            slot == preinit_address && information == (target == 0 ? 8u : 1027u) && addend == function;
            }
            bool interpreter = false;
            for (u64 header = 0; header < program_count; header += 1)
            {
                u64 type = 0;
                if (compiler_driver_android_preinit_field(bytes, program_table + header * 56, 4, &type) && type == 3)
                {
                    interpreter = compiler_driver_android_preinit_field(bytes, program_table + header * 56 + 8, 8, &interpreter_offset) &&
                                  interpreter_offset <= bytes.length && bytes.length - interpreter_offset >= sizeof("/system/bin/linker64") &&
                                  memcmp(bytes.pointer + interpreter_offset, "/system/bin/linker64", sizeof("/system/bin/linker64")) == 0;
                }
            }
            BUSTER_TEST(arguments, pie && relocated && interpreter);
        }
    }
    String8 pointer_source = buster_test_temporary_path(arena, S8("buster-android-relative"), S8(".c"));
    BUSTER_TEST(arguments, file_write(pointer_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
        "static int x;\n"
        "int *p = &x;\n"
        "int main(void) { return *p; }\n"))));
    String8 pointer_output = buster_test_temporary_path(arena, S8("buster-android-relative"), S8(""));
    String8 pointer_command[] = {S8("-target"), S8("aarch64-linux-android"), S8("-nostdinc"), S8("-g0"), pointer_source, S8("-o"), pointer_output};
    CompilerDriverResult pointer_compiled =
        compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(pointer_command)));
    BUSTER_TEST(arguments, pointer_compiled.error == COMPILER_DRIVER_ERROR_NONE);
    ByteSlice pointer_bytes = pointer_compiled.error == COMPILER_DRIVER_ERROR_NONE ? file_read(arena, pointer_output, (FileReadOptions){0}) : (ByteSlice){0};
    u64 pointer_table = 0;
    u64 pointer_count = 0;
    u64 pointer_entry = 0;
    u64 pointer_type = 0;
    bool pointer_valid = pointer_bytes.length >= 64 &&
                         compiler_driver_android_preinit_field(pointer_bytes, 16, 2, &pointer_type) && pointer_type == 3 &&
                         compiler_driver_android_preinit_field(pointer_bytes, 24, 8, &pointer_entry) &&
                         compiler_driver_android_preinit_field(pointer_bytes, 32, 8, &pointer_table) &&
                         compiler_driver_android_preinit_field(pointer_bytes, 56, 2, &pointer_count);
    u64 entry_offset = 0;
    u64 entry_word = 0;
    bool no_loader_finalizer = pointer_valid &&
                               compiler_driver_android_address_offset(pointer_bytes, pointer_table, pointer_count, pointer_entry, &entry_offset) &&
                               compiler_driver_android_preinit_field(pointer_bytes, entry_offset + 4 * sizeof(u32), 4, &entry_word) &&
                               entry_word != UINT32_C(0xb4000080);
    BUSTER_TEST(arguments, no_loader_finalizer);
    u64 x_address = 0;
    bool x_symbol = pointer_valid && compiler_driver_android_symbol_value(pointer_bytes, S8("x"), &x_address);
    BUSTER_TEST(arguments, x_symbol);
    u64 dynamic_offset = 0;
    u64 dynamic_size = 0;
    u64 relocations = 0;
    u64 relocation_size = 0;
    u64 relocation_offset = 0;
    bool listed = pointer_valid && compiler_driver_android_address_offset(pointer_bytes, pointer_table, pointer_count,
                                                                           pointer_entry, &entry_offset);
    for (u64 header = 0; pointer_valid && header < pointer_count; header += 1)
    {
        u64 base = pointer_table + header * 56;
        u64 type = 0;
        if (compiler_driver_android_preinit_field(pointer_bytes, base, 4, &type) && type == 2)
        {
            listed = compiler_driver_android_preinit_field(pointer_bytes, base + 8, 8, &dynamic_offset) &&
                     compiler_driver_android_preinit_field(pointer_bytes, base + 32, 8, &dynamic_size);
        }
    }
    listed = listed && compiler_driver_android_dynamic_value(pointer_bytes, dynamic_offset, dynamic_size, 7, &relocations) &&
             compiler_driver_android_dynamic_value(pointer_bytes, dynamic_offset, dynamic_size, 8, &relocation_size) &&
             compiler_driver_android_address_offset(pointer_bytes, pointer_table, pointer_count, relocations, &relocation_offset);
    bool relative_pointer = false;
    for (u64 entry = 0; listed && !relative_pointer && entry + 24 <= relocation_size; entry += 24)
    {
        u64 slot = 0;
        u64 information = 0;
        u64 addend = 0;
        u64 slot_offset = 0;
        if (compiler_driver_android_preinit_field(pointer_bytes, relocation_offset + entry, 8, &slot) &&
            compiler_driver_android_preinit_field(pointer_bytes, relocation_offset + entry + 8, 8, &information) &&
            compiler_driver_android_preinit_field(pointer_bytes, relocation_offset + entry + 16, 8, &addend) &&
            information == 1027 && addend == x_address &&
            compiler_driver_android_address_offset(pointer_bytes, pointer_table, pointer_count, slot, &slot_offset))
        {
            u64 initial_pointer = 0;
            relative_pointer = compiler_driver_android_preinit_field(pointer_bytes, slot_offset, 8, &initial_pointer) && initial_pointer == addend;
        }
    }
    BUSTER_TEST(arguments, relative_pointer);
    String8 weak_source = buster_test_temporary_path(arena, S8("buster-android-weak-zero"), S8(".c"));
    BUSTER_TEST(arguments, file_write(weak_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
        "extern int absent __attribute__((weak,visibility(\"hidden\")));\n"
        "int main(void) { return &absent != 0; }\n"))));
    String8 weak_output = buster_test_temporary_path(arena, S8("buster-android-weak-zero"), S8(""));
    String8 weak_command[] = {S8("-target"), S8("aarch64-linux-android"), S8("-nostdinc"), S8("-g0"), S8("-fPIC"), weak_source, S8("-o"), weak_output};
    CompilerDriverResult weak_compiled =
        compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(weak_command)));
    BUSTER_TEST(arguments, weak_compiled.error == COMPILER_DRIVER_ERROR_NONE);
    ByteSlice weak_bytes = weak_compiled.error == COMPILER_DRIVER_ERROR_NONE ? file_read(arena, weak_output, (FileReadOptions){0}) : (ByteSlice){0};
    u64 weak_main = 0;
    bool weak_main_found = compiler_driver_android_symbol_value(weak_bytes, S8("main"), &weak_main);
    BUSTER_TEST(arguments, weak_main_found);
    u64 weak_value_base = UINT64_MAX;
    u64 weak_value_bias = UINT64_MAX;
    u64 weak_slot = 0;
    u32 weak_slot_relocation = 0;
    bool weak_base_known = weak_main_found &&
                           compiler_driver_android_weak_zero_evaluate(weak_bytes, weak_main, 0, &weak_value_base, &weak_slot, &weak_slot_relocation);
    u64 weak_bias_slot = 0;
    u32 weak_bias_relocation = 0;
    bool weak_bias_known = weak_main_found && compiler_driver_android_weak_zero_evaluate(weak_bytes, weak_main, 0x10000, &weak_value_bias,
                                                                                       &weak_bias_slot, &weak_bias_relocation);
    BUSTER_TEST(arguments, weak_base_known && weak_value_base == 0 && weak_bias_known && weak_value_bias == 0);
    String8 visible_weak_source = buster_test_temporary_path(arena, S8("buster-android-weak-visible"), S8(".c"));
    BUSTER_TEST(arguments, file_write(visible_weak_source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
        "extern int present_weak __attribute__((weak));\n"
        "int main(void) { return &present_weak != 0; }\n"))));
    String8 visible_weak_output = buster_test_temporary_path(arena, S8("buster-android-weak-visible"), S8(""));
    String8 visible_weak_command[] = {S8("-target"), S8("aarch64-linux-android"), S8("-nostdinc"), S8("-g0"), S8("-fPIC"),
                                     visible_weak_source, S8("-o"), visible_weak_output};
    CompilerDriverResult visible_weak_compiled = compiler_driver_execute_invocation(
        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(visible_weak_command)));
    BUSTER_TEST(arguments, visible_weak_compiled.error == COMPILER_DRIVER_ERROR_NONE);
    ByteSlice visible_weak_bytes =
        visible_weak_compiled.error == COMPILER_DRIVER_ERROR_NONE ? file_read(arena, visible_weak_output, (FileReadOptions){0}) : (ByteSlice){0};
    u64 visible_weak_main = 0;
    bool visible_weak_main_found = compiler_driver_android_symbol_value(visible_weak_bytes, S8("main"), &visible_weak_main);
    BUSTER_TEST(arguments, visible_weak_main_found);
    u64 visible_weak_value = 0;
    u64 visible_weak_slot = 0;
    u32 visible_weak_relocation = 0;
    bool visible_weak_value_known =
        visible_weak_main_found && compiler_driver_android_weak_zero_evaluate(visible_weak_bytes, visible_weak_main, 0, &visible_weak_value,
                                                                              &visible_weak_slot, &visible_weak_relocation);
    BUSTER_TEST(arguments, visible_weak_main_found && !visible_weak_value_known && visible_weak_slot && visible_weak_relocation == 1025);
    scratch_end(temporary);
    return result;
}
#endif

#if !BUSTER_ANDROID && !BUSTER_IOS
// The Mach-O thread-local contract a platform linker reads, decoded from the
// written bytes independently of object_read: one S_THREAD_LOCAL_VARIABLES
// section whose 24-byte descriptors each carry an UNSIGNED 64-bit extern
// relocation to __tlv_bootstrap at +0 and to a local `$tlv$init` storage
// symbol at +16, with the variable symbols defined on the descriptors.
typedef struct CompilerDriverMachTls CompilerDriverMachTls;
struct CompilerDriverMachTls
{
    u64 descriptor_size;
    u32 descriptor_sections;
    u32 bootstrap_relocations;
    u32 storage_relocations;
    u32 other_relocations;
    u32 descriptor_symbols;
    u32 storage_symbols;
};

BUSTER_GLOBAL_LOCAL bool compiler_driver_mach_field(ByteSlice bytes, u64 offset, u32 width, u64* value)
{
    bool result = offset <= bytes.length && width <= bytes.length - offset;
    *value = 0;
    if (result)
    {
        memcpy(value, bytes.pointer + offset, width);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_mach_string(ByteSlice bytes, u64 table, u64 table_size, u64 index)
{
    String8 result = {0};
    if (index < table_size && table <= bytes.length && table_size <= bytes.length - table)
    {
        result.pointer = (char8*)bytes.pointer + table + index;
        while (index + result.length < table_size && result.pointer[result.length])
        {
            result.length += 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDriverMachTls compiler_driver_mach_tls(ByteSlice bytes)
{
    CompilerDriverMachTls result = {0};
    u64 magic = 0;
    u64 command_count = 0;
    bool valid = compiler_driver_mach_field(bytes, 0, 4, &magic) && magic == 0xfeedfacf && compiler_driver_mach_field(bytes, 16, 4, &command_count);
    u64 command = 32;
    u32 section_number = 0;
    u32 descriptor_number = 0;
    u64 descriptor_relocations = 0;
    u64 descriptor_relocation_count = 0;
    u64 symbols = 0;
    u64 symbol_count = 0;
    u64 strings = 0;
    u64 string_size = 0;
    for (u64 index = 0; valid && index < command_count; index += 1)
    {
        u64 kind = 0;
        u64 size = 0;
        valid = compiler_driver_mach_field(bytes, command, 4, &kind) && compiler_driver_mach_field(bytes, command + 4, 4, &size) && size >= 8;
        if (valid && kind == 0x19)
        {
            u64 section_total = 0;
            valid = compiler_driver_mach_field(bytes, command + 64, 4, &section_total);
            for (u64 section = 0; valid && section < section_total; section += 1)
            {
                u64 header = command + 72 + section * 80;
                u64 flags = 0;
                section_number += 1;
                valid = compiler_driver_mach_field(bytes, header + 64, 4, &flags);
                if (valid && (flags & 0xff) == 0x13)
                {
                    result.descriptor_sections += 1;
                    descriptor_number = section_number;
                    valid = compiler_driver_mach_field(bytes, header + 40, 8, &result.descriptor_size) &&
                            compiler_driver_mach_field(bytes, header + 56, 4, &descriptor_relocations) &&
                            compiler_driver_mach_field(bytes, header + 60, 4, &descriptor_relocation_count);
                }
            }
        }
        if (valid && kind == 0x2)
        {
            valid = compiler_driver_mach_field(bytes, command + 8, 4, &symbols) && compiler_driver_mach_field(bytes, command + 12, 4, &symbol_count) &&
                    compiler_driver_mach_field(bytes, command + 16, 4, &strings) && compiler_driver_mach_field(bytes, command + 20, 4, &string_size);
        }
        command += size;
    }
    for (u64 index = 0; valid && index < symbol_count; index += 1)
    {
        u64 name = 0;
        u64 type = 0;
        u64 section = 0;
        valid = compiler_driver_mach_field(bytes, symbols + index * 16, 4, &name) && compiler_driver_mach_field(bytes, symbols + index * 16 + 4, 1, &type) &&
                compiler_driver_mach_field(bytes, symbols + index * 16 + 5, 1, &section);
        String8 text = compiler_driver_mach_string(bytes, strings, string_size, name);
        bool defined = (type & 0x0e) == 0x0e;
        bool storage = text.length > 9 && string_equal(string_slice(text, text.length - 9, text.length), S8("$tlv$init"));
        result.descriptor_symbols += defined && section == descriptor_number &&
                                     (string_equal(text, S8("_tl")) || string_equal(text, S8("_tz")) || string_equal(text, S8("_ts")));
        result.storage_symbols += defined && storage && !(type & 1) && section != descriptor_number;
    }
    for (u64 index = 0; valid && descriptor_number && index < descriptor_relocation_count; index += 1)
    {
        u64 address = 0;
        u64 information = 0;
        u64 name = 0;
        valid = compiler_driver_mach_field(bytes, descriptor_relocations + index * 8, 4, &address) &&
                compiler_driver_mach_field(bytes, descriptor_relocations + index * 8 + 4, 4, &information) &&
                compiler_driver_mach_field(bytes, symbols + (information & 0xffffff) * 16, 4, &name);
        String8 text = compiler_driver_mach_string(bytes, strings, string_size, name);
        // r_extern, r_length 3 (eight bytes), not pc-relative, UNSIGNED.
        bool unsigned_extern = (information >> 24) == ((3u << 1) | (1u << 3));
        bool bootstrap = unsigned_extern && address % 24 == 0 && string_equal(text, S8("__tlv_bootstrap"));
        bool storage = unsigned_extern && address % 24 == 16 && text.length > 9 &&
                       string_equal(string_slice(text, text.length - 9, text.length), S8("$tlv$init"));
        result.bootstrap_relocations += bootstrap;
        result.storage_relocations += storage;
        result.other_relocations += !bootstrap && !storage;
    }
    if (!valid)
    {
        result = (CompilerDriverMachTls){0};
    }
    return result;
}

// GitHub #1323 M1: Buster's Mach-O TLS objects carry the __thread_vars
// descriptors ld64 requires, read back to the canonical model, and link
// (natively) to the same bytes as a direct compile. ld64.lld links them
// where it is installed; the libSystem stub only supplies __tlv_bootstrap.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_mach_tls_object_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    String8 source = buster_test_temporary_path(arena, S8("buster-mach-tls"), S8(".c"));
    String8 stub = buster_test_temporary_path(arena, S8("buster-mach-tls-libSystem"), S8(".tbd"));
    BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(S8("_Thread_local int tl = 5;\n_Thread_local int tz;\n"
                                                                        "static _Thread_local long ts = 7;\n"
                                                                        "int get(void) { return tl + tz + (int)ts; }\n"
                                                                        "int main(void) { tz = 1; return get() == 13 ? 0 : 1; }\n"))));
    BUSTER_TEST(arguments, file_write(stub, BUSTER_SLICE_TO_BYTE_SLICE(S8("--- !tapi-tbd\ntbd-version: 4\ntargets: [ x86_64-macos, arm64-macos ]\n"
                                                                      "install-name: '/usr/lib/libSystem.B.dylib'\ncurrent-version: 1311\n"
                                                                      "exports:\n  - targets: [ x86_64-macos, arm64-macos ]\n"
                                                                      "    symbols: [ __tlv_bootstrap, dyld_stub_binder ]\n...\n"))));
    String8 linker_names[] = {S8("ld64.lld"), S8("ld64.lld-21"), S8("ld64.lld-20"), S8("ld64.lld-19"), S8("ld64.lld-18")};
    String8 linker = {0};
    for (u32 index = 0; !linker.length && index < BUSTER_ARRAY_LENGTH(linker_names); index += 1)
    {
        linker = executable_resolve_in_path(arena, linker_names[index]);
    }
    String8 targets[] = {S8("aarch64-apple-macos"), S8("x86_64-apple-macos")};
    String8 architectures[] = {S8("arm64"), S8("x86_64")};
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(targets); target_index += 1)
    {
        String8 object_path = buster_test_temporary_path(arena, S8("buster-mach-tls"), string_format(arena, S8("-{u32}.o"), target_index));
        String8 direct_path = buster_test_temporary_path(arena, S8("buster-mach-tls-direct"), string_format(arena, S8("-{u32}"), target_index));
        String8 via_path = buster_test_temporary_path(arena, S8("buster-mach-tls-via"), string_format(arena, S8("-{u32}"), target_index));
        String8 object_command[] = {S8("-c"), S8("-g0"), S8("-target"), targets[target_index], S8("-o"), object_path, source};
        CompilerDriverResult compiled =
            compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(object_command)));
        bool compiled_ok = compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object;
        BUSTER_TEST(arguments, compiled_ok);
        ByteSlice bytes = compiled_ok ? file_read(arena, object_path, (FileReadOptions){0}) : (ByteSlice){0};
        CompilerDriverMachTls tls = compiler_driver_mach_tls(bytes);
        BUSTER_TEST(arguments, tls.descriptor_sections == 1 && tls.descriptor_size == 3 * 24 && tls.bootstrap_relocations == 3 &&
                                   tls.storage_relocations == 3 && tls.other_relocations == 0 && tls.descriptor_symbols == 3 &&
                                   tls.storage_symbols == 3);
        if (compiled_ok)
        {
            // The reader restores the canonical model: the variables on
            // their storage, no descriptor bytes in the TLS template and no
            // `$tlv$init` aliases left to receive descriptors of their own.
            ObjectFile reread = object_read(arena, bytes, compiled.object.target);
            u32 variables = 0;
            u32 aliases = 0;
            for (u32 index = 0; reread.error == OBJECT_ERROR_NONE && index < reread.symbol_count; index += 1)
            {
                ObjectSymbol* symbol = reread.symbols + index;
                bool in_thread_local = symbol->section < reread.section_count && (reread.sections[symbol->section].kind == OBJECT_SECTION_THREAD_LOCAL_DATA ||
                                                                              reread.sections[symbol->section].kind == OBJECT_SECTION_THREAD_LOCAL_ZERO);
                variables += in_thread_local && (string_equal(symbol->name, S8("tl")) || string_equal(symbol->name, S8("tz")) || string_equal(symbol->name, S8("ts")));
                aliases += string_first_sequence(symbol->name, S8("$tlv$init")) < symbol->name.length;
            }
            BUSTER_TEST(arguments, reread.error == OBJECT_ERROR_NONE && variables == 3 && aliases == 0 &&
                                       reread.sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length ==
                                           compiled.object.sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length &&
                                       reread.sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size ==
                                           compiled.object.sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size);
            String8 direct_command[] = {S8("-g0"), S8("-target"), targets[target_index], S8("-o"), direct_path, source};
            String8 via_command[] = {S8("-g0"), S8("-target"), targets[target_index], S8("-o"), via_path, object_path};
            CompilerDriverResult direct =
                compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(direct_command)));
            CompilerDriverResult via =
                compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(via_command)));
            ByteSlice direct_bytes = direct.error == COMPILER_DRIVER_ERROR_NONE ? file_read(arena, direct_path, (FileReadOptions){0}) : (ByteSlice){0};
            ByteSlice via_bytes = via.error == COMPILER_DRIVER_ERROR_NONE ? file_read(arena, via_path, (FileReadOptions){0}) : (ByteSlice){0};
            BUSTER_TEST(arguments, direct_bytes.length && direct_bytes.length == via_bytes.length &&
                                       memcmp(direct_bytes.pointer, via_bytes.pointer, direct_bytes.length) == 0);
        }
        if (!linker.length)
        {
            arguments->show(arguments, S8("MACH_TLS_EXTERNAL status=unavailable reason=missing-ld64.lld target={S8}\n"), targets[target_index]);
        }
        else if (compiled_ok)
        {
            String8 output = buster_test_temporary_path(arena, S8("buster-mach-tls-ld64"), string_format(arena, S8("-{u32}"), target_index));
            String8 link_command[] = {linker, S8("-arch"), architectures[target_index], S8("-platform_version"), S8("macos"), S8("11.0"), S8("11.0"),
                                      object_path, stub, S8("-o"), output};
            ProcessSpawnOptions options = {.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                           .use_process_environment = true, .new_process_group = true};
            ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(link_command), (SliceString8){0}, (SliceString8){0}, options);
            ProcessWaitResult waited = spawned.handle ? os_process_wait_deadline(arena, spawned, UINT64_C(30000000)) : (ProcessWaitResult){0};
            bool linked = spawned.handle && waited.result == PROCESS_RESULT_SUCCESS;
            if (!linked && spawned.handle)
            {
                ByteSlice error = waited.streams[STANDARD_STREAM_ERROR];
                arguments->show(arguments, S8("MACH_TLS_EXTERNAL linker stderr={S8}\n"),
                                (String8){.pointer = (char8*)error.pointer, .length = BUSTER_MIN(error.length, 2000)});
            }
            arguments->show(arguments, S8("MACH_TLS_EXTERNAL status={S8} target={S8} linker={S8}\n"), linked ? S8("pass") : S8("fail"),
                            targets[target_index], linker);
            BUSTER_TEST(arguments, linked);
        }
    }
    scratch_end(temporary);
    return result;
}
#endif


#if !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_emms_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 source_path = buster_test_temporary_path(arena, S8("buster-emms"), S8(".c"));
    String8 object_path = buster_test_temporary_path(arena, S8("buster-emms"), S8(".o"));
    String8 enabled = S8(
        "#ifndef __MMX__\n#error missing MMX feature macro\n#endif\n"
        "#if !__has_builtin(__builtin_ia32_emms)\n#error missing EMMS builtin\n#endif\n"
        "void emms_probe(void) { __builtin_ia32_emms(); }\n");
    String8 disabled = S8(
        "#ifdef __MMX__\n#error MMX macro ignores disabled feature\n#endif\n"
        "#if __has_builtin(__builtin_ia32_emms)\n#error EMMS ignores disabled feature\n#endif\n"
        "int disabled_feature_control;\n");
    String8 targets[] = {S8("x86_64-linux"), S8("x86_64-windows"), S8("x86_64-macos")};
    String8 allocators[] = {S8("-fregister-allocator=fast"), S8("-fregister-allocator=quality")};
    String8 forms[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
    for (u32 target = 0; target < BUSTER_ARRAY_LENGTH(targets); target += 1)
    {
        for (u32 form = 0; form < BUSTER_ARRAY_LENGTH(forms); form += 1)
        {
            for (u32 allocator = 0; allocator < BUSTER_ARRAY_LENGTH(allocators); allocator += 1)
            {
                if (BUSTER_REQUIRE(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(enabled))))
                {
                    TemporalArena compile = arena_begin_temporal(arena);
                    String8 command[] = {S8("-std=c17"), S8("-nostdinc"), S8("-mcpu=baseline"), S8("-target"), targets[target],
                        forms[form], allocators[allocator], S8("-fno-machine-fallback"), S8("-fverify-codegen"), S8("-c"), S8("-o"), object_path, source_path};
                    CompilerDriverResult compiled = compiler_driver_execute_invocation(
                        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                    if (BUSTER_REQUIRE(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object))
                    {
                        u32 resets = 0;
                        for (u32 index = 0; index < compiled.object.symbol_count; index += 1)
                        {
                            ObjectSymbol symbol = compiled.object.symbols[index];
                            bool named = string_equal(symbol.name, S8("emms_probe")) || string_equal(symbol.name, S8("_emms_probe"));
                            if (named && symbol.section < compiled.object.section_count)
                            {
                                ByteSlice bytes = compiled.object.sections[symbol.section].data;
                                if (bytes.pointer && symbol.value <= bytes.length)
                                {
                                    u64 size = symbol.size ? symbol.size : bytes.length - symbol.value;
                                    if (size <= bytes.length - symbol.value)
                                    {
                                        for (u64 offset = 0; offset + 1 < size; offset += 1)
                                        {
                                            resets += bytes.pointer[symbol.value + offset] == 0x0f && bytes.pointer[symbol.value + offset + 1] == 0x77;
                                        }
                                    }
                                }
                            }
                        }
                        BUSTER_TEST(arguments, resets == 1 && compiled.fallback_record_count == 0);
                    }
                    else
                    {
                        compiler_driver_object_path_test_report(arguments, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command), compiled);
                    }
                    os_file_delete(object_path);
                    scratch_end(compile);
                }
            }
        }
    }
    String8 flags[] = {S8("-mno-mmx"), S8("-mmmx")};
    for (u32 feature = 0; feature < BUSTER_ARRAY_LENGTH(flags); feature += 1)
    {
        String8 source = feature ? enabled : disabled;
        if (BUSTER_REQUIRE(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(source))))
        {
            String8 command[] = {S8("-std=c17"), S8("-nostdinc"), S8("-target"), S8("x86_64-linux"), S8("-mcpu=baseline"),
                S8("-mno-mmx"), flags[feature], S8("-fsyntax-only"), source_path};
            BUSTER_TEST(arguments, compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)) == COMPILER_DRIVER_ERROR_NONE);
        }
    }
    String8 invalid[] = {
        S8("void f(void) { __builtin_ia32_emms(1); }"),
        S8("int f(void) { return __builtin_ia32_emms(); }"),
        S8("void f(void) { __builtin_ia32_emms(); }"),
        S8("void f(void) { __builtin_ia32_emms(); }"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1)
    {
        if (BUSTER_REQUIRE(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(invalid[index]))))
        {
            for (u32 form = 0; form < BUSTER_ARRAY_LENGTH(forms); form += 1)
            {
                TemporalArena compile = arena_begin_temporal(arena);
                String8 command[] = {S8("-std=c17"), S8("-nostdinc"), S8("-target"), index == 3 ? S8("aarch64-linux") : S8("x86_64-linux"),
                    index == 2 ? S8("-mno-mmx") : S8("-mcpu=baseline"), forms[form], S8("-c"), S8("-o"), object_path, source_path};
                CompilerDriverResult compiled = compiler_driver_execute_invocation(
                    arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                BUSTER_TEST(arguments, compiled.error != COMPILER_DRIVER_ERROR_NONE && compiled.diagnostic.length && !compiled.has_object);
                os_file_delete(object_path);
                scratch_end(compile);
            }
        }
    }
#if defined(BUSTER_HOST_C_RESOURCE_INCLUDE)
    String8 resource = S8(BUSTER_HOST_C_RESOURCE_INCLUDE);
    String8 header = string_format_z(arena, S8("{S8}/mmintrin.h"), resource);
    if (resource.length && compiler_driver_object_path_test_file_exists(header))
    {
        String8 source = S8("#include <mmintrin.h>\nvoid header_emms_probe(void) { _mm_empty(); }\n");
        if (BUSTER_REQUIRE(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(source))))
        {
            String8 command[] = {S8("-target"), S8("x86_64-linux"), S8("-mcpu=baseline"), S8("-std=c17"), S8("-isystem"), resource,
                S8("-fno-machine-fallback"), S8("-c"), S8("-o"), object_path, source_path};
            BUSTER_TEST(arguments, compiler_driver_object_path_test_compile(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)) == COMPILER_DRIVER_ERROR_NONE);
            os_file_delete(object_path);
        }
    }
#endif
#if BUSTER_CPU_ARCH_X86_64
    // FXSAVE's abridged tag byte is at offset four. MMX makes all eight tags
    // nonempty; EMMS makes them empty. Raw setup/observation bytes deliberately
    // use module-assembly ABI leaf helpers, independently of inline directive
    // support, literal-MMX-register parsing, and the new builtin.
    String8 program = S8(
        "#include <stdio.h>\n"
        "#if defined(__APPLE__)\n"
        "#define ORACLE_NAME(name) \"_\" name\n"
        "#else\n"
        "#define ORACLE_NAME(name) name\n"
        "#endif\n"
        "#if defined(_WIN32)\n"
        "#define ORACLE_SAVE_CW \"0xd9,0x39\"\n"
        "#define ORACLE_LOAD_CW \"0xd9,0x29\"\n"
        "#define ORACLE_SNAPSHOT \"0x0f,0xae,0x01\"\n"
        "#else\n"
        "#define ORACLE_SAVE_CW \"0xd9,0x3f\"\n"
        "#define ORACLE_LOAD_CW \"0xd9,0x2f\"\n"
        "#define ORACLE_SNAPSHOT \"0x0f,0xae,0x07\"\n"
        "#endif\n"
        "void emms_state_save_cw(void*);\n"
        "void emms_state_load_cw(void*);\n"
        "void emms_state_dirty(void*);\n"
        "void emms_state_snapshot(void*);\n"
        "void emms_state_cleanup(void);\n"
        "__asm__(\".text\\n.globl \" ORACLE_NAME(\"emms_state_save_cw\") \"\\n\" ORACLE_NAME(\"emms_state_save_cw\") \":\\n.byte \" ORACLE_SAVE_CW \",0xc3\\n\"\n"
        "        \".globl \" ORACLE_NAME(\"emms_state_load_cw\") \"\\n\" ORACLE_NAME(\"emms_state_load_cw\") \":\\n.byte \" ORACLE_LOAD_CW \",0xc3\\n\"\n"
        "        \".globl \" ORACLE_NAME(\"emms_state_dirty\") \"\\n\" ORACLE_NAME(\"emms_state_dirty\") \":\\n.byte 0x0f,0xef,0xc0\\n.byte \" ORACLE_SNAPSHOT \",0xc3\\n\"\n"
        "        \".globl \" ORACLE_NAME(\"emms_state_snapshot\") \"\\n\" ORACLE_NAME(\"emms_state_snapshot\") \":\\n.byte \" ORACLE_SNAPSHOT \",0xc3\\n\"\n"
        "        \".globl \" ORACLE_NAME(\"emms_state_cleanup\") \"\\n\" ORACLE_NAME(\"emms_state_cleanup\") \":\\n.byte 0x0f,0x77,0xc3\\n\");\n"
        "struct __attribute__((aligned(16))) State { unsigned char bytes[512]; };\n"
        "static volatile int selectors[2] = { 0, 1 };\n"
        "static volatile long double left = 1.25L, right = 2.0L;\n"
        "static void emit(unsigned int n) { static const char hex[] = \"0123456789abcdef\"; putchar(hex[(n >> 4) & 15]); putchar(hex[n & 15]); return; }\n"
        "static void probe(int reset) {\n"
        " struct State before, after;\n"
        " unsigned short original = 0x037f, requested = 0x0b7f;\n"
        " emms_state_save_cw(&original);\n"
        " emms_state_load_cw(&requested);\n"
        " before.bytes[0] = before.bytes[1] = before.bytes[4] = 66;\n"
        " after.bytes[0] = after.bytes[1] = after.bytes[4] = 66;\n"
        " emms_state_dirty(&before);\n"
        " if (reset) __builtin_ia32_emms();\n"
        " emms_state_snapshot(&after);\n"
        " int arithmetic = 0;\n"
        " if (reset && after.bytes[4] == 0) arithmetic = left + right == 3.25L;\n"
        " emms_state_cleanup();\n"
        " emms_state_load_cw(&original);\n"
        " emit(before.bytes[4]); emit(after.bytes[4]); emit(before.bytes[0]); emit(before.bytes[1]);\n"
        " emit(after.bytes[0]); emit(after.bytes[1]); emit((unsigned int)arithmetic); return;\n"
        "}\n"
        "int main(void) { probe(selectors[0]); probe(selectors[1]); return 0; }\n");
    for (u32 form = 0; form < BUSTER_ARRAY_LENGTH(forms); form += 1)
    {
        for (u32 allocator = 0; allocator < BUSTER_ARRAY_LENGTH(allocators); allocator += 1)
        {
            if (BUSTER_REQUIRE(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(program))))
            {
                String8 executable = buster_test_temporary_path(arena, S8("buster-emms-state"), BUSTER_WINDOWS ? S8(".exe") : S8(""));
                String8 command[] = {S8("-mcpu=baseline"), S8("-std=c17"), forms[form], allocators[allocator], S8("-fno-machine-fallback"),
                    S8("-fverify-codegen"), S8("-o"), executable, source_path};
                CompilerDriverResult compiled = compiler_driver_execute_invocation(
                    arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                if (compiled.error != COMPILER_DRIVER_ERROR_NONE)
                {
                    compiler_driver_object_path_test_report(arguments, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command), compiled);
                }
                if (BUSTER_REQUIRE(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE))
                {
                    String8 run[] = {executable};
                    ProcessSpawnResult child = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                        (ProcessSpawnOptions){.use_process_environment = true, .new_process_group = true,
                            .capture = (UINT64_C(1) << STANDARD_STREAM_OUTPUT) | (UINT64_C(1) << STANDARD_STREAM_ERROR)});
                    ProcessWaitResult waited = child.handle ? os_process_wait_deadline(arena, child, UINT64_C(30000000)) : (ProcessWaitResult){0};
                    ByteSlice output = waited.streams[STANDARD_STREAM_OUTPUT];
                    bool valid = child.handle && waited.result == PROCESS_RESULT_SUCCESS && !waited.timed_out && !waited.capture_failed &&
                        !waited.output_truncated && !waited.streams[STANDARD_STREAM_ERROR].length && output.pointer && output.length == 28;
                    u8 observed[14] = {0};
                    for (u32 byte = 0; valid && byte < BUSTER_ARRAY_LENGTH(observed); byte += 1)
                    {
                        for (u32 nibble = 0; valid && nibble < 2; nibble += 1)
                        {
                            u8 digit = output.pointer[byte * 2 + nibble];
                            bool decimal = digit >= '0' && digit <= '9';
                            bool lower = digit >= 'a' && digit <= 'f';
                            valid = decimal || lower;
                            observed[byte] = (u8)(observed[byte] * 16 + (decimal ? digit - '0' : lower ? digit - 'a' + 10 : 0));
                        }
                    }
                    valid = valid && observed[0] == 255 && observed[1] == 255 && observed[6] == 0 &&
                        observed[7] == 255 && observed[8] == 0 && observed[13] == 1 &&
                        observed[2] == 0x7f && observed[3] == 0x0b && observed[2] == observed[4] && observed[3] == observed[5] &&
                        observed[9] == 0x7f && observed[10] == 0x0b && observed[9] == observed[11] && observed[10] == observed[12];
                    BUSTER_TEST(arguments, valid);
                    if (!valid)
                    {
                        arguments->show(arguments, S8("EMMS state oracle failed: form={S8} allocator={S8} result={u32} platform_status={u32} stdout='{S8}' stderr='{S8}'\n"),
                            forms[form], allocators[allocator], (u32)waited.result, waited.platform_status,
                            BYTE_SLICE_TO_STRING(8, output), BYTE_SLICE_TO_STRING(8, waited.streams[STANDARD_STREAM_ERROR]));
                        for (u32 symbol_index = 0; symbol_index < compiled.object.symbol_count; symbol_index += 1)
                        {
                            ObjectSymbol symbol = compiled.object.symbols[symbol_index];
                            bool selected = string_equal(symbol.name, S8("probe")) || string_equal(symbol.name, S8("main")) ||
                                            string_equal(symbol.name, S8("emit"));
                            if (selected && symbol.section < compiled.object.section_count)
                            {
                                ByteSlice bytes = compiled.object.sections[symbol.section].data;
                                if (bytes.pointer && symbol.value <= bytes.length)
                                {
                                    u64 size = symbol.size ? symbol.size : bytes.length - symbol.value;
                                    size = BUSTER_MIN(size, BUSTER_MIN(bytes.length - symbol.value, UINT64_C(1024)));
                                    char8 hexadecimal[2048];
                                    char8 const digits[] = "0123456789abcdef";
                                    for (u64 byte = 0; byte < size; byte += 1)
                                    {
                                        u8 value = bytes.pointer[symbol.value + byte];
                                        hexadecimal[2 * byte] = digits[value >> 4];
                                        hexadecimal[2 * byte + 1] = digits[value & 15];
                                    }
                                    arguments->show(arguments, S8("EMMS_STATE_CODE symbol={S8} offset={u64} bytes={S8}\n"),
                                        symbol.name, symbol.value, (String8){.pointer = hexadecimal, .length = 2 * size});
                                }
                            }
                        }
                    }
                }
                os_file_delete(executable);
            }
        }
    }
#endif
    os_file_delete(source_path);
    return result;
}
#endif

UnitTestResult compiler_driver_object_path_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if !BUSTER_ANDROID && !BUSTER_IOS
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_emms_tests);
#endif
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_elf_stack_tests);
#endif
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_elf_empty_tests);
#endif
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_tls_export_tests);
#endif
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_aarch64_printer_roundtrip);
#if !BUSTER_ANDROID && !BUSTER_IOS
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_android_preinit_tags);
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_mach_tls_object_tests);
#endif
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_elf_semantic_tests);
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_elf_preinit_tests);
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_elf_ifunc_tests);
#if BUSTER_CPU_ARCH_X86_64
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_x86_elf_modifier_roundtrip);
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_elf_cpu_runtime_tests);
#endif
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_elf_aarch64_cpu_runtime_selection_tests);
#if BUSTER_CPU_ARCH_AARCH64
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_elf_aarch64_cpu_runtime_tests);
#endif
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_elf_weak_got_tests);
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
        "extern int constant_dead_plain(void);\n"
        "extern int constant_dead_else(void);\n"
        "extern int constant_dead_short_rhs(void);\n"
        "extern int constant_dead_comma(void);\n"
        "extern int constant_dead_or_rhs(void);\n"
        "extern int constant_dead_goto_prefix(void);\n"
        "extern int constant_dead_case_prefix(void);\n"
        "extern int constant_dead_data;\n"
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
        "    int count = 0;\n"
        "    if (0) count += constant_dead_plain() + constant_dead_data;\n"
        "    if (1) count += 1; else count += constant_dead_else();\n"
        "    if (0 && constant_dead_short_rhs()) count += constant_dead_data;\n"
        "    if ((initializer_calls += 1, 0)) count += constant_dead_comma();\n"
        "    if (1 || constant_dead_or_rhs()) count += 2; else count += constant_dead_data;\n"
        "    goto entered;\n"
        "    if (0) { count += constant_dead_goto_prefix(); entered: count += 4; }\n"
        "    switch (3) { case 1: if (0) { count += constant_dead_case_prefix(); case 3: count += 8; } }\n"
        "    int condition_calls = initializer_calls;\n"
        "    initializer_calls = 0;\n"
        "    goto live;\n"
        "    int target = (initializer_calls += 1, 5);\n"
        "live:\n"
        "    target = 7;\n"
        "    goto dead;\n"
        "dead:\n"
        "    return count != 15 || condition_calls != 1 || initializer_calls != 0 || target != 7 || read_external() != 9;\n"
        "}\n");
    BUSTER_TEST(arguments, file_write(unreachable_source, BUSTER_SLICE_TO_BYTE_SLICE(unreachable_program)));
    String8 allocators[] = {S8("fast"), S8("quality")};
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
            command[command_count++] = S8("-fno-machine-fallback");
            command[command_count++] = S8("-fverify-codegen");
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
