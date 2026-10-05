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
                S8("unsupported ELF symbol semantic_absolute (section index 65521)")},
            {S8("weak-absolute"), S8("extern const unsigned long semantic_absolute_address;\n"
                "__asm__(\".weak semantic_absolute\\n.set semantic_absolute,0x1234\\n"
                ".pushsection .data\\n.balign 8\\n.globl semantic_absolute_address\\nsemantic_absolute_address:\\n.quad semantic_absolute\\n.popsection\");\n"
                "int main(void) { return semantic_absolute_address != 0x1234; }\n"),
                S8("unsupported ELF symbol semantic_absolute (section index 65521)")},
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
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_aarch64_printer_roundtrip);
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
