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
