#include <buster/tests/compiler/object/object_test.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#if BUSTER_INCLUDE_TESTS

BUSTER_GLOBAL_LOCAL bool object_bytes_contain(ByteSlice bytes, String8 value)
{
    if (value.length <= bytes.length)
    {
        for (u64 index = 0; index + value.length <= bytes.length; index += 1)
        {
            if (memcmp(bytes.pointer + index, value.pointer, value.length) == 0)
            {
                return true;
            }
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL bool object_test_mach_compact_section_rewrite(ByteSlice bytes)
{
    u64 section = 32 + 72 + (u64)OBJECT_SECTION_READ_ONLY_DATA * 80;
    u32 magic = 0;
    u32 command = 0;
    if (bytes.length >= 36)
    {
        memcpy(&magic, bytes.pointer, sizeof(magic));
        memcpy(&command, bytes.pointer + 32, sizeof(command));
    }
    bool result;
    if (bytes.length < section + 80 || magic != 0xfeedfacf || command != 0x19)
    {
        result = false;
    }
    else
    {
        memset(bytes.pointer + section, 0, 32);
        memcpy(bytes.pointer + section, "__compact_unwind", sizeof("__compact_unwind") - 1);
        memcpy(bytes.pointer + section + 16, "__LD", sizeof("__LD") - 1);
        u32 flags = 0x020000;
        memcpy(bytes.pointer + section + 64, &flags, sizeof(flags));
        result = true;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void object_test_write_u16(ByteSlice bytes, u64 offset, u16 value)
{
    if (offset <= bytes.length && sizeof(value) <= bytes.length - offset)
    {
        memcpy(bytes.pointer + offset, &value, sizeof(value));
    }
}

BUSTER_GLOBAL_LOCAL void object_test_write_u32(ByteSlice bytes, u64 offset, u32 value)
{
    if (offset <= bytes.length && sizeof(value) <= bytes.length - offset)
    {
        memcpy(bytes.pointer + offset, &value, sizeof(value));
    }
}

BUSTER_GLOBAL_LOCAL void object_test_write_u64(ByteSlice bytes, u64 offset, u64 value)
{
    if (offset <= bytes.length && sizeof(value) <= bytes.length - offset)
    {
        memcpy(bytes.pointer + offset, &value, sizeof(value));
    }
}

BUSTER_GLOBAL_LOCAL u64 object_test_elf_symbol_offset(ByteSlice bytes, u32 symbol_index)
{
    if (bytes.pointer && bytes.length >= 64)
    {
        u64 section_table = 0;
        u16 section_count = 0;
        memcpy(&section_table, bytes.pointer + 40, sizeof(section_table));
        memcpy(&section_count, bytes.pointer + 60, sizeof(section_count));
        if (section_table <= bytes.length && (u64)section_count * 64 <= bytes.length - section_table)
        {
            for (u16 section_index = 0; section_index < section_count; section_index += 1)
            {
                u64 section = section_table + (u64)section_index * 64;
                u32 section_type = 0;
                u64 symbol_offset = 0;
                u64 symbol_size = 0;
                memcpy(&section_type, bytes.pointer + section + 4, sizeof(section_type));
                memcpy(&symbol_offset, bytes.pointer + section + 24, sizeof(symbol_offset));
                memcpy(&symbol_size, bytes.pointer + section + 32, sizeof(symbol_size));
                if (section_type != 2 || symbol_size % 24 || symbol_index >= symbol_size / 24 || symbol_offset > bytes.length ||
                    symbol_size > bytes.length - symbol_offset)
                {
                    continue;
                }
                u64 symbol = symbol_offset + (u64)symbol_index * 24;
                if (symbol > bytes.length || 24 > bytes.length - symbol)
                {
                    return UINT64_MAX;
                }
                return symbol;
            }
        }
    }

    return UINT64_MAX;
}

BUSTER_GLOBAL_LOCAL bool object_test_elf_relocation_offsets(ByteSlice bytes, u64* relocation_section, u64* target_data)
{
    if (bytes.pointer && relocation_section && target_data && bytes.length >= 64)
    {
        u64 section_table = 0;
        u16 section_count = 0;
        memcpy(&section_table, bytes.pointer + 40, sizeof(section_table));
        memcpy(&section_count, bytes.pointer + 60, sizeof(section_count));
        if (section_table <= bytes.length && (u64)section_count * 64 <= bytes.length - section_table)
        {
            for (u16 section_index = 0; section_index < section_count; section_index += 1)
            {
                u64 section = section_table + (u64)section_index * 64;
                u32 section_type = 0;
                u32 target_index = 0;
                memcpy(&section_type, bytes.pointer + section + 4, sizeof(section_type));
                memcpy(&target_index, bytes.pointer + section + 44, sizeof(target_index));
                if (section_type != 4 || target_index >= section_count)
                {
                    continue;
                }
                u64 target_section = section_table + (u64)target_index * 64;
                memcpy(target_data, bytes.pointer + target_section + 24, sizeof(*target_data));
                if (*target_data > bytes.length)
                {
                    return false;
                }
                *relocation_section = section;
                return true;
            }
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL bool object_test_mach_text_offsets(ByteSlice bytes, u32* raw_offset, u32* relocation_offset, u32* relocation_count)
{
    u64 section = 32 + 72;
    u32 magic = 0;
    u32 command = 0;
    if (!bytes.pointer || !raw_offset || !relocation_offset || !relocation_count || bytes.length < section + 80)
    {
        return false;
    }
    memcpy(&magic, bytes.pointer, sizeof(magic));
    memcpy(&command, bytes.pointer + 32, sizeof(command));
    if (magic != UINT32_C(0xfeedfacf) || command != 0x19)
    {
        return false;
    }
    memcpy(raw_offset, bytes.pointer + section + 48, sizeof(*raw_offset));
    memcpy(relocation_offset, bytes.pointer + section + 56, sizeof(*relocation_offset));
    memcpy(relocation_count, bytes.pointer + section + 60, sizeof(*relocation_count));
    return *raw_offset <= bytes.length && *relocation_offset <= bytes.length && (u64)*relocation_count * 8 <= bytes.length - *relocation_offset;
}

BUSTER_GLOBAL_LOCAL bool object_test_mach_section_offsets(ByteSlice bytes, u32 section_index, u32* raw_offset, u32* relocation_offset,
                                                          u32* relocation_count)
{
    u64 section = 32 + 72 + (u64)section_index * 80;
    if (!bytes.pointer || !raw_offset || !relocation_offset || !relocation_count || bytes.length < section + 80)
    {
        return false;
    }
    u32 magic = 0;
    u32 command = 0;
    u32 section_count = 0;
    memcpy(&magic, bytes.pointer, sizeof(magic));
    memcpy(&command, bytes.pointer + 32, sizeof(command));
    memcpy(&section_count, bytes.pointer + 32 + 64, sizeof(section_count));
    if (magic != UINT32_C(0xfeedfacf) || command != 0x19 || section_index >= section_count)
    {
        return false;
    }
    memcpy(raw_offset, bytes.pointer + section + 48, sizeof(*raw_offset));
    memcpy(relocation_offset, bytes.pointer + section + 56, sizeof(*relocation_offset));
    memcpy(relocation_count, bytes.pointer + section + 60, sizeof(*relocation_count));
    return *raw_offset <= bytes.length && *relocation_offset <= bytes.length && (u64)*relocation_count * 8 <= bytes.length - *relocation_offset;
}

BUSTER_GLOBAL_LOCAL u64 object_test_mach_symbol_offset(ByteSlice bytes, u32 symbol_index)
{
    if (bytes.pointer && bytes.length >= 32)
    {
        u32 command_count = 0;
        u32 commands_size = 0;
        memcpy(&command_count, bytes.pointer + 16, sizeof(command_count));
        memcpy(&commands_size, bytes.pointer + 20, sizeof(commands_size));
        u64 command = 32;
        u64 command_end = command + commands_size;
        if (command_end >= command && command_end <= bytes.length)
        {
            for (u32 command_index = 0; command_index < command_count && command < command_end; command_index += 1)
            {
                u32 kind = 0;
                u32 size = 0;
                if (command_end - command < 8)
                {
                    return UINT64_MAX;
                }
                memcpy(&kind, bytes.pointer + command, sizeof(kind));
                memcpy(&size, bytes.pointer + command + 4, sizeof(size));
                if (size < 8 || size > command_end - command)
                {
                    return UINT64_MAX;
                }
                if (kind == 2 && size >= 24)
                {
                    u32 symbol_offset = 0;
                    u32 symbol_count = 0;
                    memcpy(&symbol_offset, bytes.pointer + command + 8, sizeof(symbol_offset));
                    memcpy(&symbol_count, bytes.pointer + command + 12, sizeof(symbol_count));
                    u64 record = symbol_offset + (u64)symbol_index * 16;
                    if (symbol_index < symbol_count && record >= symbol_offset && record <= bytes.length && 16 <= bytes.length - record)
                    {
                        return record;
                    }
                    return UINT64_MAX;
                }
                command += size;
            }
        }
    }

    return UINT64_MAX;
}

BUSTER_GLOBAL_LOCAL ByteSlice object_test_archive(Arena* arena, ByteSlice member, bool bsd_name)
{
    String8 name = S8("member-name.o");
    u64 member_size = member.length + (bsd_name ? name.length : 0);
    u64 total_size = 8 + 60 + member_size + (member_size & 1);
    u8* bytes = arena_allocate(arena, u8, total_size);
    memset(bytes, ' ', total_size);
    memcpy(bytes, "!<arch>\n", 8);
    u8* header = bytes + 8;
    if (bsd_name)
    {
        memcpy(header, "#1/13", 5);
    }
    else
    {
        memcpy(header, "member.o/", 9);
    }
    u64 decimal = member_size;
    for (u32 index = 0; index < 10; index += 1)
    {
        header[57 - index] = (u8)('0' + decimal % 10);
        decimal /= 10;
    }
    header[58] = '`';
    header[59] = '\n';
    u8* payload = header + 60;
    if (bsd_name)
    {
        memcpy(payload, name.pointer, name.length);
        payload += name.length;
    }
    memcpy(payload, member.pointer, member.length);
    if (member_size & 1)
    {
        bytes[68 + member_size] = '\n';
    }
    return (ByteSlice){.pointer = bytes, .length = total_size};
}

BUSTER_GLOBAL_LOCAL void object_test_archive_write_size(u8* header, u64 size)
{
    for (u32 index = 0; index < 10; index += 1)
    {
        header[57 - index] = (u8)('0' + size % 10);
        size /= 10;
    }
}

BUSTER_GLOBAL_LOCAL ByteSlice object_test_archive_long_name(Arena* arena, ByteSlice member)
{
    String8 long_name = S8("long-member-name.o/\n");
    u64 table_size = long_name.length;
    u64 object_size = member.length;
    u64 total_size = 8 + 60 + table_size + (table_size & 1) + 60 + object_size + (object_size & 1);
    u8* bytes = arena_allocate(arena, u8, total_size);
    memset(bytes, ' ', total_size);
    memcpy(bytes, "!<arch>\n", 8);

    u8* table_header = bytes + 8;
    memcpy(table_header, "//", 2);
    object_test_archive_write_size(table_header, table_size);
    table_header[58] = '`';
    table_header[59] = '\n';
    memcpy(table_header + 60, long_name.pointer, long_name.length);
    if (table_size & 1)
    {
        table_header[60 + table_size] = '\n';
    }
    u64 object_header_offset = 8 + 60 + table_size + (table_size & 1);
    u8* object_header = bytes + object_header_offset;
    memcpy(object_header, "/0", 2);
    object_test_archive_write_size(object_header, object_size);
    object_header[58] = '`';
    object_header[59] = '\n';
    memcpy(object_header + 60, member.pointer, member.length);
    if (object_size & 1)
    {
        object_header[60 + object_size] = '\n';
    }
    return (ByteSlice){.pointer = bytes, .length = total_size};
}


// A COFF object built by hand, because COMDAT is the one input shape
// object_write cannot produce: the writer merges sections by kind, so it has
// no way to give a constant its own IMAGE_SCN_LNK_COMDAT section.  Four
// sections cover the reader's three answers — a plain section, a selectany
// COMDAT, a NODUPLICATES COMDAT, and a COMDAT whose section symbol is missing
// entirely so its selection is never published.
enum
{
    OBJECT_TEST_COFF_HEADER_SIZE = 20,
    OBJECT_TEST_COFF_SECTION_SIZE = 40,
    OBJECT_TEST_COFF_SYMBOL_SIZE = 18,
    OBJECT_TEST_COFF_SECTION_COUNT = 4,
    OBJECT_TEST_COFF_SYMBOL_COUNT = 8,
    OBJECT_TEST_COFF_SECTION_DATA = 4,
};

BUSTER_GLOBAL_LOCAL void object_test_coff_write_u16(u8* bytes, u64 offset, u16 value)
{
    memcpy(bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void object_test_coff_write_u32(u8* bytes, u64 offset, u32 value)
{
    memcpy(bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void object_test_coff_write_name(u8* bytes, u64 offset, String8 name)
{
    memcpy(bytes + offset, name.pointer, name.length);
}

BUSTER_GLOBAL_LOCAL void object_test_coff_section(u8* bytes, u32 section_index, String8 name, u32 raw_offset, u32 characteristics)
{
    u64 section = OBJECT_TEST_COFF_HEADER_SIZE + (u64)section_index * OBJECT_TEST_COFF_SECTION_SIZE;
    object_test_coff_write_name(bytes, section, name);
    object_test_coff_write_u32(bytes, section + 16, OBJECT_TEST_COFF_SECTION_DATA);
    object_test_coff_write_u32(bytes, section + 20, raw_offset);
    object_test_coff_write_u32(bytes, section + 36, characteristics);
}

BUSTER_GLOBAL_LOCAL void object_test_coff_symbol(u8* bytes, u64 symbol_table, u32 symbol_index, String8 name, s16 section_number, u8 storage,
                                                 u8 auxiliary_count)
{
    u64 symbol = symbol_table + (u64)symbol_index * OBJECT_TEST_COFF_SYMBOL_SIZE;
    object_test_coff_write_name(bytes, symbol, name);
    object_test_coff_write_u16(bytes, symbol + 12, (u16)section_number);
    bytes[symbol + 16] = storage;
    bytes[symbol + 17] = auxiliary_count;
}

// Auxiliary Format 5: the section definition whose Selection byte is the whole
// point of the COMDAT model.
BUSTER_GLOBAL_LOCAL void object_test_coff_section_definition(u8* bytes, u64 symbol_table, u32 symbol_index, u8 selection)
{
    u64 auxiliary = symbol_table + (u64)symbol_index * OBJECT_TEST_COFF_SYMBOL_SIZE;
    object_test_coff_write_u32(bytes, auxiliary, OBJECT_TEST_COFF_SECTION_DATA);
    bytes[auxiliary + 14] = selection;
}

BUSTER_GLOBAL_LOCAL ByteSlice object_test_coff_comdat_object(Arena* arena)
{
    u64 section_data = OBJECT_TEST_COFF_HEADER_SIZE + (u64)OBJECT_TEST_COFF_SECTION_COUNT * OBJECT_TEST_COFF_SECTION_SIZE;
    u64 symbol_table = section_data + (u64)OBJECT_TEST_COFF_SECTION_COUNT * OBJECT_TEST_COFF_SECTION_DATA;
    u64 string_table = symbol_table + (u64)OBJECT_TEST_COFF_SYMBOL_COUNT * OBJECT_TEST_COFF_SYMBOL_SIZE;
    u64 length = string_table + 4;
    u8* bytes = arena_allocate(arena, u8, length);
    memset(bytes, 0, length);
    object_test_coff_write_u16(bytes, 0, 0x8664);
    object_test_coff_write_u16(bytes, 2, OBJECT_TEST_COFF_SECTION_COUNT);
    object_test_coff_write_u32(bytes, 8, (u32)symbol_table);
    object_test_coff_write_u32(bytes, 12, OBJECT_TEST_COFF_SYMBOL_COUNT);
    // IMAGE_SCN_ALIGN_4BYTES throughout; CNT_CODE/EXECUTE/READ for .text and
    // CNT_INITIALIZED_DATA/READ plus IMAGE_SCN_LNK_COMDAT for the constants.
    u32 text_characteristics = 0x60300020;
    u32 comdat_characteristics = 0x40301040;
    object_test_coff_section(bytes, 0, S8(".text"), (u32)section_data, text_characteristics);
    object_test_coff_section(bytes, 1, S8(".rdata"), (u32)section_data + 4, comdat_characteristics);
    object_test_coff_section(bytes, 2, S8(".rdata"), (u32)section_data + 8, comdat_characteristics);
    object_test_coff_section(bytes, 3, S8(".rdata"), (u32)section_data + 12, comdat_characteristics);
    object_test_coff_symbol(bytes, symbol_table, 0, S8("plain"), 1, 2, 0);
    object_test_coff_symbol(bytes, symbol_table, 1, S8(".rdata"), 2, 3, 1);
    object_test_coff_section_definition(bytes, symbol_table, 2, 2);
    object_test_coff_symbol(bytes, symbol_table, 3, S8("any_one"), 2, 2, 0);
    object_test_coff_symbol(bytes, symbol_table, 4, S8(".rdata"), 3, 3, 1);
    object_test_coff_section_definition(bytes, symbol_table, 5, 1);
    object_test_coff_symbol(bytes, symbol_table, 6, S8("strict1"), 3, 2, 0);
    object_test_coff_symbol(bytes, symbol_table, 7, S8("pending"), 4, 2, 0);
    object_test_coff_write_u32(bytes, string_table, 4);

    return (ByteSlice){.pointer = bytes, .length = length};
}

UnitTestResult object_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST(arguments, sizeof(CodegenModuleRelocation) == 32);
    BUSTER_TEST(arguments, BUSTER_ALIGN_OF(CodegenModuleRelocation) == 8);
    BUSTER_TEST(arguments, BUSTER_OFFSET_OF(CodegenModuleRelocation, kind) == 27);
    BUSTER_TEST(arguments, CODEGEN_MODULE_RELOCATION_COUNT <= UINT8_MAX);
    typedef struct CodegenRelocationKindExpectation CodegenRelocationKindExpectation;
    struct CodegenRelocationKindExpectation
    {
        CodegenModuleRelocationKind kind;
        bool aarch64;
        bool absolute;
        bool is_thread_local;
        bool thread_local_low;
        bool thread_local_index;
    };
    CodegenRelocationKindExpectation relocation_kinds[] = {
        {CODEGEN_MODULE_RELOCATION_X86_64_PC32, false, false, false, false, false},
        {CODEGEN_MODULE_RELOCATION_AARCH64_CALL26, true, false, false, false, false},
        {CODEGEN_MODULE_RELOCATION_ABSOLUTE32, false, true, false, false, false},
        {CODEGEN_MODULE_RELOCATION_ABSOLUTE64, false, true, false, false, false},
        {CODEGEN_MODULE_RELOCATION_X86_64_TPOFF32, false, false, true, false, false},
        {CODEGEN_MODULE_RELOCATION_X86_64_PE_TLS_INDEX_PC32, false, false, true, false, true},
        {CODEGEN_MODULE_RELOCATION_PE_TLS_OFFSET32, false, false, true, false, false},
        {CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP, true, false, true, false, true},
        {CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_LO12, true, false, true, true, true},
        {CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_OFFSET12, true, false, true, false, false},
        {CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12, true, false, true, false, false},
        {CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12, true, false, true, true, false},
        {CODEGEN_MODULE_RELOCATION_X86_64_MACH_TLV_PC32, false, false, true, false, false},
        {CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGE21, true, false, true, false, false},
        {CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12, true, false, true, true, false},
        {CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGE21, true, false, false, false, false},
        {CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGEOFF12, true, false, false, false, false},
    };
    BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(relocation_kinds) == CODEGEN_MODULE_RELOCATION_COUNT);
    for (u32 kind_index = 0; kind_index < BUSTER_ARRAY_LENGTH(relocation_kinds); kind_index += 1)
    {
        CodegenRelocationKindExpectation expectation = relocation_kinds[kind_index];
        CodegenModuleRelocation relocation = {
            .kind = (u8)expectation.kind,
            .aarch64 = expectation.aarch64,
            .absolute = expectation.absolute,
            .is_thread_local = expectation.is_thread_local,
            .thread_local_low = expectation.thread_local_low,
            .thread_local_index = expectation.thread_local_index,
        };
        BUSTER_TEST(arguments, codegen_module_relocation_kind_valid(relocation.kind));
        BUSTER_TEST(arguments, codegen_module_relocation_valid(&relocation));
        relocation.kind = CODEGEN_MODULE_RELOCATION_COUNT;
        BUSTER_TEST(arguments, !codegen_module_relocation_kind_valid(relocation.kind));
        BUSTER_TEST(arguments, !codegen_module_relocation_valid(&relocation));
        relocation.kind = (u8)expectation.kind;
        relocation.aarch64 = !relocation.aarch64;
        BUSTER_TEST(arguments, !codegen_module_relocation_valid(&relocation));
    }
    BUSTER_TEST(arguments, !codegen_module_relocation_kind_valid(UINT8_MAX));

    u8 x86_text[] = {
        0xe8, 0, 0, 0, 0, 0xc3, 0xb8, 42, 0, 0, 0, 0xc3,
    };
    u8 aarch64_text[] = {
        0xfd, 0x7b, 0xbf, 0xa9, 0x00, 0x00, 0x00, 0x94, 0xfd, 0x7b, 0xc1, 0xa8, 0xc0, 0x03, 0x5f, 0xd6, 0x40, 0x05, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6,
    };
    u8 read_only_data[] = {
        'h', 'e', 'l', 'l', 'o', 0,
    };
    u8 writable_data[8] = {0};
    ObjectSection sections[] = {
        {
            .name = S8(".text"),
            .data = BUSTER_ARRAY_TO_SLICE(x86_text),
            .kind = OBJECT_SECTION_TEXT,
            .alignment = 16,
        },
        {
            .name = S8(".rodata"),
            .data = BUSTER_ARRAY_TO_SLICE(read_only_data),
            .kind = OBJECT_SECTION_READ_ONLY_DATA,
            .alignment = 1,
        },
        {
            .name = S8(".data"),
            .data = BUSTER_ARRAY_TO_SLICE(writable_data),
            .kind = OBJECT_SECTION_DATA,
            .alignment = 8,
        },
        {
            .name = S8(".bss"),
            .virtual_size = BUSTER_MB(1),
            .kind = OBJECT_SECTION_ZERO,
            .alignment = 64,
        },
    };
    ObjectSymbol symbols[] = {
        {
            .name = S8("object_entry"),
            .size = 6,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("object_callee"),
            .value = 6,
            .size = 6,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("object_message"),
            .size = sizeof(read_only_data),
            .section = OBJECT_SECTION_READ_ONLY_DATA,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
        {
            .name = S8("external_function"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("object_zero"),
            .size = BUSTER_MB(1),
            .section = OBJECT_SECTION_ZERO,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
    };
    ObjectRelocation relocation = {
        .addend = -4,
        .offset = 1,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    ObjectFile object = {
        .sections = sections,
        .symbols = symbols,
        .relocations = &relocation,
        .target =
            {
                .cpu_arch = CPU_ARCH_X86_64,
                .os = OPERATING_SYSTEM_LINUX,
            },
        .section_count = BUSTER_ARRAY_LENGTH(sections),
        .symbol_count = BUSTER_ARRAY_LENGTH(symbols),
        .relocation_count = 1,
    };
    u8 x86_accumulator_extend[] = {0x66, 0x98, 0x98, 0x48, 0x98};
    ObjectSection accumulator_extend_section = {
        .name = S8(".text"),
        .data = BUSTER_ARRAY_TO_SLICE(x86_accumulator_extend),
        .kind = OBJECT_SECTION_TEXT,
        .alignment = 1,
    };
    ObjectFile accumulator_extend_object = {
        .sections = &accumulator_extend_section,
        .target = object.target,
        .section_count = 1,
    };
    String8 accumulator_extend_assembly = object_print_assembly(arguments->arena, &accumulator_extend_object);
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(accumulator_extend_assembly), S8("\tcbw\n\tcwde\n\tcdqe\n")));
    u8 x86_mmx[] = {0x0f, 0x6f, 0xc1, 0x0f, 0xef, 0xc2, 0x0f, 0xfc, 0xc3, 0x0f, 0xfe, 0xc4};
    ObjectSection mmx_section = {
        .name = S8(".text"),
        .data = BUSTER_ARRAY_TO_SLICE(x86_mmx),
        .kind = OBJECT_SECTION_TEXT,
        .alignment = 1,
    };
    ObjectFile mmx_object = {
        .sections = &mmx_section,
        .target = object.target,
        .section_count = 1,
    };
    String8 mmx_assembly = object_print_assembly(arguments->arena, &mmx_object);
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(mmx_assembly),
                                                S8("\tmovq mm0, mm1\n\tpxor mm0, mm2\n\tpaddb mm0, mm3\n\tpaddd mm0, mm4\n")));
    ObjectFormat formats[] = {
        OBJECT_FORMAT_ELF64,
        OBJECT_FORMAT_COFF,
        OBJECT_FORMAT_MACH_O64,
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(formats); index += 1)
    {
        ObjectArtifact artifact = object_write(arguments->arena, &object, formats[index]);
        BUSTER_TEST(arguments, artifact.error == OBJECT_ERROR_NONE);
        BUSTER_TEST(arguments, artifact.bytes.length > 64);
        BUSTER_TEST(arguments, artifact.bytes.length < BUSTER_MB(1));
        BUSTER_TEST(arguments, object_bytes_contain(artifact.bytes, S8("object_entry")));
        BUSTER_TEST(arguments, object_bytes_contain(artifact.bytes, S8("external_function")));
        BUSTER_TEST(arguments, object_bytes_contain(artifact.bytes, S8("hello")));
    }
    ObjectRelocation section_relocations[] = {
        relocation,
        {
            .addend = 2,
            .offset = 0,
            .section = OBJECT_SECTION_DATA,
            .symbol = 2,
            .kind = OBJECT_RELOCATION_ABSOLUTE64,
        },
    };
    ObjectFile section_relocation_object = object;
    section_relocation_object.relocations = section_relocations;
    section_relocation_object.relocation_count = BUSTER_ARRAY_LENGTH(section_relocations);
    ObjectArtifact section_relocation_elf = object_write(arguments->arena, &section_relocation_object, OBJECT_FORMAT_ELF64);
    BUSTER_TEST(arguments, section_relocation_elf.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, object_bytes_contain(section_relocation_elf.bytes, S8(".rela.data")));
    ObjectArtifact elf = object_write(arguments->arena, &object, OBJECT_FORMAT_ELF64);
    bool elf_magic_matches = false;
    if (elf.bytes.pointer && elf.bytes.length >= 4)
    {
        elf_magic_matches = elf.bytes.pointer[0] == 0x7f && elf.bytes.pointer[1] == 'E' && elf.bytes.pointer[2] == 'L' && elf.bytes.pointer[3] == 'F';
    }
    BUSTER_TEST(arguments, elf_magic_matches);
    ObjectFile elf_roundtrip = object_read(arguments->arena, elf.bytes, object.target);
    BUSTER_TEST(arguments, elf_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, elf_roundtrip.section_count == OBJECT_SECTION_COUNT);
    BUSTER_TEST(arguments, elf_roundtrip.symbol_count == object.symbol_count);
    BUSTER_TEST(arguments, elf_roundtrip.relocation_count == object.relocation_count);
    bool elf_sections_valid =
        elf_roundtrip.sections && elf_roundtrip.section_count > OBJECT_SECTION_TEXT && elf_roundtrip.sections[OBJECT_SECTION_TEXT].data.pointer;
    BUSTER_TEST(arguments, elf_sections_valid);
    if (elf_sections_valid)
    {
        BUSTER_TEST(arguments, elf_roundtrip.sections[OBJECT_SECTION_TEXT].data.length == sizeof(x86_text));
        BUSTER_TEST(arguments, memcmp(elf_roundtrip.sections[OBJECT_SECTION_TEXT].data.pointer, x86_text, sizeof(x86_text)) == 0);
    }
    bool elf_zero_section_valid = elf_roundtrip.sections && elf_roundtrip.section_count > OBJECT_SECTION_ZERO;
    BUSTER_TEST(arguments, elf_zero_section_valid);
    if (elf_zero_section_valid)
    {
        BUSTER_TEST(arguments, elf_roundtrip.sections[OBJECT_SECTION_ZERO].data.length == 0);
        BUSTER_TEST(arguments, elf_roundtrip.sections[OBJECT_SECTION_ZERO].virtual_size == BUSTER_MB(1));
    }
    bool elf_relocation_valid = elf_roundtrip.relocations && elf_roundtrip.relocation_count && elf_roundtrip.symbols &&
                                elf_roundtrip.relocations[0].symbol < elf_roundtrip.symbol_count;
    BUSTER_TEST(arguments, elf_relocation_valid);
    if (elf_relocation_valid)
    {
        BUSTER_TEST(arguments, elf_roundtrip.relocations[0].kind == OBJECT_RELOCATION_X86_64_PC32);
        BUSTER_TEST(arguments, elf_roundtrip.relocations[0].addend == -4);
        BUSTER_STRING_TEST(arguments, elf_roundtrip.symbols[elf_roundtrip.relocations[0].symbol].name, S8("object_callee"));
    }
    ObjectArtifact coff = object_write(arguments->arena, &object, OBJECT_FORMAT_COFF);
    ObjectFile coff_roundtrip = object_read(arguments->arena, coff.bytes,
                                            (Target){
                                                .cpu_arch = CPU_ARCH_X86_64,
                                                .os = OPERATING_SYSTEM_WINDOWS,
                                            });
    BUSTER_TEST(arguments, coff_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, coff_roundtrip.symbol_count == object.symbol_count);
    BUSTER_TEST(arguments, coff_roundtrip.relocation_count == object.relocation_count);
    BUSTER_TEST(arguments, coff_roundtrip.sections[OBJECT_SECTION_ZERO].data.length == 0);
    BUSTER_TEST(arguments, coff_roundtrip.sections[OBJECT_SECTION_ZERO].virtual_size == BUSTER_MB(1));
    bool coff_relocation_valid = coff_roundtrip.relocations && coff_roundtrip.relocation_count && coff_roundtrip.symbols &&
                                 coff_roundtrip.relocations[0].symbol < coff_roundtrip.symbol_count;
    BUSTER_TEST(arguments, coff_relocation_valid);
    if (coff_relocation_valid)
    {
        BUSTER_TEST(arguments, coff_roundtrip.relocations[0].kind == OBJECT_RELOCATION_X86_64_PC32);
        BUSTER_TEST(arguments, coff_roundtrip.relocations[0].addend == -4);
        BUSTER_STRING_TEST(arguments, coff_roundtrip.symbols[coff_roundtrip.relocations[0].symbol].name, S8("object_callee"));
    }
    {
        TemporalArena comdat_scope = arena_begin_temporal(arguments->arena);
        ByteSlice comdat_bytes = object_test_coff_comdat_object(arguments->arena);
        ObjectFile comdat = object_read(arguments->arena, comdat_bytes,
                                        (Target){
                                            .cpu_arch = CPU_ARCH_X86_64,
                                            .os = OPERATING_SYSTEM_WINDOWS,
                                        });
        BUSTER_TEST(arguments, comdat.error == OBJECT_ERROR_NONE);
        // Six symbols: the two auxiliary records carry no symbol of their own.
        BUSTER_TEST(arguments, comdat.symbol_count == 6);
        if (comdat.error == OBJECT_ERROR_NONE && comdat.symbol_count == 6)
        {
            BUSTER_STRING_TEST(arguments, comdat.symbols[0].name, S8("plain"));
            BUSTER_TEST(arguments, comdat.symbols[0].section == OBJECT_SECTION_TEXT && comdat.symbols[0].global && !comdat.symbols[0].weak);
            BUSTER_STRING_TEST(arguments, comdat.symbols[2].name, S8("any_one"));
            BUSTER_TEST(arguments, comdat.symbols[2].section == OBJECT_SECTION_READ_ONLY_DATA && comdat.symbols[2].global && comdat.symbols[2].weak);
            // The section symbol shares its section's selection, but stays
            // local, so it never reaches the linker's global arbitration.
            BUSTER_TEST(arguments, !comdat.symbols[1].global && comdat.symbols[1].weak);
            BUSTER_STRING_TEST(arguments, comdat.symbols[4].name, S8("strict1"));
            BUSTER_TEST(arguments, comdat.symbols[4].global && !comdat.symbols[4].weak);
            BUSTER_TEST(arguments, !comdat.symbols[3].weak);
            // IMAGE_SCN_LNK_COMDAT with no section symbol to name a selection
            // stays a hard definition rather than a silently dropped one.
            BUSTER_STRING_TEST(arguments, comdat.symbols[5].name, S8("pending"));
            BUSTER_TEST(arguments, comdat.symbols[5].global && !comdat.symbols[5].weak);
        }
        arena_set_position(arguments->arena, comdat_scope.position);
    }
    {
        // ELF64 and Mach-O carry the replaceable bit on the wire; COFF cannot,
        // because expressing it needs a section per COMDAT and the writer
        // merges sections by kind.
        TemporalArena weak_scope = arena_begin_temporal(arguments->arena);
        ObjectSymbol weak_symbols[] = {
            {.name = S8("object_weak"), .section = OBJECT_SECTION_TEXT, .kind = OBJECT_SYMBOL_FUNCTION, .global = true, .weak = true},
            {.name = S8("object_strong"), .section = OBJECT_SECTION_TEXT, .kind = OBJECT_SYMBOL_FUNCTION, .global = true},
        };
        ObjectFile weak_object = object;
        weak_object.symbols = weak_symbols;
        weak_object.symbol_count = BUSTER_ARRAY_LENGTH(weak_symbols);
        weak_object.relocations = 0;
        weak_object.relocation_count = 0;
        ObjectFormat weak_formats[] = {OBJECT_FORMAT_ELF64, OBJECT_FORMAT_MACH_O64};
        OperatingSystem weak_systems[] = {OPERATING_SYSTEM_LINUX, OPERATING_SYSTEM_MACOS};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(weak_formats); index += 1)
        {
            ObjectArtifact weak_artifact = object_write(arguments->arena, &weak_object, weak_formats[index]);
            BUSTER_TEST(arguments, weak_artifact.error == OBJECT_ERROR_NONE);
            ObjectFile weak_roundtrip = object_read(arguments->arena, weak_artifact.bytes,
                                                    (Target){
                                                        .cpu_arch = CPU_ARCH_X86_64,
                                                        .os = weak_systems[index],
                                                    });
            BUSTER_TEST(arguments, weak_roundtrip.error == OBJECT_ERROR_NONE && weak_roundtrip.symbol_count == BUSTER_ARRAY_LENGTH(weak_symbols));
            if (weak_roundtrip.error == OBJECT_ERROR_NONE && weak_roundtrip.symbol_count == BUSTER_ARRAY_LENGTH(weak_symbols))
            {
                bool weak_found = false;
                bool strong_found = false;
                for (u32 symbol = 0; symbol < weak_roundtrip.symbol_count; symbol += 1)
                {
                    ObjectSymbol* read = weak_roundtrip.symbols + symbol;
                    weak_found = weak_found || (string_equal(read->name, S8("object_weak")) && read->global && read->weak);
                    strong_found = strong_found || (string_equal(read->name, S8("object_strong")) && read->global && !read->weak);
                }
                BUSTER_TEST(arguments, weak_found);
                BUSTER_TEST(arguments, strong_found);
            }
        }
        arena_set_position(arguments->arena, weak_scope.position);
    }
    {
        u8 debug_data[] = {0};
        ObjectSection long_coff_sections[OBJECT_SECTION_COUNT] = {0};
        for (u32 section_index = 0; section_index < OBJECT_SECTION_COUNT; section_index += 1)
        {
            long_coff_sections[section_index] = (ObjectSection){
                .name = object_section_name_for_kind((ObjectSectionKind)section_index),
                .kind = (ObjectSectionKind)section_index,
                .alignment = object_section_default_alignment((ObjectSectionKind)section_index),
            };
        }
        for (u32 section_index = 0; section_index < BUSTER_ARRAY_LENGTH(sections); section_index += 1)
        {
            long_coff_sections[section_index] = sections[section_index];
        }
        long_coff_sections[OBJECT_SECTION_DEBUG_INFO].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(debug_data);
        long_coff_sections[OBJECT_SECTION_DEBUG_INFO].virtual_size = sizeof(debug_data);
        ObjectFile long_coff_object = object;
        long_coff_object.sections = long_coff_sections;
        long_coff_object.section_count = OBJECT_SECTION_COUNT;
        ObjectArtifact long_coff = object_write(arguments->arena, &long_coff_object, OBJECT_FORMAT_COFF);
        BUSTER_TEST(arguments, long_coff.error == OBJECT_ERROR_NONE);
        ObjectFile long_coff_roundtrip = object_read(arguments->arena, long_coff.bytes,
                                                     (Target){
                                                         .cpu_arch = CPU_ARCH_X86_64,
                                                         .os = OPERATING_SYSTEM_WINDOWS,
                                                     });
        BUSTER_TEST(arguments, long_coff_roundtrip.error == OBJECT_ERROR_NONE);
        BUSTER_TEST(arguments, long_coff_roundtrip.sections[OBJECT_SECTION_DEBUG_INFO].data.length == sizeof(debug_data));
        if (long_coff.bytes.pointer && long_coff.bytes.length > 0)
        {
            u64 section = 20 + (u64)OBJECT_SECTION_DEBUG_INFO * 40;
            TemporalArena bad_offset_scope = arena_begin_temporal(arguments->arena);
            ByteSlice bad_offset = {
                .pointer = arena_allocate(arguments->arena, u8, long_coff.bytes.length),
                .length = long_coff.bytes.length,
            };
            memcpy(bad_offset.pointer, long_coff.bytes.pointer, long_coff.bytes.length);
            memset(bad_offset.pointer + section, 0, 8);
            memcpy(bad_offset.pointer + section, "/999999", 7);
            BUSTER_TEST(arguments, object_read(arguments->arena, bad_offset,
                                               (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS})
                                             .error != OBJECT_ERROR_NONE);
            arena_set_position(arguments->arena, bad_offset_scope.position);

            TemporalArena unterminated_scope = arena_begin_temporal(arguments->arena);
            ByteSlice unterminated = {
                .pointer = arena_allocate(arguments->arena, u8, long_coff.bytes.length),
                .length = long_coff.bytes.length,
            };
            memcpy(unterminated.pointer, long_coff.bytes.pointer, long_coff.bytes.length);
            unterminated.pointer[unterminated.length - 1] = 'x';
            BUSTER_TEST(arguments, object_read(arguments->arena, unterminated,
                                               (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS})
                                             .error != OBJECT_ERROR_NONE);
            arena_set_position(arguments->arena, unterminated_scope.position);
        }
    }
    u16 coff_machine = 0;
    if (coff.bytes.pointer && coff.bytes.length >= 2)
    {
        memcpy(&coff_machine, coff.bytes.pointer, 2);
    }
    BUSTER_TEST(arguments, coff_machine == 0x8664);
    ObjectArtifact mach = object_write(arguments->arena, &object, OBJECT_FORMAT_MACH_O64);
    ObjectFile mach_roundtrip = object_read(arguments->arena, mach.bytes,
                                            (Target){
                                                .cpu_arch = CPU_ARCH_X86_64,
                                                .os = OPERATING_SYSTEM_MACOS,
                                            });
    BUSTER_TEST(arguments, mach_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, mach_roundtrip.symbol_count == object.symbol_count);
    BUSTER_TEST(arguments, mach_roundtrip.relocation_count == object.relocation_count);
    BUSTER_TEST(arguments, mach_roundtrip.sections[OBJECT_SECTION_ZERO].data.length == 0);
    BUSTER_TEST(arguments, mach_roundtrip.sections[OBJECT_SECTION_ZERO].virtual_size == BUSTER_MB(1));
    bool mach_relocation_valid = mach_roundtrip.relocations && mach_roundtrip.relocation_count && mach_roundtrip.symbols &&
                                 mach_roundtrip.relocations[0].symbol < mach_roundtrip.symbol_count;
    BUSTER_TEST(arguments, mach_relocation_valid);
    if (mach_relocation_valid)
    {
        BUSTER_TEST(arguments, mach_roundtrip.relocations[0].kind == OBJECT_RELOCATION_X86_64_PC32);
        BUSTER_TEST(arguments, mach_roundtrip.relocations[0].addend == -4);
        BUSTER_STRING_TEST(arguments, mach_roundtrip.symbols[mach_roundtrip.relocations[0].symbol].name, S8("object_callee"));
    }
    u32 mach_magic = 0;
    if (mach.bytes.pointer && mach.bytes.length >= 4)
    {
        memcpy(&mach_magic, mach.bytes.pointer, 4);
    }
    BUSTER_TEST(arguments, mach_magic == 0xfeedfacf);
    sections[0].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(aarch64_text);
    object.target.cpu_arch = CPU_ARCH_AARCH64;
    relocation = (ObjectRelocation){
        .offset = 4,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_AARCH64_CALL26,
    };
    symbols[0].size = 16;
    symbols[1].value = 16;
    symbols[1].size = 8;
    ObjectArtifact aarch64_elf = object_write(arguments->arena, &object, OBJECT_FORMAT_ELF64);
    BUSTER_TEST(arguments, aarch64_elf.error == OBJECT_ERROR_NONE);
    u16 aarch64_elf_machine = 0;
    if (aarch64_elf.bytes.pointer && aarch64_elf.bytes.length >= 20)
    {
        memcpy(&aarch64_elf_machine, aarch64_elf.bytes.pointer + 18, 2);
    }
    BUSTER_TEST(arguments, aarch64_elf_machine == 183);
    ObjectArtifact aarch64_coff = object_write(arguments->arena, &object, OBJECT_FORMAT_COFF);
    BUSTER_TEST(arguments, aarch64_coff.error == OBJECT_ERROR_NONE);
    u16 aarch64_coff_machine = 0;
    if (aarch64_coff.bytes.pointer && aarch64_coff.bytes.length >= 2)
    {
        memcpy(&aarch64_coff_machine, aarch64_coff.bytes.pointer, 2);
    }
    BUSTER_TEST(arguments, aarch64_coff_machine == 0xaa64);
    ObjectArtifact aarch64_mach = object_write(arguments->arena, &object, OBJECT_FORMAT_MACH_O64);
    BUSTER_TEST(arguments, aarch64_mach.error == OBJECT_ERROR_NONE);
    u32 aarch64_mach_cpu = 0;
    if (aarch64_mach.bytes.pointer && aarch64_mach.bytes.length >= 8)
    {
        memcpy(&aarch64_mach_cpu, aarch64_mach.bytes.pointer + 4, 4);
    }
    BUSTER_TEST(arguments, aarch64_mach_cpu == 0x0100000c);
    u16 aarch64_mach_n_desc[5] = {0};
    bool aarch64_mach_n_desc_valid = true;
    for (u32 symbol_index = 0; symbol_index < BUSTER_ARRAY_LENGTH(aarch64_mach_n_desc); symbol_index += 1)
    {
        u64 symbol_offset = object_test_mach_symbol_offset(aarch64_mach.bytes, symbol_index);
        aarch64_mach_n_desc_valid &= symbol_offset != UINT64_MAX &&
                                     (u64)symbol_offset + 8 <= aarch64_mach.bytes.length;
        if (symbol_offset != UINT64_MAX && (u64)symbol_offset + 8 <= aarch64_mach.bytes.length)
        {
            memcpy(&aarch64_mach_n_desc[symbol_index], aarch64_mach.bytes.pointer + symbol_offset + 6, sizeof(u16));
        }
    }
    BUSTER_TEST(arguments, aarch64_mach_n_desc_valid && aarch64_mach_n_desc[0] == 2 && aarch64_mach_n_desc[1] == 2 &&
                               aarch64_mach_n_desc[2] == 2 && aarch64_mach_n_desc[3] == 0 && aarch64_mach_n_desc[4] == 2);
    u32 aarch64_jump_instruction = UINT32_C(0x14000000);
    memcpy(aarch64_text + 4, &aarch64_jump_instruction, sizeof(aarch64_jump_instruction));
    relocation.kind = OBJECT_RELOCATION_AARCH64_JUMP26;
    ObjectFormat aarch64_jump_formats[] = {OBJECT_FORMAT_ELF64, OBJECT_FORMAT_COFF, OBJECT_FORMAT_MACH_O64};
    OperatingSystem aarch64_jump_systems[] = {OPERATING_SYSTEM_LINUX, OPERATING_SYSTEM_WINDOWS, OPERATING_SYSTEM_MACOS};
    for (u32 format_index = 0; format_index < BUSTER_ARRAY_LENGTH(aarch64_jump_formats); format_index += 1)
    {
        ObjectArtifact jump_artifact = object_write(arguments->arena, &object, aarch64_jump_formats[format_index]);
        ObjectFile jump_roundtrip = object_read(arguments->arena, jump_artifact.bytes,
                                                (Target){
                                                    .cpu_arch = CPU_ARCH_AARCH64,
                                                    .os = aarch64_jump_systems[format_index],
                                                });
        BUSTER_TEST(arguments, jump_artifact.error == OBJECT_ERROR_NONE && jump_roundtrip.error == OBJECT_ERROR_NONE);
        BUSTER_TEST(arguments, jump_roundtrip.relocation_count == 1 &&
                                   jump_roundtrip.relocations[0].kind == OBJECT_RELOCATION_AARCH64_JUMP26 &&
                                   jump_roundtrip.relocations[0].addend == 0);
    }
    s64 mach_branch_addends[] = {-4, 4, -INT64_C(0x800000), INT64_C(0x7fffff)};
    for (u32 addend_index = 0; addend_index < BUSTER_ARRAY_LENGTH(mach_branch_addends); addend_index += 1)
    {
        relocation.addend = mach_branch_addends[addend_index];
        ObjectArtifact addend_mach = object_write(arguments->arena, &object, OBJECT_FORMAT_MACH_O64);
        u32 raw_offset = 0;
        u32 relocation_offset = 0;
        u32 relocation_count = 0;
        bool offsets_valid = object_test_mach_text_offsets(addend_mach.bytes, &raw_offset, &relocation_offset, &relocation_count);
        BUSTER_TEST(arguments, addend_mach.error == OBJECT_ERROR_NONE && offsets_valid && relocation_count == 2);
        u32 stored_instruction = 0;
        if (offsets_valid && (u64)raw_offset + relocation.offset <= addend_mach.bytes.length &&
            sizeof(stored_instruction) <= addend_mach.bytes.length - ((u64)raw_offset + relocation.offset))
        {
            memcpy(&stored_instruction, addend_mach.bytes.pointer + raw_offset + relocation.offset, sizeof(stored_instruction));
        }
        BUSTER_TEST(arguments, stored_instruction == UINT32_C(0x14000000));
        ObjectFile addend_roundtrip = object_read(arguments->arena, addend_mach.bytes,
                                                  (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS});
        BUSTER_TEST(arguments, addend_roundtrip.error == OBJECT_ERROR_NONE && addend_roundtrip.relocation_count == 1 &&
                                   addend_roundtrip.relocations[0].kind == OBJECT_RELOCATION_AARCH64_JUMP26 &&
                                   addend_roundtrip.relocations[0].addend == mach_branch_addends[addend_index]);
    }
    s64 invalid_mach_addends[] = {-INT64_C(0x800001), INT64_C(0x800000)};
    for (u32 addend_index = 0; addend_index < BUSTER_ARRAY_LENGTH(invalid_mach_addends); addend_index += 1)
    {
        relocation.addend = invalid_mach_addends[addend_index];
        BUSTER_TEST(arguments, object_write(arguments->arena, &object, OBJECT_FORMAT_MACH_O64).error == OBJECT_ERROR_UNSUPPORTED_TARGET);
    }
    s64 unsupported_coff_addends[] = {-4, 4};
    for (u32 addend_index = 0; addend_index < BUSTER_ARRAY_LENGTH(unsupported_coff_addends); addend_index += 1)
    {
        relocation.addend = unsupported_coff_addends[addend_index];
        BUSTER_TEST(arguments, object_write(arguments->arena, &object, OBJECT_FORMAT_COFF).error == OBJECT_ERROR_UNSUPPORTED_TARGET);
    }
    relocation.addend = 0;

    u32 aarch64_branch_opcodes[] = {UINT32_C(0x14000000), UINT32_C(0x94000000)};
    ObjectRelocationKind aarch64_branch_kinds[] = {OBJECT_RELOCATION_AARCH64_JUMP26, OBJECT_RELOCATION_AARCH64_CALL26};
    s64 elf_rel_addends[] = {4, -4};
    for (u32 opcode_index = 0; opcode_index < BUSTER_ARRAY_LENGTH(aarch64_branch_opcodes); opcode_index += 1)
    {
        relocation.kind = aarch64_branch_kinds[opcode_index];
        memcpy(aarch64_text + 4, aarch64_branch_opcodes + opcode_index, sizeof(u32));
        for (u32 addend_index = 0; addend_index < BUSTER_ARRAY_LENGTH(elf_rel_addends); addend_index += 1)
        {
            relocation.addend = elf_rel_addends[addend_index];
            ObjectArtifact rela_artifact = object_write(arguments->arena, &object, OBJECT_FORMAT_ELF64);
            u64 rela_section = 0;
            u64 rela_target_data = 0;
            bool rela_offsets_valid = object_test_elf_relocation_offsets(rela_artifact.bytes, &rela_section, &rela_target_data);
            if (rela_offsets_valid)
            {
                u32 ignored_immediate = 0;
                bool ignored_encoded = a64_signed_scaled_immediate_encode(-elf_rel_addends[addend_index], 26, 2, &ignored_immediate);
                object_test_write_u32(rela_artifact.bytes, rela_target_data + relocation.offset,
                                      aarch64_branch_opcodes[opcode_index] | ignored_immediate);
                BUSTER_TEST(arguments, ignored_encoded);
            }
            ObjectFile rela_roundtrip = object_read(arguments->arena, rela_artifact.bytes,
                                                    (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX});
            BUSTER_TEST(arguments, rela_artifact.error == OBJECT_ERROR_NONE && rela_offsets_valid && rela_roundtrip.error == OBJECT_ERROR_NONE &&
                                       rela_roundtrip.relocation_count == 1 && rela_roundtrip.relocations[0].kind == aarch64_branch_kinds[opcode_index] &&
                                       rela_roundtrip.relocations[0].addend == elf_rel_addends[addend_index]);
            ObjectArtifact rela_rewritten = object_write(arguments->arena, &rela_roundtrip, OBJECT_FORMAT_ELF64);
            BUSTER_TEST(arguments, rela_rewritten.error == OBJECT_ERROR_NONE);

            relocation.addend = 0;
            ObjectArtifact rel_artifact = object_write(arguments->arena, &object, OBJECT_FORMAT_ELF64);
            u64 relocation_section = 0;
            u64 target_data = 0;
            bool offsets_valid = object_test_elf_relocation_offsets(rel_artifact.bytes, &relocation_section, &target_data);
            BUSTER_TEST(arguments, rel_artifact.error == OBJECT_ERROR_NONE && offsets_valid);
            if (offsets_valid)
            {
                u32 encoded_immediate = 0;
                bool encoded = a64_signed_scaled_immediate_encode(elf_rel_addends[addend_index], 26, 2, &encoded_immediate);
                u32 rel_instruction = aarch64_branch_opcodes[opcode_index] | encoded_immediate;
                object_test_write_u32(rel_artifact.bytes, target_data + relocation.offset, rel_instruction);
                object_test_write_u32(rel_artifact.bytes, relocation_section + 4, 9);
                object_test_write_u64(rel_artifact.bytes, relocation_section + 32, 16);
                object_test_write_u64(rel_artifact.bytes, relocation_section + 56, 16);
                BUSTER_TEST(arguments, encoded);
            }
            ObjectFile rel_roundtrip = object_read(arguments->arena, rel_artifact.bytes,
                                                   (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX});
            BUSTER_TEST(arguments, rel_roundtrip.error == OBJECT_ERROR_NONE && rel_roundtrip.relocation_count == 1 &&
                                       rel_roundtrip.relocations[0].kind == aarch64_branch_kinds[opcode_index] &&
                                       rel_roundtrip.relocations[0].addend == elf_rel_addends[addend_index]);
            ObjectArtifact rel_rewritten = object_write(arguments->arena, &rel_roundtrip, OBJECT_FORMAT_ELF64);
            ObjectFile rel_rewritten_roundtrip = object_read(arguments->arena, rel_rewritten.bytes,
                                                             (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX});
            BUSTER_TEST(arguments, rel_rewritten.error == OBJECT_ERROR_NONE && rel_rewritten_roundtrip.error == OBJECT_ERROR_NONE &&
                                       rel_rewritten_roundtrip.relocation_count == 1 &&
                                       rel_rewritten_roundtrip.relocations[0].kind == aarch64_branch_kinds[opcode_index] &&
                                       rel_rewritten_roundtrip.relocations[0].addend == elf_rel_addends[addend_index]);
        }
    }

    relocation.kind = OBJECT_RELOCATION_AARCH64_JUMP26;
    relocation.addend = 0;
    aarch64_jump_instruction = UINT32_C(0x14000000);
    memcpy(aarch64_text + 4, &aarch64_jump_instruction, sizeof(aarch64_jump_instruction));
    ObjectArtifact valid_mach_branch = object_write(arguments->arena, &object, OBJECT_FORMAT_MACH_O64);
    u32 mach_raw_offset = 0;
    u32 mach_relocation_offset = 0;
    u32 mach_relocation_count = 0;
    bool mach_offsets_valid = object_test_mach_text_offsets(valid_mach_branch.bytes, &mach_raw_offset, &mach_relocation_offset, &mach_relocation_count);
    BUSTER_TEST(arguments, valid_mach_branch.error == OBJECT_ERROR_NONE && mach_offsets_valid && mach_relocation_count == 1);
    if (mach_offsets_valid)
    {
        u32 relocation_information = 0;
        memcpy(&relocation_information, valid_mach_branch.bytes.pointer + mach_relocation_offset + 4, sizeof(relocation_information));
        object_test_write_u32(valid_mach_branch.bytes, mach_relocation_offset + 4, relocation_information & ~(1u << 24));
    }
    BUSTER_TEST(arguments, object_read(arguments->arena, valid_mach_branch.bytes,
                                       (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS})
                                 .error == OBJECT_ERROR_UNSUPPORTED_TARGET);
    valid_mach_branch = object_write(arguments->arena, &object, OBJECT_FORMAT_MACH_O64);
    mach_offsets_valid = object_test_mach_text_offsets(valid_mach_branch.bytes, &mach_raw_offset, &mach_relocation_offset, &mach_relocation_count);
    if (mach_offsets_valid)
    {
        u32 relocation_information = 0;
        memcpy(&relocation_information, valid_mach_branch.bytes.pointer + mach_relocation_offset + 4, sizeof(relocation_information));
        object_test_write_u32(valid_mach_branch.bytes, mach_relocation_offset + 4, relocation_information & ~(1u << 27));
    }
    BUSTER_TEST(arguments, mach_offsets_valid &&
                               object_read(arguments->arena, valid_mach_branch.bytes,
                                           (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS})
                                       .error == OBJECT_ERROR_UNSUPPORTED_TARGET);
    valid_mach_branch = object_write(arguments->arena, &object, OBJECT_FORMAT_MACH_O64);
    mach_offsets_valid = object_test_mach_text_offsets(valid_mach_branch.bytes, &mach_raw_offset, &mach_relocation_offset, &mach_relocation_count);
    if (mach_offsets_valid)
    {
        object_test_write_u32(valid_mach_branch.bytes, (u64)mach_raw_offset + relocation.offset, UINT32_C(0x14000001));
    }
    BUSTER_TEST(arguments, mach_offsets_valid &&
                               object_read(arguments->arena, valid_mach_branch.bytes,
                                           (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS})
                                       .error == OBJECT_ERROR_UNSUPPORTED_TARGET);
    relocation.addend = 4;
    ObjectArtifact malformed_addend_pair = object_write(arguments->arena, &object, OBJECT_FORMAT_MACH_O64);
    mach_offsets_valid = object_test_mach_text_offsets(malformed_addend_pair.bytes, &mach_raw_offset, &mach_relocation_offset, &mach_relocation_count);
    if (mach_offsets_valid && mach_relocation_count == 2)
    {
        u32 addend_information = 0;
        memcpy(&addend_information, malformed_addend_pair.bytes.pointer + mach_relocation_offset + 4, sizeof(addend_information));
        object_test_write_u32(malformed_addend_pair.bytes, mach_relocation_offset + 4, addend_information | (1u << 27));
    }
    BUSTER_TEST(arguments, mach_offsets_valid && mach_relocation_count == 2 &&
                               object_read(arguments->arena, malformed_addend_pair.bytes,
                                           (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS})
                                       .error == OBJECT_ERROR_UNSUPPORTED_TARGET);
    relocation.addend = 0;

    u8 direct_page_words[] = {0x09, 0x00, 0x00, 0x90, 0x29, 0x01, 0x00, 0x91};
    ObjectSection direct_page_sections[] = {
        {
            .name = S8(".text"),
            .data = BUSTER_ARRAY_TO_SLICE(direct_page_words),
            .kind = OBJECT_SECTION_TEXT,
            .alignment = 4,
        },
    };
    ObjectSymbol direct_page_symbols[] = {
        {
            .name = S8("direct_page_target"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation direct_page_relocations[] = {
        {
            .offset = 0,
            .section = OBJECT_SECTION_TEXT,
            .symbol = 0,
            .kind = OBJECT_RELOCATION_AARCH64_MACH_PAGE21,
        },
        {
            .offset = sizeof(u32),
            .section = OBJECT_SECTION_TEXT,
            .symbol = 0,
            .kind = OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12,
        },
    };
    ObjectFile direct_page_object = {
        .sections = direct_page_sections,
        .section_count = BUSTER_ARRAY_LENGTH(direct_page_sections),
        .symbols = direct_page_symbols,
        .symbol_count = BUSTER_ARRAY_LENGTH(direct_page_symbols),
        .relocations = direct_page_relocations,
        .relocation_count = BUSTER_ARRAY_LENGTH(direct_page_relocations),
        .target = {
            .cpu_arch = CPU_ARCH_AARCH64,
            .os = OPERATING_SYSTEM_MACOS,
        },
    };
    String8 direct_page_assembly = object_print_assembly(arguments->arena, &direct_page_object);
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(direct_page_assembly), S8("\tadrp x9, _direct_page_target@PAGE\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(direct_page_assembly), S8("\tadd x9, x9, _direct_page_target@PAGEOFF\n")));
    u32 saved_direct_page_instruction = 0;
    memcpy(&saved_direct_page_instruction, direct_page_words, sizeof(saved_direct_page_instruction));
    u32 page21_xzr_instruction = UINT32_C(0x9000001f);
    memcpy(direct_page_words, &page21_xzr_instruction, sizeof(page21_xzr_instruction));
    String8 page21_xzr_assembly = object_print_assembly(arguments->arena, &direct_page_object);
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(page21_xzr_assembly), S8("\tadrp xzr, _direct_page_target@PAGE\n")));
    BUSTER_TEST(arguments, object_write(arguments->arena, &direct_page_object, OBJECT_FORMAT_MACH_O64).error == OBJECT_ERROR_NONE);
    memcpy(direct_page_words, &saved_direct_page_instruction, sizeof(saved_direct_page_instruction));
    u32 malformed_page21_instruction = UINT32_C(0x14000000);
    memcpy(direct_page_words, &malformed_page21_instruction, sizeof(malformed_page21_instruction));
    BUSTER_TEST(arguments, object_print_assembly(arguments->arena, &direct_page_object).length == 0);
    memcpy(direct_page_words, &saved_direct_page_instruction, sizeof(saved_direct_page_instruction));
    ObjectArtifact direct_page_mach = object_write(arguments->arena, &direct_page_object, OBJECT_FORMAT_MACH_O64);
    u32 direct_page_raw_offset = 0;
    u32 direct_page_relocation_offset = 0;
    u32 direct_page_relocation_count = 0;
    bool direct_page_offsets_valid = object_test_mach_text_offsets(direct_page_mach.bytes, &direct_page_raw_offset,
                                                                    &direct_page_relocation_offset, &direct_page_relocation_count);
    BUSTER_TEST(arguments, direct_page_mach.error == OBJECT_ERROR_NONE && direct_page_offsets_valid && direct_page_relocation_count == 2);
    if (direct_page_offsets_valid && direct_page_relocation_count == 2)
    {
        u32 high_information = 0;
        u32 low_information = 0;
        memcpy(&high_information, direct_page_mach.bytes.pointer + direct_page_relocation_offset + 4, sizeof(high_information));
        memcpy(&low_information, direct_page_mach.bytes.pointer + direct_page_relocation_offset + 8 + 4, sizeof(low_information));
        BUSTER_TEST(arguments, high_information >> 28 == 3 && low_information >> 28 == 4);
        BUSTER_TEST(arguments, (high_information & (1u << 24)) != 0 && (low_information & (1u << 24)) == 0);
        BUSTER_TEST(arguments, ((high_information >> 25) & 3) == 2 && ((low_information >> 25) & 3) == 2);
        BUSTER_TEST(arguments, (high_information & (1u << 27)) != 0 && (low_information & (1u << 27)) != 0);
        BUSTER_TEST(arguments, direct_page_mach.bytes.pointer[direct_page_relocation_offset] == 0);
        BUSTER_TEST(arguments, direct_page_mach.bytes.pointer[direct_page_relocation_offset + 8] == sizeof(u32));
    }
    ObjectFile direct_page_roundtrip = object_read(arguments->arena, direct_page_mach.bytes,
                                                   (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS});
    BUSTER_TEST(arguments, direct_page_roundtrip.error == OBJECT_ERROR_NONE && direct_page_roundtrip.relocation_count == 2);
    if (direct_page_roundtrip.error == OBJECT_ERROR_NONE && direct_page_roundtrip.relocation_count == 2)
    {
        BUSTER_TEST(arguments, direct_page_roundtrip.relocations[0].kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21 &&
                                   direct_page_roundtrip.relocations[1].kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12 &&
                                   direct_page_roundtrip.symbol_count >= 1 &&
                                   direct_page_roundtrip.symbols[0].kind == OBJECT_SYMBOL_DATA);
        BUSTER_TEST(arguments, direct_page_roundtrip.relocations[0].offset == 0 && direct_page_roundtrip.relocations[1].offset == sizeof(u32));
    }
    u32 tlvp_words[] = {UINT32_C(0x90000009), UINT32_C(0xf9400129)};
    ObjectSection tlvp_section = {
        .name = S8(".text"),
        .data = {.pointer = (u8*)tlvp_words, .length = sizeof(tlvp_words)},
        .kind = OBJECT_SECTION_TEXT,
        .alignment = 4,
    };
    ObjectSymbol tlvp_symbol = {
        .name = S8("tls_value"),
        .section = OBJECT_SECTION_UNDEFINED,
        .kind = OBJECT_SYMBOL_DATA,
        .global = true,
    };
    ObjectRelocation tlvp_relocations[] = {
        {
            .addend = 0x1000,
            .section = OBJECT_SECTION_TEXT,
            .symbol = 0,
            .kind = OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21,
        },
        {
            .addend = 8,
            .offset = sizeof(u32),
            .section = OBJECT_SECTION_TEXT,
            .symbol = 0,
            .kind = OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12,
        },
    };
    ObjectFile tlvp_object = {
        .sections = &tlvp_section,
        .symbols = &tlvp_symbol,
        .relocations = tlvp_relocations,
        .target = {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS},
        .section_count = 1,
        .symbol_count = 1,
        .relocation_count = BUSTER_ARRAY_LENGTH(tlvp_relocations),
    };
    ObjectArtifact tlvp_mach = object_write(arguments->arena, &tlvp_object, OBJECT_FORMAT_MACH_O64);
    ObjectFile tlvp_roundtrip = object_read(arguments->arena, tlvp_mach.bytes,
                                            (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS});
    BUSTER_TEST(arguments, tlvp_mach.error == OBJECT_ERROR_NONE && tlvp_roundtrip.error == OBJECT_ERROR_NONE &&
                               tlvp_roundtrip.relocation_count == BUSTER_ARRAY_LENGTH(tlvp_relocations));
    if (tlvp_roundtrip.error == OBJECT_ERROR_NONE && tlvp_roundtrip.relocation_count == BUSTER_ARRAY_LENGTH(tlvp_relocations))
    {
        BUSTER_TEST(arguments, tlvp_roundtrip.relocations[0].kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21 &&
                                   tlvp_roundtrip.relocations[1].kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12 &&
                                   tlvp_roundtrip.relocations[0].addend == 0x1000 && tlvp_roundtrip.relocations[1].addend == 8);
    }
    tlvp_words[1] = UINT32_C(0x91000029);
    BUSTER_TEST(arguments, object_write(arguments->arena, &tlvp_object, OBJECT_FORMAT_MACH_O64).error != OBJECT_ERROR_NONE);
    tlvp_words[1] = UINT32_C(0xf9400129);
    u64 direct_page_symbol_offset = object_test_mach_symbol_offset(direct_page_mach.bytes, 0);
    u16 direct_page_n_desc = 0;
    if (direct_page_symbol_offset != UINT64_MAX && direct_page_symbol_offset + 8 <= direct_page_mach.bytes.length)
    {
        memcpy(&direct_page_n_desc, direct_page_mach.bytes.pointer + direct_page_symbol_offset + 6, sizeof(direct_page_n_desc));
    }
    BUSTER_TEST(arguments, direct_page_n_desc == 0);
    ObjectArtifact lazy_reference_kind_one = object_write(arguments->arena, &direct_page_object, OBJECT_FORMAT_MACH_O64);
    u64 lazy_reference_symbol = object_test_mach_symbol_offset(lazy_reference_kind_one.bytes, 0);
    ObjectFile lazy_reference_kind_one_roundtrip = {0};
    if (lazy_reference_symbol != UINT64_MAX && lazy_reference_symbol + 8 <= lazy_reference_kind_one.bytes.length)
    {
        object_test_write_u16(lazy_reference_kind_one.bytes, lazy_reference_symbol + 6, 1);
        lazy_reference_kind_one_roundtrip = object_read(arguments->arena, lazy_reference_kind_one.bytes,
                                                         (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS});
    }
    BUSTER_TEST(arguments, lazy_reference_kind_one_roundtrip.error == OBJECT_ERROR_NONE &&
                               lazy_reference_kind_one_roundtrip.symbol_count >= 1 &&
                               lazy_reference_kind_one_roundtrip.symbols[0].kind == OBJECT_SYMBOL_FUNCTION);
    ObjectArtifact lazy_reference_kind_five = object_write(arguments->arena, &direct_page_object, OBJECT_FORMAT_MACH_O64);
    lazy_reference_symbol = object_test_mach_symbol_offset(lazy_reference_kind_five.bytes, 0);
    ObjectFile lazy_reference_kind_five_roundtrip = {0};
    if (lazy_reference_symbol != UINT64_MAX && lazy_reference_symbol + 8 <= lazy_reference_kind_five.bytes.length)
    {
        object_test_write_u16(lazy_reference_kind_five.bytes, lazy_reference_symbol + 6, 5);
        lazy_reference_kind_five_roundtrip = object_read(arguments->arena, lazy_reference_kind_five.bytes,
                                                          (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS});
    }
    BUSTER_TEST(arguments, lazy_reference_kind_five_roundtrip.error == OBJECT_ERROR_NONE &&
                               lazy_reference_kind_five_roundtrip.symbol_count >= 1 &&
                               lazy_reference_kind_five_roundtrip.symbols[0].kind == OBJECT_SYMBOL_FUNCTION);
    ObjectArtifact unknown_reference_kind = object_write(arguments->arena, &direct_page_object, OBJECT_FORMAT_MACH_O64);
    u64 unknown_reference_symbol = object_test_mach_symbol_offset(unknown_reference_kind.bytes, 0);
    if (unknown_reference_symbol != UINT64_MAX && unknown_reference_symbol + 8 <= unknown_reference_kind.bytes.length)
    {
        object_test_write_u16(unknown_reference_kind.bytes, unknown_reference_symbol + 6, 7);
    }
    BUSTER_TEST(arguments, unknown_reference_kind.error == OBJECT_ERROR_NONE &&
                               object_read(arguments->arena, unknown_reference_kind.bytes,
                                           (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS})
                                       .error != OBJECT_ERROR_NONE);
    ObjectRelocation direct_page_addend_relocations[] = {
        direct_page_relocations[0],
        direct_page_relocations[1],
    };
    direct_page_addend_relocations[0].addend = 0x1000;
    direct_page_addend_relocations[1].addend = 4;
    ObjectFile direct_page_addend_object = direct_page_object;
    direct_page_addend_object.relocations = direct_page_addend_relocations;
    ObjectArtifact direct_page_addend_mach = object_write(arguments->arena, &direct_page_addend_object, OBJECT_FORMAT_MACH_O64);
    u32 direct_page_addend_raw_offset = 0;
    u32 direct_page_addend_relocation_offset = 0;
    u32 direct_page_addend_relocation_count = 0;
    bool direct_page_addend_offsets_valid = object_test_mach_text_offsets(direct_page_addend_mach.bytes, &direct_page_addend_raw_offset,
                                                                           &direct_page_addend_relocation_offset,
                                                                           &direct_page_addend_relocation_count);
    BUSTER_TEST(arguments, direct_page_addend_mach.error == OBJECT_ERROR_NONE && direct_page_addend_offsets_valid &&
                               direct_page_addend_relocation_count == 4);
    ObjectFile direct_page_addend_roundtrip = object_read(arguments->arena, direct_page_addend_mach.bytes,
                                                          (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS});
    BUSTER_TEST(arguments, direct_page_addend_roundtrip.error == OBJECT_ERROR_NONE && direct_page_addend_roundtrip.relocation_count == 2);
    if (direct_page_addend_roundtrip.error == OBJECT_ERROR_NONE && direct_page_addend_roundtrip.relocation_count == 2)
    {
        BUSTER_TEST(arguments, direct_page_addend_roundtrip.relocations[0].addend == 0x1000 &&
                                   direct_page_addend_roundtrip.relocations[1].addend == 4);
    }
    ObjectArtifact malformed_direct_page = object_write(arguments->arena, &direct_page_object, OBJECT_FORMAT_MACH_O64);
    if (object_test_mach_text_offsets(malformed_direct_page.bytes, &direct_page_raw_offset, &direct_page_relocation_offset, &direct_page_relocation_count))
    {
        object_test_write_u32(malformed_direct_page.bytes, (u64)direct_page_raw_offset, UINT32_C(0x14000000));
    }
    BUSTER_TEST(arguments, object_read(arguments->arena, malformed_direct_page.bytes,
                                       (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS})
                                 .error != OBJECT_ERROR_NONE);
    malformed_direct_page = object_write(arguments->arena, &direct_page_object, OBJECT_FORMAT_MACH_O64);
    if (object_test_mach_text_offsets(malformed_direct_page.bytes, &direct_page_raw_offset, &direct_page_relocation_offset, &direct_page_relocation_count))
    {
        u32 information = 0;
        memcpy(&information, malformed_direct_page.bytes.pointer + direct_page_relocation_offset + 8 + 4, sizeof(information));
        object_test_write_u32(malformed_direct_page.bytes, direct_page_relocation_offset + 8 + 4, information | (1u << 24));
    }
    BUSTER_TEST(arguments, object_read(arguments->arena, malformed_direct_page.bytes,
                                       (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS})
                                 .error != OBJECT_ERROR_NONE);
    malformed_direct_page = object_write(arguments->arena, &direct_page_object, OBJECT_FORMAT_MACH_O64);
    if (object_test_mach_text_offsets(malformed_direct_page.bytes, &direct_page_raw_offset, &direct_page_relocation_offset, &direct_page_relocation_count))
    {
        object_test_write_u32(malformed_direct_page.bytes, direct_page_relocation_offset + 8, 2 * sizeof(u32));
    }
    BUSTER_TEST(arguments, object_read(arguments->arena, malformed_direct_page.bytes,
                                       (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS})
                                 .error != OBJECT_ERROR_NONE);
    ObjectRelocation standalone_pageoff = direct_page_relocations[1];
    ObjectFile standalone_pageoff_object = direct_page_object;
    standalone_pageoff_object.relocations = &standalone_pageoff;
    standalone_pageoff_object.relocation_count = 1;
    ObjectArtifact standalone_pageoff_mach = object_write(arguments->arena, &standalone_pageoff_object, OBJECT_FORMAT_MACH_O64);
    BUSTER_TEST(arguments, standalone_pageoff_mach.error == OBJECT_ERROR_NONE &&
                               object_read(arguments->arena, standalone_pageoff_mach.bytes,
                                           (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS})
                                       .error == OBJECT_ERROR_NONE);

    ObjectRelocation standalone_page21 = direct_page_relocations[0];
    ObjectFile standalone_page21_object = direct_page_object;
    standalone_page21_object.relocations = &standalone_page21;
    standalone_page21_object.relocation_count = 1;
    ObjectArtifact standalone_page21_mach = object_write(arguments->arena, &standalone_page21_object, OBJECT_FORMAT_MACH_O64);
    BUSTER_TEST(arguments, standalone_page21_mach.error == OBJECT_ERROR_NONE &&
                               object_read(arguments->arena, standalone_page21_mach.bytes,
                                           (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS})
                                       .error == OBJECT_ERROR_NONE);

    // LLVM writes Mach-O relocations in descending place order.  The reader
    // must preserve each ADDEND's own PAGE relocation without pairing it to a
    // neighboring PAGE21/PAGEOFF12 record.
    ObjectArtifact descending_direct_page = object_write(arguments->arena, &direct_page_addend_object, OBJECT_FORMAT_MACH_O64);
    if (object_test_mach_text_offsets(descending_direct_page.bytes, &direct_page_addend_raw_offset, &direct_page_addend_relocation_offset,
                                      &direct_page_addend_relocation_count) &&
        direct_page_addend_relocation_count == 4)
    {
        u8 records[2 * 8];
        memcpy(records, descending_direct_page.bytes.pointer + direct_page_addend_relocation_offset, sizeof(records));
        memcpy(descending_direct_page.bytes.pointer + direct_page_addend_relocation_offset,
               descending_direct_page.bytes.pointer + direct_page_addend_relocation_offset + sizeof(records), sizeof(records));
        memcpy(descending_direct_page.bytes.pointer + direct_page_addend_relocation_offset + sizeof(records), records, sizeof(records));
    }
    ObjectFile descending_direct_page_roundtrip = object_read(arguments->arena, descending_direct_page.bytes,
                                                              (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS});
    BUSTER_TEST(arguments, descending_direct_page_roundtrip.error == OBJECT_ERROR_NONE && descending_direct_page_roundtrip.relocation_count == 2);
    if (descending_direct_page_roundtrip.error == OBJECT_ERROR_NONE && descending_direct_page_roundtrip.relocation_count == 2)
    {
        BUSTER_TEST(arguments, descending_direct_page_roundtrip.relocations[0].offset == sizeof(u32) &&
                                   descending_direct_page_roundtrip.relocations[0].kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12 &&
                                   descending_direct_page_roundtrip.relocations[0].addend == 4 &&
                                   descending_direct_page_roundtrip.relocations[1].offset == 0 &&
                                   descending_direct_page_roundtrip.relocations[1].kind == OBJECT_RELOCATION_AARCH64_MACH_PAGE21 &&
                                   descending_direct_page_roundtrip.relocations[1].addend == 0x1000);
    }

    u32 pageoff_words[] = {
        UINT32_C(0x91000041), // add x1, x2, #0
        UINT32_C(0xf9400083), // ldr x3, [x4]
        UINT32_C(0x394000c5), // ldrb w5, [x6]
        UINT32_C(0x79400107), // ldrh w7, [x8]
        UINT32_C(0x39800149), // ldrsb x9, [x10]
        UINT32_C(0x79c0018b), // ldrsh w11, [x12]
        UINT32_C(0xb98001cd), // ldrsw x13, [x14]
        UINT32_C(0x3d400020), // ldr b0, [x1]
        UINT32_C(0x7d400062), // ldr h2, [x3]
        UINT32_C(0xbd4000a4), // ldr s4, [x5]
        UINT32_C(0xfd4000e6), // ldr d6, [x7]
        UINT32_C(0x3d00016a), // str b10, [x11]
        UINT32_C(0x7d0001ac), // str h12, [x13]
        UINT32_C(0xbd0001ee), // str s14, [x15]
        UINT32_C(0xfd000230), // str d16, [x17]
        UINT32_C(0x3dc0020f), // ldr q15, [x16]
        UINT32_C(0x3d80024f), // str q15, [x18]
        UINT32_C(0xf9800280), // prfm pldl1keep, [x20]
        UINT32_C(0xf9800288), // prfm plil1keep, [x20]
        UINT32_C(0xf9800290), // prfm pstl1keep, [x20]
        UINT32_C(0x394000ff), // ldrb wzr, [x7]
        UINT32_C(0x390001ff), // strb wzr, [x15]
        UINT32_C(0xf94000ff), // ldr xzr, [x7]
        UINT32_C(0xf90001ff), // str xzr, [x15]
        UINT32_C(0x9100003f), // add sp, x1, #0
    };
    ObjectRelocation pageoff_relocations[BUSTER_ARRAY_LENGTH(pageoff_words)];
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(pageoff_relocations); index += 1)
    {
        pageoff_relocations[index] = (ObjectRelocation){
            .offset = index * sizeof(u32),
            .section = OBJECT_SECTION_TEXT,
            .symbol = 0,
            .kind = OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12,
        };
    }
    ObjectSection pageoff_sections[] = {
        {
            .name = S8(".text"),
            .data = {.pointer = (u8*)pageoff_words, .length = sizeof(pageoff_words)},
            .kind = OBJECT_SECTION_TEXT,
            .alignment = 4,
        },
    };
    ObjectFile pageoff_object = direct_page_object;
    pageoff_object.sections = pageoff_sections;
    pageoff_object.section_count = BUSTER_ARRAY_LENGTH(pageoff_sections);
    pageoff_object.relocations = pageoff_relocations;
    pageoff_object.relocation_count = BUSTER_ARRAY_LENGTH(pageoff_relocations);
    String8 pageoff_assembly = object_print_assembly(arguments->arena, &pageoff_object);
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tadd x1, x2, _direct_page_target@PAGEOFF\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tldr x3, [x4, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tldrb w5, [x6, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tldrh w7, [x8, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tldrsb x9, [x10, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tldrsh w11, [x12, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tldrsw x13, [x14, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tldr b0, [x1, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tldr h2, [x3, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tldr s4, [x5, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tldr d6, [x7, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tstr b10, [x11, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tstr h12, [x13, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tstr s14, [x15, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tstr d16, [x17, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tldr q15, [x16, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tstr q15, [x18, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tprfm pldl1keep, [x20, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tprfm plil1keep, [x20, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tprfm pstl1keep, [x20, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tldrb wzr, [x7, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tstrb wzr, [x15, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tldr xzr, [x7, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tstr xzr, [x15, _direct_page_target@PAGEOFF]\n")));
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(pageoff_assembly), S8("\tadd sp, x1, _direct_page_target@PAGEOFF\n")));
    ObjectArtifact pageoff_mach = object_write(arguments->arena, &pageoff_object, OBJECT_FORMAT_MACH_O64);
    ObjectFile pageoff_roundtrip = object_read(arguments->arena, pageoff_mach.bytes,
                                               (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS});
    BUSTER_TEST(arguments, pageoff_mach.error == OBJECT_ERROR_NONE && pageoff_roundtrip.error == OBJECT_ERROR_NONE &&
                               pageoff_roundtrip.relocation_count == BUSTER_ARRAY_LENGTH(pageoff_words));
    if (pageoff_roundtrip.error == OBJECT_ERROR_NONE && pageoff_roundtrip.relocation_count == BUSTER_ARRAY_LENGTH(pageoff_words))
    {
        BUSTER_TEST(arguments, pageoff_roundtrip.sections[OBJECT_SECTION_TEXT].data.length == sizeof(pageoff_words) &&
                                   memcmp(pageoff_roundtrip.sections[OBJECT_SECTION_TEXT].data.pointer, pageoff_words, sizeof(pageoff_words)) == 0);
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(pageoff_words); index += 1)
        {
            BUSTER_TEST(arguments, pageoff_roundtrip.relocations[index].kind == OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12 &&
                                       pageoff_roundtrip.relocations[index].offset == index * sizeof(u32));
        }
    }
    u32 valid_pageoff_bases[] = {
        UINT32_C(0x91000000),
        UINT32_C(0x39000000), UINT32_C(0x39400000), UINT32_C(0x39800000), UINT32_C(0x39c00000),
        UINT32_C(0x79000000), UINT32_C(0x79400000), UINT32_C(0x79800000), UINT32_C(0x79c00000),
        UINT32_C(0xb9000000), UINT32_C(0xb9400000), UINT32_C(0xb9800000),
        UINT32_C(0xf9000000), UINT32_C(0xf9400000), UINT32_C(0xf9800000),
        UINT32_C(0x3d000000), UINT32_C(0x3d400000), UINT32_C(0x3d800000), UINT32_C(0x3dc00000),
        UINT32_C(0x7d000000), UINT32_C(0x7d400000),
        UINT32_C(0xbd000000), UINT32_C(0xbd400000),
        UINT32_C(0xfd000000), UINT32_C(0xfd400000),
    };
    u32 invalid_pageoff_bases[] = {
        UINT32_C(0x91800000),
        UINT32_C(0xb9c00000), UINT32_C(0xf9c00000),
        UINT32_C(0x7d800000), UINT32_C(0x7dc00000),
        UINT32_C(0xbd800000), UINT32_C(0xbdc00000),
        UINT32_C(0xfd800000), UINT32_C(0xfdc00000),
    };
    ObjectFile pageoff_domain_object = pageoff_object;
    pageoff_domain_object.relocation_count = 1;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(valid_pageoff_bases); index += 1)
    {
        pageoff_words[0] = valid_pageoff_bases[index];
        BUSTER_TEST(arguments, object_write(arguments->arena, &pageoff_domain_object, OBJECT_FORMAT_MACH_O64).error == OBJECT_ERROR_NONE);
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid_pageoff_bases); index += 1)
    {
        pageoff_words[0] = invalid_pageoff_bases[index];
        BUSTER_TEST(arguments, object_write(arguments->arena, &pageoff_domain_object, OBJECT_FORMAT_MACH_O64).error != OBJECT_ERROR_NONE);
    }
    pageoff_words[0] = UINT32_C(0x91000041);
    u32 saved_pageoff_word = pageoff_words[0];
    pageoff_words[0] = UINT32_C(0x91000441); // stale imm12
    BUSTER_TEST(arguments, object_write(arguments->arena, &pageoff_object, OBJECT_FORMAT_MACH_O64).error != OBJECT_ERROR_NONE);
    pageoff_words[0] = UINT32_C(0x14000000); // unrelated opcode
    BUSTER_TEST(arguments, object_write(arguments->arena, &pageoff_object, OBJECT_FORMAT_MACH_O64).error != OBJECT_ERROR_NONE);
    pageoff_words[0] = saved_pageoff_word;
    ObjectRelocation misaligned_pageoff_relocation = pageoff_relocations[0];
    misaligned_pageoff_relocation.offset = 2;
    ObjectFile misaligned_pageoff_object = pageoff_object;
    misaligned_pageoff_object.relocations = &misaligned_pageoff_relocation;
    misaligned_pageoff_object.relocation_count = 1;
    BUSTER_TEST(arguments, object_write(arguments->arena, &misaligned_pageoff_object, OBJECT_FORMAT_MACH_O64).error != OBJECT_ERROR_NONE);
    ObjectRelocation out_of_range_page_addend = direct_page_relocations[0];
    out_of_range_page_addend.addend = INT64_C(0x800000);
    ObjectFile out_of_range_page_object = direct_page_object;
    out_of_range_page_object.relocations = &out_of_range_page_addend;
    out_of_range_page_object.relocation_count = 1;
    BUSTER_TEST(arguments, object_write(arguments->arena, &out_of_range_page_object, OBJECT_FORMAT_MACH_O64).error != OBJECT_ERROR_NONE);
    u32 saved_direct_page_high = 0;
    memcpy(&saved_direct_page_high, direct_page_words, sizeof(saved_direct_page_high));
    direct_page_words[0] |= 1u << 5; // stale ADRP immediate
    BUSTER_TEST(arguments, object_write(arguments->arena, &direct_page_object, OBJECT_FORMAT_MACH_O64).error != OBJECT_ERROR_NONE);
    memcpy(direct_page_words, &saved_direct_page_high, sizeof(saved_direct_page_high));

    ObjectArtifact stale_coff_branch = object_write(arguments->arena, &object, OBJECT_FORMAT_COFF);
    u32 coff_raw_offset = 0;
    if (stale_coff_branch.bytes.pointer && stale_coff_branch.bytes.length >= 44)
    {
        memcpy(&coff_raw_offset, stale_coff_branch.bytes.pointer + 40, sizeof(coff_raw_offset));
    }
    object_test_write_u32(stale_coff_branch.bytes, (u64)coff_raw_offset + relocation.offset, UINT32_C(0x14000001));
    BUSTER_TEST(arguments, object_read(arguments->arena, stale_coff_branch.bytes,
                                       (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_WINDOWS})
                                 .error == OBJECT_ERROR_UNSUPPORTED_TARGET);

    ObjectFormat strict_branch_formats[] = {OBJECT_FORMAT_ELF64, OBJECT_FORMAT_COFF, OBJECT_FORMAT_MACH_O64};
    aarch64_jump_instruction = UINT32_C(0x94000000);
    memcpy(aarch64_text + 4, &aarch64_jump_instruction, sizeof(aarch64_jump_instruction));
    for (u32 format_index = 0; format_index < BUSTER_ARRAY_LENGTH(strict_branch_formats); format_index += 1)
    {
        BUSTER_TEST(arguments, object_write(arguments->arena, &object, strict_branch_formats[format_index]).error == OBJECT_ERROR_INVALID_INPUT);
    }
    aarch64_jump_instruction = UINT32_C(0x14000001);
    memcpy(aarch64_text + 4, &aarch64_jump_instruction, sizeof(aarch64_jump_instruction));
    for (u32 format_index = 0; format_index < BUSTER_ARRAY_LENGTH(strict_branch_formats); format_index += 1)
    {
        BUSTER_TEST(arguments, object_write(arguments->arena, &object, strict_branch_formats[format_index]).error == OBJECT_ERROR_INVALID_INPUT);
    }
    aarch64_jump_instruction = UINT32_C(0x14000000);
    memcpy(aarch64_text + 4, &aarch64_jump_instruction, sizeof(aarch64_jump_instruction));
    ObjectArtifact underaligned_elf = object_write(arguments->arena, &object, OBJECT_FORMAT_ELF64);
    u64 underaligned_relocation_section = 0;
    u64 underaligned_target_data = 0;
    bool underaligned_offsets_valid =
        object_test_elf_relocation_offsets(underaligned_elf.bytes, &underaligned_relocation_section, &underaligned_target_data);
    if (underaligned_offsets_valid)
    {
        u64 section_table = 0;
        u32 target_index = 0;
        memcpy(&section_table, underaligned_elf.bytes.pointer + 40, sizeof(section_table));
        memcpy(&target_index, underaligned_elf.bytes.pointer + underaligned_relocation_section + 44, sizeof(target_index));
        object_test_write_u64(underaligned_elf.bytes, section_table + (u64)target_index * 64 + 48, 1);
    }
    BUSTER_TEST(arguments, underaligned_offsets_valid &&
                               object_read(arguments->arena, underaligned_elf.bytes,
                                           (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX})
                                       .error == OBJECT_ERROR_UNSUPPORTED_TARGET);
    sections[0].alignment = 1;
    BUSTER_TEST(arguments, object_write(arguments->arena, &object, OBJECT_FORMAT_ELF64).error == OBJECT_ERROR_INVALID_INPUT);
    sections[0].alignment = 16;
    relocation.offset = 5;
    BUSTER_TEST(arguments, object_write(arguments->arena, &object, OBJECT_FORMAT_ELF64).error == OBJECT_ERROR_INVALID_INPUT);
    relocation.offset = UINT64_MAX - 1;
    BUSTER_TEST(arguments, object_write(arguments->arena, &object, OBJECT_FORMAT_ELF64).error == OBJECT_ERROR_INVALID_INPUT);
    relocation.offset = 4;
    u8 misaligned_link_text[12] = {0};
    memcpy(misaligned_link_text + 1, &aarch64_jump_instruction, sizeof(aarch64_jump_instruction));
    ObjectSection misaligned_link_section = {
        .name = S8(".text"),
        .data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(misaligned_link_text),
        .kind = OBJECT_SECTION_TEXT,
        .alignment = 16,
    };
    ObjectSymbol misaligned_link_symbols[] = {
        {
            .name = S8("misaligned_entry"),
            .size = 5,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("misaligned_target"),
            .value = 5,
            .size = 4,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
        },
    };
    ObjectRelocation misaligned_link_relocation = {
        .offset = 1,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_AARCH64_JUMP26,
    };
    ObjectFile misaligned_link_object = {
        .sections = &misaligned_link_section,
        .symbols = misaligned_link_symbols,
        .relocations = &misaligned_link_relocation,
        .target = {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX},
        .section_count = 1,
        .symbol_count = BUSTER_ARRAY_LENGTH(misaligned_link_symbols),
        .relocation_count = 1,
    };
    ObjectExecutable misaligned_link_executable = object_link_executable(&misaligned_link_object);
    BUSTER_TEST(arguments, misaligned_link_executable.error == OBJECT_ERROR_CAPACITY);
    object_release_executable(misaligned_link_executable);

    // The in-memory AArch64 linker shares the checked Mach PAGE patch rules
    // with the JIT.  Local data is enough to exercise both fixups without
    // requiring an AArch64 host, while TLVP remains an explicit resolver-only
    // exclusion and must fail before exposing an executable mapping.
    u32 linked_page_words[] = {UINT32_C(0x90000009), UINT32_C(0x91000129)};
    u64 linked_page_data = UINT64_C(0x1122334455667788);
    ObjectSection linked_page_sections[] = {
        {
            .name = S8(".text"),
            .data = {.pointer = (u8*)linked_page_words, .length = sizeof(linked_page_words)},
            .kind = OBJECT_SECTION_TEXT,
            .alignment = 4,
        },
        {
            .name = S8(".data"),
            .data = {.pointer = (u8*)&linked_page_data, .length = sizeof(linked_page_data)},
            .kind = OBJECT_SECTION_READ_ONLY_DATA,
            .alignment = 8,
        },
    };
    ObjectSymbol linked_page_symbols[] = {
        {
            .name = S8("linked_page_entry"),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
            .size = sizeof(linked_page_words),
        },
        {
            .name = S8("linked_page_data"),
            .section = 1,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
            .size = sizeof(linked_page_data),
        },
    };
    ObjectRelocation linked_page_relocations[] = {
        {
            .offset = 0,
            .section = OBJECT_SECTION_TEXT,
            .symbol = 1,
            .kind = OBJECT_RELOCATION_AARCH64_MACH_PAGE21,
        },
        {
            .offset = sizeof(u32),
            .section = OBJECT_SECTION_TEXT,
            .symbol = 1,
            .kind = OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12,
        },
    };
    ObjectFile linked_page_object = {
        .sections = linked_page_sections,
        .symbols = linked_page_symbols,
        .relocations = linked_page_relocations,
        .target = {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX},
        .section_count = BUSTER_ARRAY_LENGTH(linked_page_sections),
        .symbol_count = BUSTER_ARRAY_LENGTH(linked_page_symbols),
        .relocation_count = BUSTER_ARRAY_LENGTH(linked_page_relocations),
    };
    ObjectExecutable linked_page_executable = object_link_executable(&linked_page_object);
    BUSTER_TEST(arguments, linked_page_executable.error == OBJECT_ERROR_NONE && linked_page_executable.address);
    if (linked_page_executable.error == OBJECT_ERROR_NONE && linked_page_executable.address)
    {
        u8* linked_page_text = (u8*)linked_page_executable.address;
        u8* linked_page_data_address = linked_page_text + 8;
        u32 expected_linked_page21 = 0;
        u32 actual_linked_page21 = 0;
        u32 actual_linked_pageoff12 = 0;
        memcpy(&actual_linked_page21, linked_page_text, sizeof(actual_linked_page21));
        memcpy(&actual_linked_pageoff12, linked_page_text + sizeof(u32), sizeof(actual_linked_pageoff12));
        bool linked_page21_valid = a64_adrp_encode(9, (u64)(uintptr_t)linked_page_text, (u64)(uintptr_t)linked_page_data_address, &expected_linked_page21);
        BUSTER_TEST(arguments, linked_page21_valid && actual_linked_page21 == expected_linked_page21);
        BUSTER_TEST(arguments, actual_linked_pageoff12 == (UINT32_C(0x91000000) | (u32)((u64)(uintptr_t)linked_page_data_address & 0xfff) << 10 | (9u << 5) | 9u));
        object_release_executable(linked_page_executable);
    }
    ObjectRelocation linked_page_tlvp_relocation = linked_page_relocations[0];
    linked_page_tlvp_relocation.kind = OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21;
    ObjectFile linked_page_tlvp_object = linked_page_object;
    linked_page_tlvp_object.relocations = &linked_page_tlvp_relocation;
    linked_page_tlvp_object.relocation_count = 1;
    ObjectExecutable linked_page_tlvp_executable = object_link_executable(&linked_page_tlvp_object);
    BUSTER_TEST(arguments, linked_page_tlvp_executable.error == OBJECT_ERROR_UNSUPPORTED_TARGET && !linked_page_tlvp_executable.address);
    object_release_executable(linked_page_tlvp_executable);

    String8 aarch64_jump_assembly = object_print_assembly(arguments->arena, &object);
    BUSTER_TEST(arguments, object_bytes_contain(BUSTER_SLICE_TO_BYTE_SLICE(aarch64_jump_assembly), S8("\tb object_callee\n")));
    aarch64_jump_instruction = UINT32_C(0x94000000);
    memcpy(aarch64_text + 4, &aarch64_jump_instruction, sizeof(aarch64_jump_instruction));
    relocation.kind = OBJECT_RELOCATION_AARCH64_CALL26;
    sections[0].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(x86_text);
    object.target.cpu_arch = CPU_ARCH_X86_64;
    relocation = (ObjectRelocation){
        .addend = -4,
        .offset = 1,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    symbols[0].size = 6;
    symbols[1].value = 6;
    symbols[1].size = 6;
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    ObjectExecutable executable = object_link_executable(&object);
    BUSTER_TEST(arguments, executable.error == OBJECT_ERROR_NONE);
    if (executable.address)
    {
        u64 (*entry)(void) = 0;
        memcpy(&entry, &executable.address, sizeof(entry));
        BUSTER_TEST(arguments, entry() == 42);
        object_release_executable(executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    sections[0].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(aarch64_text);
    object.target.cpu_arch = CPU_ARCH_AARCH64;
    relocation = (ObjectRelocation){
        .offset = 4,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_AARCH64_CALL26,
    };
    symbols[0].size = 16;
    symbols[1].value = 16;
    symbols[1].size = 8;
    ObjectExecutable executable = object_link_executable(&object);
    BUSTER_TEST(arguments, executable.error == OBJECT_ERROR_NONE);
    if (executable.address)
    {
        u64 (*entry)(void) = 0;
        memcpy(&entry, &executable.address, sizeof(entry));
        BUSTER_TEST(arguments, entry() == 42);
        object_release_executable(executable);
    }
#else
    BUSTER_UNUSED(aarch64_text);
#endif
    IrProgram separate_program = ir_program_initialize(arguments->arena, 0, 0, 256, 0);
    IrSymbolId defined_symbol = ir_program_add_symbol(&separate_program, (IrSymbol){
        .name = S8("defined_function"),
        .kind = IR_SYMBOL_FUNCTION,
        .linkage = IR_LINKAGE_EXTERNAL,
        .is_definition = true,
    });
    IrSymbolId undefined_symbol = ir_program_add_symbol(&separate_program, (IrSymbol){
        .name = S8("external_function"),
        .kind = IR_SYMBOL_FUNCTION,
        .linkage = IR_LINKAGE_EXTERNAL,
    });
    u8 separate_code[] = {
        0xe8, 0, 0, 0, 0, 0xc3,
    };
    CodegenModuleEntry separate_entry = {
        .symbol = defined_symbol,
    };
    CodegenFunctionDescriptor separate_function = {
        .symbol = defined_symbol,
        .code_size = (u32)sizeof(separate_code),
    };
    CodegenModuleRelocation separate_relocation = {
        .symbol = undefined_symbol,
        .offset = 1,
    };
    CodegenModule separate_module = {
        .code = (ByteSlice)BUSTER_ARRAY_TO_SLICE(separate_code),
        .entries = &separate_entry,
        .functions = &separate_function,
        .relocations = &separate_relocation,
        .abi = CODEGEN_ABI_X86_64_SYSTEM_V,
        .entry_count = 1,
        .function_count = 1,
        .relocation_count = 1,
    };
    ObjectFile separate_object = object_from_canonical_codegen_module(arguments->arena, &separate_program, &separate_module,
                                                            (Target){
                                                                .cpu_arch = CPU_ARCH_X86_64,
                                                                .os = OPERATING_SYSTEM_LINUX,
                                                            });
    BUSTER_TEST(arguments, separate_object.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, separate_object.symbol_count == 2);
    BUSTER_TEST(arguments, separate_object.sections[OBJECT_SECTION_UNWIND].data.length > 0);
    bool separate_symbol_is_undefined = false;
    if (separate_object.symbols && separate_object.symbol_count >= 2)
    {
        separate_symbol_is_undefined = separate_object.symbols[1].section == OBJECT_SECTION_UNDEFINED;
    }
    BUSTER_TEST(arguments, separate_symbol_is_undefined);
    BUSTER_TEST(arguments, separate_object.relocation_count == 2);
    bool separate_cfi_relocation = false;
    for (u32 relocation_index = 0; relocation_index < separate_object.relocation_count; relocation_index += 1)
    {
        ObjectRelocation* candidate = separate_object.relocations + relocation_index;
        separate_cfi_relocation |= candidate->section == OBJECT_SECTION_UNWIND && candidate->symbol == 0 &&
                                   candidate->kind == OBJECT_RELOCATION_X86_64_PC32;
    }
    BUSTER_TEST(arguments, separate_cfi_relocation);

    enum
    {
        OBJECT_LOOKUP_STRESS_ENTRY_COUNT = 128,
        OBJECT_LOOKUP_STRESS_RELOCATION_COUNT = 512,
    };
    u8* lookup_stress_code = arena_allocate(arguments->arena, u8, OBJECT_LOOKUP_STRESS_RELOCATION_COUNT * 4);
    CodegenModuleEntry* lookup_stress_entries = arena_allocate(arguments->arena, CodegenModuleEntry, OBJECT_LOOKUP_STRESS_ENTRY_COUNT);
    CodegenFunctionDescriptor* lookup_stress_functions = arena_allocate(arguments->arena, CodegenFunctionDescriptor, OBJECT_LOOKUP_STRESS_ENTRY_COUNT);
    CodegenModuleRelocation* lookup_stress_relocations =
        arena_allocate(arguments->arena, CodegenModuleRelocation, OBJECT_LOOKUP_STRESS_RELOCATION_COUNT);
    memset(lookup_stress_code, 0xcc, OBJECT_LOOKUP_STRESS_RELOCATION_COUNT * 4);
    memset(lookup_stress_entries, 0, sizeof(*lookup_stress_entries) * OBJECT_LOOKUP_STRESS_ENTRY_COUNT);
    memset(lookup_stress_functions, 0, sizeof(*lookup_stress_functions) * OBJECT_LOOKUP_STRESS_ENTRY_COUNT);
    memset(lookup_stress_relocations, 0, sizeof(*lookup_stress_relocations) * OBJECT_LOOKUP_STRESS_RELOCATION_COUNT);
    for (u32 index = 0; index < OBJECT_LOOKUP_STRESS_ENTRY_COUNT; index += 1)
    {
        IrSymbolId symbol = ir_program_add_symbol(&separate_program, (IrSymbol){
            .name = string_format(arguments->arena, S8("lookup_{u32}"), index),
            .kind = IR_SYMBOL_FUNCTION,
            .linkage = IR_LINKAGE_EXTERNAL,
            .is_definition = true,
        });
        lookup_stress_entries[index] = (CodegenModuleEntry){
            .symbol = symbol,
            .offset = index * 4,
        };
        lookup_stress_functions[index] = (CodegenFunctionDescriptor){
            .symbol = symbol,
            .code_offset = index * 4,
            .code_size = 4,
        };
    }
    // Duplicate canonical symbol lookup is deliberately first-entry-wins.
    lookup_stress_entries[OBJECT_LOOKUP_STRESS_ENTRY_COUNT - 1].symbol = lookup_stress_entries[0].symbol;
    lookup_stress_functions[OBJECT_LOOKUP_STRESS_ENTRY_COUNT - 1].symbol = lookup_stress_entries[0].symbol;
    IrSymbolId missing_lookup_symbol = ir_program_add_symbol(&separate_program, (IrSymbol){
        .name = S8("lookup_missing"),
        .kind = IR_SYMBOL_FUNCTION,
        .linkage = IR_LINKAGE_EXTERNAL,
    });
    for (u32 index = 0; index < OBJECT_LOOKUP_STRESS_RELOCATION_COUNT; index += 1)
    {
        u32 target_index = (index * 37) % OBJECT_LOOKUP_STRESS_ENTRY_COUNT;
        bool missing = index % 29 == 0;
        lookup_stress_relocations[index] = (CodegenModuleRelocation){
            .symbol = missing ? missing_lookup_symbol : lookup_stress_entries[target_index].symbol,
            .offset = index * 4,
        };
    }
    CodegenModule lookup_stress_module = {
        .code = {.pointer = lookup_stress_code, .length = OBJECT_LOOKUP_STRESS_RELOCATION_COUNT * 4},
        .entries = lookup_stress_entries,
        .functions = lookup_stress_functions,
        .relocations = lookup_stress_relocations,
        .abi = CODEGEN_ABI_X86_64_SYSTEM_V,
        .entry_count = OBJECT_LOOKUP_STRESS_ENTRY_COUNT,
        .function_count = OBJECT_LOOKUP_STRESS_ENTRY_COUNT,
        .relocation_count = OBJECT_LOOKUP_STRESS_RELOCATION_COUNT,
    };
    ObjectFile lookup_stress_object = object_from_canonical_codegen_module(arguments->arena, &separate_program, &lookup_stress_module,
                                                                  (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX});
    BUSTER_TEST(arguments, lookup_stress_object.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, lookup_stress_object.symbol_count == OBJECT_LOOKUP_STRESS_ENTRY_COUNT + 1);
    BUSTER_TEST(arguments, lookup_stress_object.relocation_count >= OBJECT_LOOKUP_STRESS_RELOCATION_COUNT);
    if (lookup_stress_object.error == OBJECT_ERROR_NONE && lookup_stress_object.relocation_count >= OBJECT_LOOKUP_STRESS_RELOCATION_COUNT)
    {
        bool lookup_stress_mapping_valid = true;
        for (u32 index = 0; index < OBJECT_LOOKUP_STRESS_RELOCATION_COUNT; index += 1)
        {
            u32 target_index = (index * 37) % OBJECT_LOOKUP_STRESS_ENTRY_COUNT;
            u32 expected_symbol = index % 29 == 0                             ? OBJECT_LOOKUP_STRESS_ENTRY_COUNT
                                  : target_index == OBJECT_LOOKUP_STRESS_ENTRY_COUNT - 1 ? 0
                                                                                       : target_index;
            lookup_stress_mapping_valid &= lookup_stress_object.relocations[index].symbol == expected_symbol;
        }
        BUSTER_TEST(arguments, lookup_stress_mapping_valid);
    }

    ObjectArtifact separate_elf = object_write(arguments->arena, &separate_object, OBJECT_FORMAT_ELF64);
    BUSTER_TEST(arguments, separate_elf.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, object_bytes_contain(separate_elf.bytes, S8(".eh_frame")));
    BUSTER_TEST(arguments, object_bytes_contain(separate_elf.bytes, S8(".rela.eh_frame")));
    ObjectFile separate_roundtrip = object_read(arguments->arena, separate_elf.bytes, separate_object.target);
    BUSTER_TEST(arguments, separate_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, separate_roundtrip.sections[OBJECT_SECTION_UNWIND].data.length ==
                               separate_object.sections[OBJECT_SECTION_UNWIND].data.length);
    bool separate_roundtrip_cfi = false;
    for (u32 relocation_index = 0; relocation_index < separate_roundtrip.relocation_count; relocation_index += 1)
    {
        ObjectRelocation* candidate = separate_roundtrip.relocations + relocation_index;
        separate_roundtrip_cfi |= candidate->section == OBJECT_SECTION_UNWIND && candidate->kind == OBJECT_RELOCATION_X86_64_PC32;
    }
    BUSTER_TEST(arguments, separate_roundtrip_cfi);

    u8 windows_unwind_code[32] = {0};
    CodegenUnwindAction windows_unwind_actions[] = {
        {
            .code_offset = 1,
            .kind = CODEGEN_UNWIND_ACTION_PUSH_REGISTER,
            .register_index = 5,
        },
        {
            .code_offset = 4,
            .kind = CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER,
            .register_index = 5,
        },
        {
            .code_offset = 11,
            .value = 144,
            .kind = CODEGEN_UNWIND_ACTION_ALLOCATE_STACK,
        },
    };
    CodegenFunctionDescriptor windows_unwind_function = {
        .unwind_actions = windows_unwind_actions,
        .code_size = sizeof(windows_unwind_code),
        .prolog_size = 11,
        .unwind_action_count = BUSTER_ARRAY_LENGTH(windows_unwind_actions),
    };
    CodegenModule windows_unwind_module = {
        .code = BUSTER_ARRAY_TO_SLICE(windows_unwind_code),
        .entries = &separate_entry,
        .functions = &windows_unwind_function,
        .abi = CODEGEN_ABI_X86_64_WINDOWS,
        .entry_count = 1,
        .function_count = 1,
    };
    Target windows_unwind_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .os = OPERATING_SYSTEM_WINDOWS,
    };
    ObjectFile windows_unwind_object =
        object_from_canonical_codegen_module(arguments->arena, &separate_program, &windows_unwind_module, windows_unwind_target);
    BUSTER_TEST(arguments, windows_unwind_object.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, windows_unwind_object.sections[OBJECT_SECTION_UNWIND].data.length == 0);
    BUSTER_TEST(arguments, windows_unwind_object.sections[OBJECT_SECTION_WINDOWS_PDATA].data.length == 12);
    BUSTER_TEST(arguments, windows_unwind_object.sections[OBJECT_SECTION_WINDOWS_XDATA].data.length == 12);
    u8 expected_windows_xdata[] = {
        1, 11, 4, 0x95, 11, 1, 18, 0, 4, 3, 1, 0x50,
    };
    BUSTER_TEST(arguments, windows_unwind_object.sections[OBJECT_SECTION_WINDOWS_XDATA].data.length == sizeof(expected_windows_xdata) &&
                               memcmp(windows_unwind_object.sections[OBJECT_SECTION_WINDOWS_XDATA].data.pointer, expected_windows_xdata,
                                      sizeof(expected_windows_xdata)) == 0);
    BUSTER_TEST(arguments, windows_unwind_object.relocation_count == 3);
    for (u32 relocation_index = 0; relocation_index < windows_unwind_object.relocation_count; relocation_index += 1)
    {
        BUSTER_TEST(arguments, windows_unwind_object.relocations[relocation_index].section == OBJECT_SECTION_WINDOWS_PDATA);
        BUSTER_TEST(arguments, windows_unwind_object.relocations[relocation_index].kind == OBJECT_RELOCATION_COFF_ADDR32NB);
    }
    ObjectArtifact windows_unwind_coff = object_write(arguments->arena, &windows_unwind_object, OBJECT_FORMAT_COFF);
    BUSTER_TEST(arguments, windows_unwind_coff.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, object_bytes_contain(windows_unwind_coff.bytes, S8(".pdata")));
    BUSTER_TEST(arguments, object_bytes_contain(windows_unwind_coff.bytes, S8(".xdata")));
    ObjectFile windows_unwind_roundtrip = object_read(arguments->arena, windows_unwind_coff.bytes, windows_unwind_target);
    BUSTER_TEST(arguments, windows_unwind_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, windows_unwind_roundtrip.sections[OBJECT_SECTION_WINDOWS_PDATA].data.length == 12);
    BUSTER_TEST(arguments, windows_unwind_roundtrip.sections[OBJECT_SECTION_WINDOWS_XDATA].data.length == sizeof(expected_windows_xdata));
    BUSTER_TEST(arguments, windows_unwind_roundtrip.relocation_count == 3);
    for (u32 relocation_index = 0; relocation_index < windows_unwind_roundtrip.relocation_count; relocation_index += 1)
    {
        BUSTER_TEST(arguments, windows_unwind_roundtrip.relocations[relocation_index].section == OBJECT_SECTION_WINDOWS_PDATA);
        BUSTER_TEST(arguments, windows_unwind_roundtrip.relocations[relocation_index].kind == OBJECT_RELOCATION_COFF_ADDR32NB);
    }
    u8 windows_save_code[16] = {0};
    CodegenUnwindAction windows_save_actions[] = {
        {
            .code_offset = 8,
            .value = 512,
            .kind = CODEGEN_UNWIND_ACTION_SAVE_REGISTER,
            .register_index = 3,
        },
    };
    CodegenFunctionDescriptor windows_save_function = {
        .unwind_actions = windows_save_actions,
        .code_size = sizeof(windows_save_code),
        .prolog_size = 8,
        .unwind_action_count = BUSTER_ARRAY_LENGTH(windows_save_actions),
    };
    CodegenModule windows_save_module = windows_unwind_module;
    windows_save_module.code = (ByteSlice)BUSTER_ARRAY_TO_SLICE(windows_save_code);
    windows_save_module.functions = &windows_save_function;
    ObjectFile windows_save_object = object_from_canonical_codegen_module(arguments->arena, &separate_program, &windows_save_module, windows_unwind_target);
    BUSTER_TEST(arguments, windows_save_object.error == OBJECT_ERROR_NONE);
    u8 expected_windows_save_xdata[] = {
        1, 8, 2, 0, 8, 0x34, 64, 0,
    };
    ByteSlice windows_save_xdata = windows_save_object.sections[OBJECT_SECTION_WINDOWS_XDATA].data;
    BUSTER_TEST(arguments, windows_save_xdata.length == sizeof(expected_windows_save_xdata) &&
                               memcmp(windows_save_xdata.pointer, expected_windows_save_xdata, sizeof(expected_windows_save_xdata)) == 0);
    u8 windows_far_save_code[16] = {0};
    windows_save_actions[0].value = 524288;
    CodegenModule windows_far_save_module = windows_save_module;
    windows_far_save_module.code = (ByteSlice)BUSTER_ARRAY_TO_SLICE(windows_far_save_code);
    ObjectFile windows_far_save_object = object_from_canonical_codegen_module(arguments->arena, &separate_program, &windows_far_save_module, windows_unwind_target);
    BUSTER_TEST(arguments, windows_far_save_object.error == OBJECT_ERROR_NONE);
    u8 expected_windows_far_save_xdata[] = {
        1, 8, 3, 0, 8, 0x35, 0, 0, 8, 0, 0, 0,
    };
    ByteSlice windows_far_save_xdata = windows_far_save_object.sections[OBJECT_SECTION_WINDOWS_XDATA].data;
    BUSTER_TEST(arguments, windows_far_save_xdata.length == sizeof(expected_windows_far_save_xdata) &&
                               memcmp(windows_far_save_xdata.pointer, expected_windows_far_save_xdata, sizeof(expected_windows_far_save_xdata)) == 0);
    ObjectArtifact windows_far_save_coff = object_write(arguments->arena, &windows_far_save_object, OBJECT_FORMAT_COFF);
    BUSTER_TEST(arguments, windows_far_save_coff.error == OBJECT_ERROR_NONE);
    ObjectFile windows_far_save_roundtrip = object_read(arguments->arena, windows_far_save_coff.bytes, windows_unwind_target);
    BUSTER_TEST(arguments, windows_far_save_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, windows_far_save_roundtrip.sections[OBJECT_SECTION_WINDOWS_XDATA].data.length == sizeof(expected_windows_far_save_xdata));
    if (windows_far_save_roundtrip.sections[OBJECT_SECTION_WINDOWS_XDATA].data.length == sizeof(expected_windows_far_save_xdata))
    {
        BUSTER_TEST(arguments, memcmp(windows_far_save_roundtrip.sections[OBJECT_SECTION_WINDOWS_XDATA].data.pointer, expected_windows_far_save_xdata,
                                      sizeof(expected_windows_far_save_xdata)) == 0);
    }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    ObjectExecutable windows_unwind_executable = object_link_executable(&windows_unwind_object);
    BUSTER_TEST(arguments, windows_unwind_executable.error == OBJECT_ERROR_NONE);
    if (windows_unwind_executable.address)
    {
        object_release_executable(windows_unwind_executable);
    }
#endif

    u8 windows_arm64_code[64] = {0};
    u32 windows_arm64_epilog = 48;
    CodegenUnwindAction windows_arm64_actions[] = {
        {.code_offset = 4, .value = 16, .kind = CODEGEN_UNWIND_ACTION_ALLOCATE_STACK},
        {.code_offset = 4, .kind = CODEGEN_UNWIND_ACTION_SAVE_REGISTER, .register_index = 29},
        {.code_offset = 4, .value = 8, .kind = CODEGEN_UNWIND_ACTION_SAVE_REGISTER, .register_index = 30},
        {.code_offset = 8, .kind = CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, .register_index = 29},
        {.code_offset = 12, .value = 48, .kind = CODEGEN_UNWIND_ACTION_ALLOCATE_STACK},
        {.code_offset = 16, .kind = CODEGEN_UNWIND_ACTION_NOP},
        {.code_offset = 20, .kind = CODEGEN_UNWIND_ACTION_SAVE_REGISTER, .register_index = 28},
        {.code_offset = 24, .kind = CODEGEN_UNWIND_ACTION_NOP},
    };
    CodegenFunctionDescriptor windows_arm64_function = {
        .unwind_actions = windows_arm64_actions,
        .epilog_offsets = &windows_arm64_epilog,
        .code_size = sizeof(windows_arm64_code),
        .prolog_size = 24,
        .unwind_action_count = BUSTER_ARRAY_LENGTH(windows_arm64_actions),
        .epilog_count = 1,
    };
    CodegenModule windows_arm64_module = {
        .code = BUSTER_ARRAY_TO_SLICE(windows_arm64_code),
        .entries = &separate_entry,
        .functions = &windows_arm64_function,
        .abi = CODEGEN_ABI_AARCH64_WINDOWS,
        .entry_count = 1,
        .function_count = 1,
    };
    Target windows_arm64_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .os = OPERATING_SYSTEM_WINDOWS,
    };
    ObjectFile windows_arm64_object =
        object_from_canonical_codegen_module(arguments->arena, &separate_program, &windows_arm64_module, windows_arm64_target);
    BUSTER_TEST(arguments, windows_arm64_object.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, windows_arm64_object.sections[OBJECT_SECTION_WINDOWS_PDATA].data.length == 8);
    u8 expected_windows_arm64_xdata[] = {
        0x10, 0x00, 0x40, 0x20, 0x0c, 0x00, 0x00, 0x02, 0xe3, 0xd2, 0x40, 0xe3, 0x03, 0xe1, 0x81, 0xe4, 0xe3, 0xd2, 0x40, 0x03,
        0x81, 0xe4, 0x00, 0x00,
    };
    ByteSlice windows_arm64_xdata = windows_arm64_object.sections[OBJECT_SECTION_WINDOWS_XDATA].data;
    BUSTER_TEST(arguments, windows_arm64_xdata.length == sizeof(expected_windows_arm64_xdata) &&
                               memcmp(windows_arm64_xdata.pointer, expected_windows_arm64_xdata, sizeof(expected_windows_arm64_xdata)) == 0);
    BUSTER_TEST(arguments, windows_arm64_object.relocation_count == 2);
    ObjectArtifact windows_arm64_coff = object_write(arguments->arena, &windows_arm64_object, OBJECT_FORMAT_COFF);
    BUSTER_TEST(arguments, windows_arm64_coff.error == OBJECT_ERROR_NONE);
    ObjectFile windows_arm64_roundtrip = object_read(arguments->arena, windows_arm64_coff.bytes, windows_arm64_target);
    BUSTER_TEST(arguments, windows_arm64_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, windows_arm64_roundtrip.sections[OBJECT_SECTION_WINDOWS_PDATA].data.length == 8);
    BUSTER_TEST(arguments, windows_arm64_roundtrip.sections[OBJECT_SECTION_WINDOWS_XDATA].data.length == sizeof(expected_windows_arm64_xdata));
    BUSTER_TEST(arguments, windows_arm64_roundtrip.relocation_count == 2);
    for (u32 relocation_index = 0; relocation_index < windows_arm64_roundtrip.relocation_count; relocation_index += 1)
    {
        BUSTER_TEST(arguments, windows_arm64_roundtrip.relocations[relocation_index].section == OBJECT_SECTION_WINDOWS_PDATA);
        BUSTER_TEST(arguments, windows_arm64_roundtrip.relocations[relocation_index].kind == OBJECT_RELOCATION_COFF_ADDR32NB);
    }

    CodegenModule mach_cfi_module = separate_module;
    mach_cfi_module.relocations = 0;
    mach_cfi_module.relocation_count = 0;
    ObjectFile mach_cfi_object = object_from_canonical_codegen_module(arguments->arena, &separate_program, &mach_cfi_module,
                                                            (Target){
                                                                .cpu_arch = CPU_ARCH_X86_64,
                                                                .os = OPERATING_SYSTEM_MACOS,
                                                            });
    BUSTER_TEST(arguments, mach_cfi_object.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, mach_cfi_object.sections[OBJECT_SECTION_UNWIND].data.length > 0);
    ObjectArtifact mach_cfi_artifact = object_write(arguments->arena, &mach_cfi_object, OBJECT_FORMAT_MACH_O64);
    BUSTER_TEST(arguments, mach_cfi_artifact.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, object_bytes_contain(mach_cfi_artifact.bytes, S8("__eh_frame")));
    ObjectFile mach_cfi_roundtrip = object_read(arguments->arena, mach_cfi_artifact.bytes, mach_cfi_object.target);
    BUSTER_TEST(arguments, mach_cfi_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, mach_cfi_roundtrip.sections[OBJECT_SECTION_UNWIND].data.length ==
                               mach_cfi_object.sections[OBJECT_SECTION_UNWIND].data.length);
    BUSTER_TEST(arguments, mach_cfi_roundtrip.relocation_count == 1);
    if (mach_cfi_roundtrip.relocation_count == 1)
    {
        BUSTER_TEST(arguments, mach_cfi_roundtrip.relocations[0].section == OBJECT_SECTION_UNWIND);
        BUSTER_TEST(arguments, mach_cfi_roundtrip.relocations[0].kind == OBJECT_RELOCATION_X86_64_PC32);
    }

    CodegenModule a64_cfi_module = separate_module;
    a64_cfi_module.relocations = 0;
    a64_cfi_module.relocation_count = 0;
    a64_cfi_module.abi = CODEGEN_ABI_AARCH64_AAPCS64;
    ObjectFile a64_cfi_object = object_from_canonical_codegen_module(arguments->arena, &separate_program, &a64_cfi_module,
                                                           (Target){
                                                               .cpu_arch = CPU_ARCH_AARCH64,
                                                               .os = OPERATING_SYSTEM_LINUX,
                                                           });
    BUSTER_TEST(arguments, a64_cfi_object.error == OBJECT_ERROR_NONE);
    ObjectArtifact a64_cfi_elf = object_write(arguments->arena, &a64_cfi_object, OBJECT_FORMAT_ELF64);
    BUSTER_TEST(arguments, a64_cfi_elf.error == OBJECT_ERROR_NONE);
    ObjectFile a64_cfi_roundtrip = object_read(arguments->arena, a64_cfi_elf.bytes, a64_cfi_object.target);
    BUSTER_TEST(arguments, a64_cfi_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, a64_cfi_roundtrip.relocation_count == 1);
    if (a64_cfi_roundtrip.relocation_count == 1)
    {
        BUSTER_TEST(arguments, a64_cfi_roundtrip.relocations[0].section == OBJECT_SECTION_UNWIND);
        BUSTER_TEST(arguments, a64_cfi_roundtrip.relocations[0].kind == OBJECT_RELOCATION_AARCH64_PREL32);
    }
    ObjectFile a64_mach_cfi_object = object_from_canonical_codegen_module(arguments->arena, &separate_program, &a64_cfi_module,
                                                                (Target){
                                                                    .cpu_arch = CPU_ARCH_AARCH64,
                                                                    .os = OPERATING_SYSTEM_MACOS,
                                                                });
    BUSTER_TEST(arguments, a64_mach_cfi_object.error == OBJECT_ERROR_NONE);
    ObjectArtifact a64_mach_cfi_artifact = object_write(arguments->arena, &a64_mach_cfi_object, OBJECT_FORMAT_MACH_O64);
    BUSTER_TEST(arguments, a64_mach_cfi_artifact.error == OBJECT_ERROR_NONE);
    u64 a64_mach_cfi_section_header = 32 + 72 + (u64)OBJECT_SECTION_UNWIND * 80;
    u32 a64_mach_cfi_flags = 0;
    if (a64_mach_cfi_section_header + 68 <= a64_mach_cfi_artifact.bytes.length)
    {
        memcpy(&a64_mach_cfi_flags, a64_mach_cfi_artifact.bytes.pointer + a64_mach_cfi_section_header + 64, sizeof(a64_mach_cfi_flags));
    }
    BUSTER_TEST(arguments, a64_mach_cfi_flags == 0x6800000b);
    ObjectFile a64_mach_cfi_roundtrip = object_read(arguments->arena, a64_mach_cfi_artifact.bytes, a64_mach_cfi_object.target);
    BUSTER_TEST(arguments, a64_mach_cfi_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, a64_mach_cfi_roundtrip.sections[OBJECT_SECTION_UNWIND].data.length ==
                               a64_mach_cfi_object.sections[OBJECT_SECTION_UNWIND].data.length);
    BUSTER_TEST(arguments, a64_mach_cfi_roundtrip.relocation_count == 1);
    if (a64_mach_cfi_roundtrip.relocation_count == 1)
    {
        BUSTER_TEST(arguments, a64_mach_cfi_roundtrip.relocations[0].section == OBJECT_SECTION_UNWIND);
        BUSTER_TEST(arguments, a64_mach_cfi_roundtrip.relocations[0].kind == OBJECT_RELOCATION_AARCH64_PREL32);
    }

    u32 a64_mach_prel_text_data[2] = {0};
    u32 a64_mach_prel_read_only_data[2] = {0};
    u32 a64_mach_prel_data_data[2] = {0};
    ObjectSection a64_mach_prel_sections[] = {
        {
            .name = S8(".text"),
            .data = BUSTER_ARRAY_TO_BYTE_SLICE(a64_mach_prel_text_data),
            .kind = OBJECT_SECTION_TEXT,
            .alignment = 4,
        },
        {
            .name = S8(".rodata"),
            .data = BUSTER_ARRAY_TO_BYTE_SLICE(a64_mach_prel_read_only_data),
            .kind = OBJECT_SECTION_READ_ONLY_DATA,
            .alignment = 4,
        },
        {
            .name = S8(".data"),
            .data = BUSTER_ARRAY_TO_BYTE_SLICE(a64_mach_prel_data_data),
            .kind = OBJECT_SECTION_DATA,
            .alignment = 4,
        },
    };
    ObjectSymbol a64_mach_prel_symbols[] = {
        {
            .name = S8("prel_text_target"),
            .section = OBJECT_SECTION_TEXT,
            .size = sizeof(u32),
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
        {
            .name = S8("prel_read_only_target"),
            .section = OBJECT_SECTION_READ_ONLY_DATA,
            .size = sizeof(u32),
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
        {
            .name = S8("prel_data_target"),
            .section = OBJECT_SECTION_DATA,
            .size = sizeof(u32),
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
    };
    s64 a64_mach_prel_addends[] = {17, -23, 4096};
    ObjectRelocation a64_mach_prel_relocations[] = {
        {
            .addend = a64_mach_prel_addends[0],
            .offset = sizeof(u32),
            .section = OBJECT_SECTION_TEXT,
            .symbol = 1,
            .kind = OBJECT_RELOCATION_AARCH64_PREL32,
        },
        {
            .addend = a64_mach_prel_addends[1],
            .offset = sizeof(u32),
            .section = OBJECT_SECTION_READ_ONLY_DATA,
            .symbol = 2,
            .kind = OBJECT_RELOCATION_AARCH64_PREL32,
        },
        {
            .addend = a64_mach_prel_addends[2],
            .offset = sizeof(u32),
            .section = OBJECT_SECTION_DATA,
            .symbol = 0,
            .kind = OBJECT_RELOCATION_AARCH64_PREL32,
        },
    };
    ObjectFile a64_mach_prel_object = {
        .sections = a64_mach_prel_sections,
        .section_count = BUSTER_ARRAY_LENGTH(a64_mach_prel_sections),
        .symbols = a64_mach_prel_symbols,
        .symbol_count = BUSTER_ARRAY_LENGTH(a64_mach_prel_symbols),
        .relocations = a64_mach_prel_relocations,
        .relocation_count = BUSTER_ARRAY_LENGTH(a64_mach_prel_relocations),
        .target = {
            .cpu_arch = CPU_ARCH_AARCH64,
            .os = OPERATING_SYSTEM_MACOS,
        },
    };
    ObjectArtifact a64_mach_prel_artifact = object_write(arguments->arena, &a64_mach_prel_object, OBJECT_FORMAT_MACH_O64);
    BUSTER_TEST(arguments, a64_mach_prel_artifact.error == OBJECT_ERROR_NONE);
    // LLVM's AArch64 Mach-O PREL32 lowering is the SUBTRACTOR/UNSIGNED pair
    // checked below: both records are external, non-PC-relative, and 32-bit.
    bool a64_mach_prel_shape_valid = true;
    for (u32 section_index = 0; section_index < BUSTER_ARRAY_LENGTH(a64_mach_prel_sections); section_index += 1)
    {
        u32 raw_offset = 0;
        u32 relocation_offset = 0;
        u32 relocation_count = 0;
        bool offsets_valid = object_test_mach_section_offsets(a64_mach_prel_artifact.bytes, section_index, &raw_offset, &relocation_offset,
                                                               &relocation_count);
        a64_mach_prel_shape_valid &= offsets_valid && relocation_count == 2;
        if (offsets_valid && relocation_count == 2)
        {
            u32 first_source_offset = 0;
            u32 first_information = 0;
            u32 second_source_offset = 0;
            u32 second_information = 0;
            memcpy(&first_source_offset, a64_mach_prel_artifact.bytes.pointer + relocation_offset, sizeof(first_source_offset));
            memcpy(&first_information, a64_mach_prel_artifact.bytes.pointer + relocation_offset + 4, sizeof(first_information));
            memcpy(&second_source_offset, a64_mach_prel_artifact.bytes.pointer + relocation_offset + 8, sizeof(second_source_offset));
            memcpy(&second_information, a64_mach_prel_artifact.bytes.pointer + relocation_offset + 12, sizeof(second_information));
            a64_mach_prel_shape_valid &= first_source_offset == sizeof(u32) && second_source_offset == sizeof(u32) &&
                                         (first_information >> 28) == 1 && ((first_information >> 25) & 3) == 2 &&
                                         (first_information & (1u << 27)) && !(first_information & (1u << 24)) &&
                                         (second_information >> 28) == 0 && ((second_information >> 25) & 3) == 2 &&
                                         (second_information & (1u << 27)) && !(second_information & (1u << 24));
        }
    }
    BUSTER_TEST(arguments, a64_mach_prel_shape_valid);
    ObjectFile a64_mach_prel_roundtrip = object_read(arguments->arena, a64_mach_prel_artifact.bytes, a64_mach_prel_object.target);
    BUSTER_TEST(arguments, a64_mach_prel_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, a64_mach_prel_roundtrip.relocation_count == BUSTER_ARRAY_LENGTH(a64_mach_prel_relocations));
    ObjectSectionKind a64_mach_prel_expected_sections[] = {
        OBJECT_SECTION_TEXT,
        OBJECT_SECTION_READ_ONLY_DATA,
        OBJECT_SECTION_DATA,
    };
    String8 a64_mach_prel_expected_symbols[] = {
        S8("prel_read_only_target"),
        S8("prel_data_target"),
        S8("prel_text_target"),
    };
    if (a64_mach_prel_roundtrip.error == OBJECT_ERROR_NONE &&
        a64_mach_prel_roundtrip.relocation_count == BUSTER_ARRAY_LENGTH(a64_mach_prel_relocations))
    {
        for (u32 relocation_index = 0; relocation_index < BUSTER_ARRAY_LENGTH(a64_mach_prel_relocations); relocation_index += 1)
        {
            ObjectRelocation* roundtrip_relocation = &a64_mach_prel_roundtrip.relocations[relocation_index];
            BUSTER_TEST(arguments, roundtrip_relocation->section == (u32)a64_mach_prel_expected_sections[relocation_index]);
            BUSTER_TEST(arguments, roundtrip_relocation->offset == sizeof(u32));
            BUSTER_TEST(arguments, roundtrip_relocation->kind == OBJECT_RELOCATION_AARCH64_PREL32);
            BUSTER_TEST(arguments, roundtrip_relocation->addend == a64_mach_prel_addends[relocation_index]);
            BUSTER_TEST(arguments, roundtrip_relocation->symbol < a64_mach_prel_roundtrip.symbol_count);
            if (roundtrip_relocation->symbol < a64_mach_prel_roundtrip.symbol_count)
            {
                BUSTER_STRING_TEST(arguments, a64_mach_prel_roundtrip.symbols[roundtrip_relocation->symbol].name,
                                   a64_mach_prel_expected_symbols[relocation_index]);
            }
        }
    }
    for (u32 malformed_kind = 0; malformed_kind < 13; malformed_kind += 1)
    {
        TemporalArena malformed_scope = arena_begin_temporal(arguments->arena);
        ByteSlice malformed = {
            .pointer = arena_allocate(arguments->arena, u8, a64_mach_prel_artifact.bytes.length),
            .length = a64_mach_prel_artifact.bytes.length,
        };
        memcpy(malformed.pointer, a64_mach_prel_artifact.bytes.pointer, malformed.length);
        u32 raw_offset = 0;
        u32 relocation_offset = 0;
        u32 relocation_count = 0;
        bool offsets_valid = object_test_mach_section_offsets(malformed, 0, &raw_offset, &relocation_offset, &relocation_count);
        if (offsets_valid && relocation_count == 2)
        {
            u32 first_information = 0;
            u32 second_information = 0;
            memcpy(&first_information, malformed.pointer + relocation_offset + 4, sizeof(first_information));
            memcpy(&second_information, malformed.pointer + relocation_offset + 12, sizeof(second_information));
            switch (malformed_kind)
            {
                case 0:
                    object_test_write_u32(malformed, relocation_offset + 4, first_information & ~(1u << 27));
                    break;
                case 1:
                    object_test_write_u32(malformed, relocation_offset + 4, first_information | (1u << 24));
                    break;
                case 2:
                    object_test_write_u32(malformed, relocation_offset + 12, second_information & ~(1u << 27));
                    break;
                case 3:
                    object_test_write_u32(malformed, relocation_offset + 12, second_information | (1u << 24));
                    break;
                case 4:
                    object_test_write_u32(malformed, relocation_offset + 12,
                                          (second_information & ~(0xfu << 28)) | (2u << 28));
                    break;
                case 5:
                    object_test_write_u32(malformed, relocation_offset + 12,
                                          (second_information & ~(3u << 25)) | (3u << 25));
                    break;
                case 6:
                    object_test_write_u32(malformed, relocation_offset + 8, sizeof(u32) * 2);
                    break;
                case 7:
                {
                    u32 first_source_offset = 0;
                    u32 first_word = 0;
                    u32 second_source_offset = 0;
                    u32 second_word = 0;
                    memcpy(&first_source_offset, malformed.pointer + relocation_offset, sizeof(first_source_offset));
                    memcpy(&first_word, malformed.pointer + relocation_offset + 4, sizeof(first_word));
                    memcpy(&second_source_offset, malformed.pointer + relocation_offset + 8, sizeof(second_source_offset));
                    memcpy(&second_word, malformed.pointer + relocation_offset + 12, sizeof(second_word));
                    object_test_write_u32(malformed, relocation_offset, second_source_offset);
                    object_test_write_u32(malformed, relocation_offset + 4, second_word);
                    object_test_write_u32(malformed, relocation_offset + 8, first_source_offset);
                    object_test_write_u32(malformed, relocation_offset + 12, first_word);
                    break;
                }
                case 8:
                {
                    u64 symbol_offset = object_test_mach_symbol_offset(malformed, BUSTER_ARRAY_LENGTH(a64_mach_prel_symbols));
                    if (symbol_offset != UINT64_MAX)
                    {
                        object_test_write_u64(malformed, symbol_offset + 8, 8);
                    }
                    break;
                }
                case 9:
                {
                    u64 symbol_offset = object_test_mach_symbol_offset(malformed, BUSTER_ARRAY_LENGTH(a64_mach_prel_symbols));
                    if (symbol_offset != UINT64_MAX && symbol_offset + 6 <= malformed.length)
                    {
                        malformed.pointer[symbol_offset + 5] = OBJECT_SECTION_READ_ONLY_DATA + 1;
                    }
                    break;
                }
                case 10:
                    object_test_write_u32(malformed, 32 + 72 + 60, 1);
                    break;
                case 11:
                    // A bare arm64 32-bit UNSIGNED record is not a PREL32
                    // pair and must not be accepted as ABS32.
                    object_test_write_u32(malformed, relocation_offset + 4, (first_information & ~(0xfu << 28)) | (0u << 28));
                    break;
                case 12:
                    // PREL32 fields are 4-byte objects; a pair at an
                    // unaligned offset is malformed even when its records
                    // otherwise have the canonical shape.
                    object_test_write_u32(malformed, relocation_offset, 2);
                    object_test_write_u32(malformed, relocation_offset + 8, 2);
                    break;
            }
        }
        ObjectFile malformed_result = object_read(arguments->arena, malformed, a64_mach_prel_object.target);
        BUSTER_TEST(arguments, offsets_valid && malformed_result.error != OBJECT_ERROR_NONE);
        arena_set_position(arguments->arena, malformed_scope.position);
    }
    // The Mach-O writer owns the serialized PREL32 word.  Verify that it
    // overwrites stale input for both zero and nonzero addends.
    u32 saved_a64_mach_prel_text = a64_mach_prel_text_data[1];
    u32 saved_a64_mach_prel_read_only = a64_mach_prel_read_only_data[1];
    u32 saved_a64_mach_prel_data = a64_mach_prel_data_data[1];
    s64 saved_a64_mach_prel_addend = a64_mach_prel_relocations[0].addend;
    a64_mach_prel_text_data[1] = UINT32_C(0xdeadbeef);
    a64_mach_prel_read_only_data[1] = UINT32_C(0xcafebabe);
    a64_mach_prel_data_data[1] = UINT32_C(0xfeedface);
    a64_mach_prel_relocations[0].addend = 0;
    ObjectArtifact a64_mach_prel_zero_artifact = object_write(arguments->arena, &a64_mach_prel_object, OBJECT_FORMAT_MACH_O64);
    u32 zero_raw_offset = 0;
    u32 ignored_relocation_offset = 0;
    u32 ignored_relocation_count = 0;
    bool zero_offsets_valid = object_test_mach_section_offsets(a64_mach_prel_zero_artifact.bytes, OBJECT_SECTION_TEXT, &zero_raw_offset,
                                                                &ignored_relocation_offset, &ignored_relocation_count);
    u32 zero_slot = UINT32_MAX;
    if (zero_offsets_valid && (u64)zero_raw_offset + sizeof(u32) * 2 <= a64_mach_prel_zero_artifact.bytes.length)
    {
        memcpy(&zero_slot, a64_mach_prel_zero_artifact.bytes.pointer + zero_raw_offset + sizeof(u32), sizeof(zero_slot));
    }
    BUSTER_TEST(arguments, a64_mach_prel_zero_artifact.error == OBJECT_ERROR_NONE && zero_offsets_valid && zero_slot == 0);
    ObjectFile a64_mach_prel_zero_roundtrip = object_read(arguments->arena, a64_mach_prel_zero_artifact.bytes, a64_mach_prel_object.target);
    BUSTER_TEST(arguments, a64_mach_prel_zero_roundtrip.error == OBJECT_ERROR_NONE && a64_mach_prel_zero_roundtrip.relocation_count == 3 &&
                               a64_mach_prel_zero_roundtrip.relocations[0].addend == 0);
    a64_mach_prel_relocations[0].addend = INT32_MAX;
    a64_mach_prel_relocations[1].addend = INT32_MIN;
    a64_mach_prel_relocations[2].addend = 0;
    ObjectArtifact a64_mach_prel_boundary_artifact = object_write(arguments->arena, &a64_mach_prel_object, OBJECT_FORMAT_MACH_O64);
    BUSTER_TEST(arguments, a64_mach_prel_boundary_artifact.error == OBJECT_ERROR_NONE);
    ObjectFile a64_mach_prel_boundary_roundtrip = object_read(arguments->arena, a64_mach_prel_boundary_artifact.bytes, a64_mach_prel_object.target);
    BUSTER_TEST(arguments, a64_mach_prel_boundary_roundtrip.error == OBJECT_ERROR_NONE && a64_mach_prel_boundary_roundtrip.relocation_count == 3 &&
                               a64_mach_prel_boundary_roundtrip.relocations[0].addend == INT32_MAX &&
                               a64_mach_prel_boundary_roundtrip.relocations[1].addend == INT32_MIN &&
                               a64_mach_prel_boundary_roundtrip.relocations[2].addend == 0);
    s64 invalid_a64_mach_prel_addends[] = {(s64)INT32_MIN - 1, (s64)INT32_MAX + 1};
    for (u32 addend_index = 0; addend_index < BUSTER_ARRAY_LENGTH(invalid_a64_mach_prel_addends); addend_index += 1)
    {
        a64_mach_prel_relocations[0].addend = invalid_a64_mach_prel_addends[addend_index];
        ObjectArtifact invalid_prel_artifact = object_write(arguments->arena, &a64_mach_prel_object, OBJECT_FORMAT_MACH_O64);
        BUSTER_TEST(arguments, invalid_prel_artifact.error == OBJECT_ERROR_UNSUPPORTED_TARGET);
    }
    ObjectRelocation misaligned_a64_mach_prel_relocation = a64_mach_prel_relocations[0];
    misaligned_a64_mach_prel_relocation.offset = 2;
    ObjectFile misaligned_a64_mach_prel_object = a64_mach_prel_object;
    misaligned_a64_mach_prel_object.relocations = &misaligned_a64_mach_prel_relocation;
    misaligned_a64_mach_prel_object.relocation_count = 1;
    misaligned_a64_mach_prel_relocation.addend = 0;
    BUSTER_TEST(arguments, object_write(arguments->arena, &misaligned_a64_mach_prel_object, OBJECT_FORMAT_MACH_O64).error == OBJECT_ERROR_INVALID_INPUT);
    a64_mach_prel_text_data[1] = saved_a64_mach_prel_text;
    a64_mach_prel_read_only_data[1] = saved_a64_mach_prel_read_only;
    a64_mach_prel_data_data[1] = saved_a64_mach_prel_data;
    a64_mach_prel_relocations[0].addend = saved_a64_mach_prel_addend;
    a64_mach_prel_relocations[1].addend = -23;
    a64_mach_prel_relocations[2].addend = 4096;
    u8 compact_x64_code[] = {
        0x55, 0x48, 0x89, 0xe5, 0xc3,
    };
    CodegenFunctionDescriptor compact_x64_descriptor = {0};
    BUSTER_TEST(arguments, object_mach_compact_decode(arguments->arena, (ByteSlice)BUSTER_ARRAY_TO_SLICE(compact_x64_code), 0,
                                                      (u32)sizeof(compact_x64_code), 0x01000000,
                                                      (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_MACOS}, &compact_x64_descriptor));
    BUSTER_TEST(arguments, compact_x64_descriptor.unwind_action_count == 2 && compact_x64_descriptor.prolog_size == 4);
    BUSTER_TEST(arguments, !object_mach_compact_decode(arguments->arena, (ByteSlice)BUSTER_ARRAY_TO_SLICE(compact_x64_code), 0,
                                                       (u32)sizeof(compact_x64_code), 0x01000001,
                                                       (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_MACOS}, &compact_x64_descriptor));
    u32 compact_a64_code[] = {
        0xd10083ff, 0xa9017bfd, 0x910043fd, 0xd65f03c0,
    };
    CodegenFunctionDescriptor compact_a64_descriptor = {0};
    BUSTER_TEST(arguments, object_mach_compact_decode(arguments->arena,
                                                      (ByteSlice){.pointer = (u8*)compact_a64_code, .length = sizeof(compact_a64_code)}, 0,
                                                      (u32)sizeof(compact_a64_code), 0x04000000,
                                                      (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS}, &compact_a64_descriptor));
    BUSTER_TEST(arguments, compact_a64_descriptor.unwind_action_count == 4 && compact_a64_descriptor.prolog_size == 12);
    DwarfCfiResult compact_a64_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                                           .functions = &compact_a64_descriptor,
                                                                           .target = {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS},
                                                                           .function_count = 1,
                                                                       });
    BUSTER_TEST(arguments, compact_a64_cfi.valid);
    u32 compact_a64_saved_code[] = {
        0xa9be6ffc, 0xa9017bfd, 0x910043fd, 0xd65f03c0,
    };
    BUSTER_TEST(arguments, object_mach_compact_decode(arguments->arena,
                                                      (ByteSlice){.pointer = (u8*)compact_a64_saved_code, .length = sizeof(compact_a64_saved_code)}, 0,
                                                      (u32)sizeof(compact_a64_saved_code), 0x04000010,
                                                      (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS}, &compact_a64_descriptor));
    BUSTER_TEST(arguments, compact_a64_descriptor.unwind_action_count == 6);
    u32 compact_a64_leaf_code[] = {
        0xd10083ff, 0xd65f03c0,
    };
    BUSTER_TEST(arguments, object_mach_compact_decode(arguments->arena,
                                                      (ByteSlice){.pointer = (u8*)compact_a64_leaf_code, .length = sizeof(compact_a64_leaf_code)}, 0,
                                                      (u32)sizeof(compact_a64_leaf_code), 0x02002000,
                                                      (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS}, &compact_a64_descriptor));
    BUSTER_TEST(arguments, compact_a64_descriptor.unwind_action_count == 1 && compact_a64_descriptor.prolog_size == 4);
    BUSTER_TEST(arguments, !object_mach_compact_decode(arguments->arena,
                                                       (ByteSlice){.pointer = (u8*)compact_a64_code, .length = sizeof(compact_a64_code)}, 0,
                                                       (u32)sizeof(compact_a64_code), 0x05000000,
                                                       (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS}, &compact_a64_descriptor));
    u8 compact_x64_entry[32] = {0};
    u32 compact_x64_size = (u32)sizeof(compact_x64_code);
    u32 compact_x64_encoding = 0x01000000;
    memcpy(compact_x64_entry + 8, &compact_x64_size, sizeof(compact_x64_size));
    memcpy(compact_x64_entry + 12, &compact_x64_encoding, sizeof(compact_x64_encoding));
    ObjectSection* compact_x64_sections = arena_allocate(arguments->arena, ObjectSection, OBJECT_SECTION_COUNT);
    memcpy(compact_x64_sections, mach_cfi_object.sections, sizeof(*compact_x64_sections) * OBJECT_SECTION_COUNT);
    compact_x64_sections[OBJECT_SECTION_TEXT].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(compact_x64_code);
    compact_x64_sections[OBJECT_SECTION_READ_ONLY_DATA].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(compact_x64_entry);
    compact_x64_sections[OBJECT_SECTION_UNWIND].data = (ByteSlice){0};
    ObjectSymbol compact_x64_symbol = {
        .name = S8("compact_x64"),
        .size = sizeof(compact_x64_code),
        .section = OBJECT_SECTION_TEXT,
        .kind = OBJECT_SYMBOL_FUNCTION,
        .global = true,
    };
    ObjectRelocation compact_x64_relocation = {
        .section = OBJECT_SECTION_READ_ONLY_DATA,
        .symbol = 0,
        .kind = OBJECT_RELOCATION_ABSOLUTE64,
    };
    ObjectFile compact_x64_object = mach_cfi_object;
    compact_x64_object.sections = compact_x64_sections;
    compact_x64_object.symbols = &compact_x64_symbol;
    compact_x64_object.relocations = &compact_x64_relocation;
    compact_x64_object.symbol_count = 1;
    compact_x64_object.relocation_count = 1;
    ObjectArtifact compact_x64_artifact = object_write(arguments->arena, &compact_x64_object, OBJECT_FORMAT_MACH_O64);
    BUSTER_TEST(arguments, compact_x64_artifact.error == OBJECT_ERROR_NONE && object_test_mach_compact_section_rewrite(compact_x64_artifact.bytes));
    ObjectFile compact_x64_roundtrip = object_read(arguments->arena, compact_x64_artifact.bytes, compact_x64_object.target);
    BUSTER_TEST(arguments, compact_x64_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, compact_x64_roundtrip.sections[OBJECT_SECTION_UNWIND].data.length > 0);
    BUSTER_TEST(arguments, compact_x64_roundtrip.relocation_count == 1 &&
                               compact_x64_roundtrip.relocations[0].kind == OBJECT_RELOCATION_X86_64_PC32);

    u8 compact_a64_entry[32] = {0};
    u32 compact_a64_size = (u32)sizeof(compact_a64_code);
    u32 compact_a64_encoding = 0x04000000;
    memcpy(compact_a64_entry + 8, &compact_a64_size, sizeof(compact_a64_size));
    memcpy(compact_a64_entry + 12, &compact_a64_encoding, sizeof(compact_a64_encoding));
    ObjectSection* compact_a64_sections = arena_allocate(arguments->arena, ObjectSection, OBJECT_SECTION_COUNT);
    memcpy(compact_a64_sections, a64_mach_cfi_object.sections, sizeof(*compact_a64_sections) * OBJECT_SECTION_COUNT);
    compact_a64_sections[OBJECT_SECTION_TEXT].data = (ByteSlice){.pointer = (u8*)compact_a64_code, .length = sizeof(compact_a64_code)};
    compact_a64_sections[OBJECT_SECTION_READ_ONLY_DATA].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(compact_a64_entry);
    compact_a64_sections[OBJECT_SECTION_UNWIND].data = (ByteSlice){0};
    ObjectSymbol compact_a64_symbol = {
        .name = S8("compact_a64"),
        .size = sizeof(compact_a64_code),
        .section = OBJECT_SECTION_TEXT,
        .kind = OBJECT_SYMBOL_FUNCTION,
        .global = true,
    };
    ObjectRelocation compact_a64_relocation = {
        .section = OBJECT_SECTION_READ_ONLY_DATA,
        .symbol = 0,
        .kind = OBJECT_RELOCATION_ABSOLUTE64,
    };
    ObjectFile compact_a64_object = a64_mach_cfi_object;
    compact_a64_object.sections = compact_a64_sections;
    compact_a64_object.symbols = &compact_a64_symbol;
    compact_a64_object.relocations = &compact_a64_relocation;
    compact_a64_object.symbol_count = 1;
    compact_a64_object.relocation_count = 1;
    ObjectArtifact compact_a64_artifact = object_write(arguments->arena, &compact_a64_object, OBJECT_FORMAT_MACH_O64);
    BUSTER_TEST(arguments, compact_a64_artifact.error == OBJECT_ERROR_NONE && object_test_mach_compact_section_rewrite(compact_a64_artifact.bytes));
    ObjectFile compact_a64_roundtrip = object_read(arguments->arena, compact_a64_artifact.bytes, compact_a64_object.target);
    BUSTER_TEST(arguments, compact_a64_roundtrip.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, compact_a64_roundtrip.sections[OBJECT_SECTION_UNWIND].data.length > 0);
    BUSTER_TEST(arguments, compact_a64_roundtrip.relocation_count == 1 &&
                               compact_a64_roundtrip.relocations[0].kind == OBJECT_RELOCATION_AARCH64_PREL32);
    CodegenFunctionDescriptor invalid_function = separate_function;
    invalid_function.prolog_size = invalid_function.code_size + 1;
    CodegenModule invalid_module = separate_module;
    invalid_module.functions = &invalid_function;
    ObjectFile invalid_object = object_from_canonical_codegen_module(arguments->arena, &separate_program, &invalid_module,
                                                           (Target){
                                                               .cpu_arch = CPU_ARCH_X86_64,
                                                               .os = OPERATING_SYSTEM_LINUX,
                                                           });
    BUSTER_TEST(arguments, invalid_object.error == OBJECT_ERROR_INVALID_INPUT);

    Target x86_linux_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    {
        TemporalArena mutation_scope = arena_begin_temporal(arguments->arena);
        ByteSlice mutation = {
            .pointer = arena_allocate(arguments->arena, u8, elf.bytes.length),
            .length = elf.bytes.length,
        };
        memcpy(mutation.pointer, elf.bytes.pointer, elf.bytes.length);
        object_test_write_u64(mutation, 40, UINT64_MAX);
        BUSTER_TEST(arguments, object_read(arguments->arena, mutation, x86_linux_target).error != OBJECT_ERROR_NONE);
        arena_set_position(arguments->arena, mutation_scope.position);
    }
    {
        u64 section_table = 0;
        u16 section_count = 0;
        u16 section_string_index = 0;
        memcpy(&section_table, elf.bytes.pointer + 40, sizeof(section_table));
        memcpy(&section_count, elf.bytes.pointer + 60, sizeof(section_count));
        memcpy(&section_string_index, elf.bytes.pointer + 62, sizeof(section_string_index));
        if (section_string_index < section_count)
        {
            TemporalArena mutation_scope = arena_begin_temporal(arguments->arena);
            ByteSlice mutation = {
                .pointer = arena_allocate(arguments->arena, u8, elf.bytes.length),
                .length = elf.bytes.length,
            };
            memcpy(mutation.pointer, elf.bytes.pointer, elf.bytes.length);
            object_test_write_u32(mutation, section_table + (u64)section_string_index * 64, UINT32_MAX);
            BUSTER_TEST(arguments, object_read(arguments->arena, mutation, x86_linux_target).error != OBJECT_ERROR_NONE);
            arena_set_position(arguments->arena, mutation_scope.position);
        }
    }
    {
        u64 section_table = 0;
        u16 section_count = 0;
        memcpy(&section_table, elf.bytes.pointer + 40, sizeof(section_table));
        memcpy(&section_count, elf.bytes.pointer + 60, sizeof(section_count));
        for (u16 section_index = 0; section_index < section_count; section_index += 1)
        {
            u64 section = section_table + (u64)section_index * 64;
            u32 section_type = 0;
            memcpy(&section_type, elf.bytes.pointer + section + 4, sizeof(section_type));
            if (section_type == 8)
            {
                TemporalArena mutation_scope = arena_begin_temporal(arguments->arena);
                ByteSlice mutation = {
                    .pointer = arena_allocate(arguments->arena, u8, elf.bytes.length),
                    .length = elf.bytes.length,
                };
                memcpy(mutation.pointer, elf.bytes.pointer, elf.bytes.length);
                object_test_write_u64(mutation, section + 32, UINT64_MAX);
                BUSTER_TEST(arguments, object_read(arguments->arena, mutation, x86_linux_target).error != OBJECT_ERROR_NONE);
                arena_set_position(arguments->arena, mutation_scope.position);
                break;
            }
        }
    }
    {
        u64 section_table = 0;
        u16 section_count = 0;
        memcpy(&section_table, elf.bytes.pointer + 40, sizeof(section_table));
        memcpy(&section_count, elf.bytes.pointer + 60, sizeof(section_count));
        for (u16 section_index = 0; section_index < section_count; section_index += 1)
        {
            u64 section = section_table + (u64)section_index * 64;
            u32 section_type = 0;
            memcpy(&section_type, elf.bytes.pointer + section + 4, sizeof(section_type));
            if (section_type == 4)
            {
                TemporalArena mutation_scope = arena_begin_temporal(arguments->arena);
                ByteSlice mutation = {
                    .pointer = arena_allocate(arguments->arena, u8, elf.bytes.length),
                    .length = elf.bytes.length,
                };
                memcpy(mutation.pointer, elf.bytes.pointer, elf.bytes.length);
                object_test_write_u64(mutation, section + 24, UINT64_MAX);
                BUSTER_TEST(arguments, object_read(arguments->arena, mutation, x86_linux_target).error != OBJECT_ERROR_NONE);
                arena_set_position(arguments->arena, mutation_scope.position);
                break;
            }
        }
    }
    {
        u64 section_table = 0;
        u16 section_count = 0;
        memcpy(&section_table, elf.bytes.pointer + 40, sizeof(section_table));
        memcpy(&section_count, elf.bytes.pointer + 60, sizeof(section_count));
        for (u16 section_index = 0; section_index < section_count; section_index += 1)
        {
            u64 section = section_table + (u64)section_index * 64;
            u32 section_type = 0;
            u64 symbol_offset = 0;
            u64 symbol_size = 0;
            memcpy(&section_type, elf.bytes.pointer + section + 4, sizeof(section_type));
            memcpy(&symbol_offset, elf.bytes.pointer + section + 24, sizeof(symbol_offset));
            memcpy(&symbol_size, elf.bytes.pointer + section + 32, sizeof(symbol_size));
            if (section_type == 2 && symbol_size >= 48)
            {
                TemporalArena mutation_scope = arena_begin_temporal(arguments->arena);
                ByteSlice mutation = {
                    .pointer = arena_allocate(arguments->arena, u8, elf.bytes.length),
                    .length = elf.bytes.length,
                };
                memcpy(mutation.pointer, elf.bytes.pointer, elf.bytes.length);
                object_test_write_u16(mutation, symbol_offset + 24 + 6, 0x7fff);
                BUSTER_TEST(arguments, object_read(arguments->arena, mutation, x86_linux_target).error != OBJECT_ERROR_NONE);
                arena_set_position(arguments->arena, mutation_scope.position);
                break;
            }
        }
    }
    {
        u64 symbol = object_test_elf_symbol_offset(elf.bytes, 1);
        BUSTER_TEST(arguments, symbol != UINT64_MAX);
        if (symbol != UINT64_MAX)
        {
            u16 special_indexes[] = {0xfff1, 0xfff2, 0xffff};
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(special_indexes); index += 1)
            {
                TemporalArena mutation_scope = arena_begin_temporal(arguments->arena);
                ByteSlice mutation = {
                    .pointer = arena_allocate(arguments->arena, u8, elf.bytes.length),
                    .length = elf.bytes.length,
                };
                memcpy(mutation.pointer, elf.bytes.pointer, elf.bytes.length);
                object_test_write_u16(mutation, symbol + 6, special_indexes[index]);
                BUSTER_TEST(arguments, object_read(arguments->arena, mutation, x86_linux_target).error == OBJECT_ERROR_NONE);
                arena_set_position(arguments->arena, mutation_scope.position);
            }
        }
    }
    {
        u8 one_byte_text[] = {0xc3};
        ObjectSection one_byte_section = {
            .name = S8(".text"),
            .data = BUSTER_ARRAY_TO_SLICE(one_byte_text),
            .virtual_size = sizeof(one_byte_text),
            .kind = OBJECT_SECTION_TEXT,
            .alignment = 1,
        };
        ObjectSymbol one_byte_symbol = {
            .name = S8("one_byte"),
            .size = sizeof(one_byte_text),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        };
        ObjectFile one_byte_object = {
            .sections = &one_byte_section,
            .symbols = &one_byte_symbol,
            .target = x86_linux_target,
            .section_count = 1,
            .symbol_count = 1,
        };
        ObjectArtifact one_byte_elf = object_write(arguments->arena, &one_byte_object, OBJECT_FORMAT_ELF64);
        BUSTER_TEST(arguments, one_byte_elf.error == OBJECT_ERROR_NONE);
        u64 symbol = object_test_elf_symbol_offset(one_byte_elf.bytes, 1);
        BUSTER_TEST(arguments, symbol != UINT64_MAX);
        if (symbol != UINT64_MAX)
        {
            TemporalArena mutation_scope = arena_begin_temporal(arguments->arena);
            ByteSlice mutation = {
                .pointer = arena_allocate(arguments->arena, u8, one_byte_elf.bytes.length),
                .length = one_byte_elf.bytes.length,
            };
            memcpy(mutation.pointer, one_byte_elf.bytes.pointer, one_byte_elf.bytes.length);
            object_test_write_u64(mutation, symbol + 16, UINT64_MAX);
            BUSTER_TEST(arguments, object_read(arguments->arena, mutation, x86_linux_target).error != OBJECT_ERROR_NONE);
            arena_set_position(arguments->arena, mutation_scope.position);
        }
    }
    {
        TemporalArena mutation_scope = arena_begin_temporal(arguments->arena);
        ByteSlice mutation = {
            .pointer = arena_allocate(arguments->arena, u8, coff.bytes.length),
            .length = coff.bytes.length,
        };
        memcpy(mutation.pointer, coff.bytes.pointer, coff.bytes.length);
        object_test_write_u32(mutation, 20 + 16, UINT32_MAX);
        object_test_write_u32(mutation, 20 + 20, 0);
        BUSTER_TEST(arguments, object_read(arguments->arena, mutation, (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS}).error !=
                                   OBJECT_ERROR_NONE);
        arena_set_position(arguments->arena, mutation_scope.position);
    }
    {
        TemporalArena mutation_scope = arena_begin_temporal(arguments->arena);
        ByteSlice mutation = {
            .pointer = arena_allocate(arguments->arena, u8, coff.bytes.length),
            .length = coff.bytes.length,
        };
        memcpy(mutation.pointer, coff.bytes.pointer, coff.bytes.length);
        object_test_write_u32(mutation, 8, UINT32_MAX);
        BUSTER_TEST(arguments, object_read(arguments->arena, mutation, (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS}).error !=
                                   OBJECT_ERROR_NONE);
        arena_set_position(arguments->arena, mutation_scope.position);
    }
    {
        u32 symbol_offset = 0;
        u32 symbol_count = 0;
        memcpy(&symbol_offset, coff.bytes.pointer + 8, sizeof(symbol_offset));
        memcpy(&symbol_count, coff.bytes.pointer + 12, sizeof(symbol_count));
        if (symbol_count)
        {
            TemporalArena mutation_scope = arena_begin_temporal(arguments->arena);
            ByteSlice mutation = {
                .pointer = arena_allocate(arguments->arena, u8, coff.bytes.length),
                .length = coff.bytes.length,
            };
            memcpy(mutation.pointer, coff.bytes.pointer, coff.bytes.length);
            object_test_write_u32(mutation, symbol_offset, 0);
            object_test_write_u32(mutation, symbol_offset + 4, UINT32_MAX);
            object_test_write_u32(mutation, symbol_offset + 8, 0);
            object_test_write_u16(mutation, symbol_offset + 12, 1);
            BUSTER_TEST(arguments, object_read(arguments->arena, mutation, (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS}).error !=
                                       OBJECT_ERROR_NONE);
            arena_set_position(arguments->arena, mutation_scope.position);
        }
    }
    {
        u16 section_count = 0;
        u32 symbol_offset = 0;
        u32 symbol_count = 0;
        memcpy(&section_count, coff.bytes.pointer + 2, sizeof(section_count));
        memcpy(&symbol_offset, coff.bytes.pointer + 8, sizeof(symbol_offset));
        memcpy(&symbol_count, coff.bytes.pointer + 12, sizeof(symbol_count));
        u16 ignored_section_number = 0;
        u64 ignored_section = 0;
        for (u16 section_index = 0; section_index < section_count; section_index += 1)
        {
            u64 section = 20 + (u64)section_index * 40;
            if (memcmp(coff.bytes.pointer + section, ".bss", 4) == 0 && coff.bytes.pointer[section + 4] == 0)
            {
                ignored_section_number = section_index + 1;
                ignored_section = section;
                break;
            }
        }
        BUSTER_TEST(arguments, ignored_section_number != 0);
        if (ignored_section_number)
        {
            bool ignored_symbol = false;
            for (u32 symbol_index = 0; symbol_index < symbol_count; symbol_index += 1)
            {
                u16 symbol_section = 0;
                u64 symbol = (u64)symbol_offset + (u64)symbol_index * 18;
                memcpy(&symbol_section, coff.bytes.pointer + symbol + 12, sizeof(symbol_section));
                ignored_symbol |= symbol_section == ignored_section_number;
            }
            BUSTER_TEST(arguments, ignored_symbol);

            TemporalArena ignored_scope = arena_begin_temporal(arguments->arena);
            ByteSlice ignored_bytes = {
                .pointer = arena_allocate(arguments->arena, u8, coff.bytes.length),
                .length = coff.bytes.length,
            };
            memcpy(ignored_bytes.pointer, coff.bytes.pointer, coff.bytes.length);
            object_test_write_u32(ignored_bytes, ignored_section + 36, 0x02000800);
            ObjectFile ignored_object = object_read(arguments->arena, ignored_bytes,
                                                     (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS});
            BUSTER_TEST(arguments, ignored_object.error == OBJECT_ERROR_NONE);
            BUSTER_TEST(arguments, ignored_object.symbol_count < coff_roundtrip.symbol_count);
            arena_set_position(arguments->arena, ignored_scope.position);

            bool relocation_symbol = false;
            u32 relocation_symbol_index = 0;
            for (u16 section_index = 0; section_index < section_count && !relocation_symbol; section_index += 1)
            {
                if (section_index + 1 == ignored_section_number)
                {
                    continue;
                }
                u64 section = 20 + (u64)section_index * 40;
                u32 relocation_offset = 0;
                u16 relocation_count = 0;
                memcpy(&relocation_offset, coff.bytes.pointer + section + 24, sizeof(relocation_offset));
                memcpy(&relocation_count, coff.bytes.pointer + section + 32, sizeof(relocation_count));
                if (relocation_count)
                {
                    memcpy(&relocation_symbol_index, coff.bytes.pointer + relocation_offset + 4, sizeof(relocation_symbol_index));
                    relocation_symbol = relocation_symbol_index < symbol_count;
                }
            }
            BUSTER_TEST(arguments, relocation_symbol);
            if (relocation_symbol)
            {
                TemporalArena relocation_scope = arena_begin_temporal(arguments->arena);
                ByteSlice relocation_bytes = {
                    .pointer = arena_allocate(arguments->arena, u8, coff.bytes.length),
                    .length = coff.bytes.length,
                };
                memcpy(relocation_bytes.pointer, coff.bytes.pointer, coff.bytes.length);
                object_test_write_u32(relocation_bytes, ignored_section + 36, 0x02000800);
                object_test_write_u16(relocation_bytes, (u64)symbol_offset + (u64)relocation_symbol_index * 18 + 12, ignored_section_number);
                BUSTER_TEST(arguments, object_read(arguments->arena, relocation_bytes,
                                                   (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS})
                                                   .error != OBJECT_ERROR_NONE);
                arena_set_position(arguments->arena, relocation_scope.position);
            }
        }
    }
    {
        TemporalArena mutation_scope = arena_begin_temporal(arguments->arena);
        ByteSlice mutation = {
            .pointer = arena_allocate(arguments->arena, u8, mach.bytes.length),
            .length = mach.bytes.length,
        };
        memcpy(mutation.pointer, mach.bytes.pointer, mach.bytes.length);
        object_test_write_u32(mutation, 20, 8);
        BUSTER_TEST(arguments, object_read(arguments->arena, mutation, (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_MACOS}).error !=
                                   OBJECT_ERROR_NONE);
        arena_set_position(arguments->arena, mutation_scope.position);
    }
    {
        TemporalArena mutation_scope = arena_begin_temporal(arguments->arena);
        ByteSlice mutation = {
            .pointer = arena_allocate(arguments->arena, u8, mach.bytes.length),
            .length = mach.bytes.length,
        };
        memcpy(mutation.pointer, mach.bytes.pointer, mach.bytes.length);
        object_test_write_u32(mutation, 16, UINT32_MAX);
        BUSTER_TEST(arguments, object_read(arguments->arena, mutation, (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_MACOS}).error !=
                                   OBJECT_ERROR_NONE);
        arena_set_position(arguments->arena, mutation_scope.position);
    }
    {
        u32 command_count = 0;
        memcpy(&command_count, mach.bytes.pointer + 16, sizeof(command_count));
        u64 command = 32;
        for (u32 command_index = 0; command_index < command_count; command_index += 1)
        {
            u32 kind = 0;
            u32 command_size = 0;
            memcpy(&kind, mach.bytes.pointer + command, sizeof(kind));
            memcpy(&command_size, mach.bytes.pointer + command + 4, sizeof(command_size));
            if (kind == 2)
            {
                TemporalArena mutation_scope = arena_begin_temporal(arguments->arena);
                ByteSlice mutation = {
                    .pointer = arena_allocate(arguments->arena, u8, mach.bytes.length),
                    .length = mach.bytes.length,
                };
                memcpy(mutation.pointer, mach.bytes.pointer, mach.bytes.length);
                object_test_write_u32(mutation, command + 16, UINT32_MAX);
                BUSTER_TEST(arguments, object_read(arguments->arena, mutation, (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_MACOS}).error !=
                                           OBJECT_ERROR_NONE);
                arena_set_position(arguments->arena, mutation_scope.position);
                break;
            }
            command += command_size;
        }
    }
    {
        TemporalArena archive_scope = arena_begin_temporal(arguments->arena);
        ByteSlice normal_archive = object_test_archive(arguments->arena, elf.bytes, false);
        ObjectArchive normal = object_archive_read(arguments->arena, normal_archive, x86_linux_target);
        BUSTER_TEST(arguments, normal.error == OBJECT_ERROR_NONE && normal.object_count == 1);
        ByteSlice bsd_archive = object_test_archive(arguments->arena, elf.bytes, true);
        ObjectArchive bsd = object_archive_read(arguments->arena, bsd_archive, x86_linux_target);
        BUSTER_TEST(arguments, bsd.error == OBJECT_ERROR_NONE && bsd.object_count == 1);
        ByteSlice long_name_archive = object_test_archive_long_name(arguments->arena, elf.bytes);
        ObjectArchive long_name = object_archive_read(arguments->arena, long_name_archive, x86_linux_target);
        BUSTER_TEST(arguments, long_name.error == OBJECT_ERROR_NONE && long_name.object_count == 1);
        if (long_name.error == OBJECT_ERROR_NONE && long_name.object_count == 1)
        {
            BUSTER_STRING_TEST(arguments, long_name.member_names[0], S8("long-member-name.o"));
        }
        {
            u64 long_table_size = S8("long-member-name.o/\n").length;
            u64 object_header = 8 + 60 + long_table_size + (long_table_size & 1);
            TemporalArena boundary_scope = arena_begin_temporal(arguments->arena);
            ByteSlice bad_boundary = {
                .pointer = arena_allocate(arguments->arena, u8, long_name_archive.length),
                .length = long_name_archive.length,
            };
            memcpy(bad_boundary.pointer, long_name_archive.pointer, long_name_archive.length);
            bad_boundary.pointer[object_header] = '/';
            bad_boundary.pointer[object_header + 1] = '1';
            BUSTER_TEST(arguments, object_archive_read(arguments->arena, bad_boundary, x86_linux_target).error != OBJECT_ERROR_NONE);
            arena_set_position(arguments->arena, boundary_scope.position);
        }
        TemporalArena long_name_scope = arena_begin_temporal(arguments->arena);
        ByteSlice bad_long_name = {
            .pointer = arena_allocate(arguments->arena, u8, long_name_archive.length),
            .length = long_name_archive.length,
        };
        memcpy(bad_long_name.pointer, long_name_archive.pointer, long_name_archive.length);
        bad_long_name.pointer[8 + 60 + S8("long-member-name.o/\n").length - 1] = 'x';
        BUSTER_TEST(arguments, object_archive_read(arguments->arena, bad_long_name, x86_linux_target).error != OBJECT_ERROR_NONE);
        arena_set_position(arguments->arena, long_name_scope.position);
        ByteSlice padded_archive = elf.bytes.length & 1 ? normal_archive : bsd_archive;
        TemporalArena padding_scope = arena_begin_temporal(arguments->arena);
        ByteSlice bad_padding = {
            .pointer = arena_allocate(arguments->arena, u8, padded_archive.length),
            .length = padded_archive.length,
        };
        memcpy(bad_padding.pointer, padded_archive.pointer, padded_archive.length);
        bad_padding.pointer[bad_padding.length - 1] = 0;
        BUSTER_TEST(arguments, object_archive_read(arguments->arena, bad_padding, x86_linux_target).error != OBJECT_ERROR_NONE);
        arena_set_position(arguments->arena, padding_scope.position);
        TemporalArena size_scope = arena_begin_temporal(arguments->arena);
        ByteSlice bad_size = {
            .pointer = arena_allocate(arguments->arena, u8, normal_archive.length),
            .length = normal_archive.length,
        };
        memcpy(bad_size.pointer, normal_archive.pointer, normal_archive.length);
        memset(bad_size.pointer + 8 + 48, '9', 10);
        BUSTER_TEST(arguments, object_archive_read(arguments->arena, bad_size, x86_linux_target).error != OBJECT_ERROR_NONE);
        arena_set_position(arguments->arena, size_scope.position);
        ObjectArchive null_archive = object_archive_read(arguments->arena, (ByteSlice){.length = 8}, x86_linux_target);
        BUSTER_TEST(arguments, null_archive.error != OBJECT_ERROR_NONE);
        arena_set_position(arguments->arena, archive_scope.position);
    }
#if BUSTER_FUZZ_AVAILABLE
    {
        TemporalArena fuzz_scope = arena_begin_temporal(arguments->arena);
        u64 boundary_size = BUSTER_KB(64);
        u8* boundary_input = arena_allocate(arguments->arena, u8, boundary_size + 1);
        memset(boundary_input, 0, boundary_size + 1);
        BUSTER_TEST(arguments, object_fuzz_test_input(boundary_input, boundary_size) == 0);
        BUSTER_TEST(arguments, object_fuzz_test_input(boundary_input, boundary_size + 1) == -1);
        arena_set_position(arguments->arena, fuzz_scope.position);
    }
#endif
    return result;
}
#endif
