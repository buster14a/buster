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

UnitTestResult compiler_driver_object_path_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_tls_export_tests);
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
