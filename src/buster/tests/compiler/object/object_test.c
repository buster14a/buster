#include <buster/tests/compiler/object/object_test.h>
#if BUSTER_INCLUDE_TESTS

BUSTER_GLOBAL_LOCAL bool object_bytes_contain(ByteSlice bytes, String8 value)
{
    if (value.length > bytes.length)
    {
        return false;
    }
    for (u64 index = 0; index + value.length <= bytes.length; index += 1)
    {
        if (memcmp(bytes.pointer + index, value.pointer, value.length) == 0)
        {
            return true;
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
    if (bytes.length < section + 80 || magic != 0xfeedfacf || command != 0x19)
    {
        return false;
    }
    memset(bytes.pointer + section, 0, 32);
    memcpy(bytes.pointer + section, "__compact_unwind", sizeof("__compact_unwind") - 1);
    memcpy(bytes.pointer + section + 16, "__LD", sizeof("__LD") - 1);
    u32 flags = 0x020000;
    memcpy(bytes.pointer + section + 64, &flags, sizeof(flags));
    return true;
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
    if (!bytes.pointer || bytes.length < 64)
    {
        return UINT64_MAX;
    }
    u64 section_table = 0;
    u16 section_count = 0;
    memcpy(&section_table, bytes.pointer + 40, sizeof(section_table));
    memcpy(&section_count, bytes.pointer + 60, sizeof(section_count));
    if (section_table > bytes.length || (u64)section_count * 64 > bytes.length - section_table)
    {
        return UINT64_MAX;
    }
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

UnitTestResult object_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {0};
    AnalysisResult missing_entities = {0};
    missing_entities.module.id.value = 7;
    missing_entities.module.entity_count = 1;
    BUSTER_TEST(arguments, !object_entity_find(&missing_entities, (AnalysisEntityId){.module.value = 7}));
    missing_entities.module.import_count = 1;
    BUSTER_TEST(arguments, !object_entity_find(&missing_entities, (AnalysisEntityId){.module.value = 8}));
    AnalysisImport missing_import_entities[1] = {0};
    AnalysisResult imported_missing_entities = {0};
    imported_missing_entities.module.id.value = 8;
    imported_missing_entities.module.entity_count = 1;
    missing_import_entities[0].target = &imported_missing_entities;
    missing_entities.module.imports = missing_import_entities;
    BUSTER_TEST(arguments, !object_entity_find(&missing_entities, (AnalysisEntityId){.module.value = 8}));
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
    BUSTER_TEST(arguments, elf_roundtrip.sections[OBJECT_SECTION_ZERO].data.length == 0);
    BUSTER_TEST(arguments, elf_roundtrip.sections[OBJECT_SECTION_ZERO].virtual_size == BUSTER_MB(1));
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
    AstCode defined_code = {
        .name = S8("defined_function"),
        .has_body = true,
    };
    AnalysisEntity defined_entity = {
        .name = defined_code.name,
        .id =
            {
                .module = {.value = 11},
                .index = {.value = 0},
            },
        .kind = ANALYSIS_ENTITY_CODE,
        .ast.code = &defined_code,
    };
    AnalysisResult separate_analysis = {
        .module =
            {
                .entities = &defined_entity,
                .id = {.value = 11},
                .entity_count = 1,
            },
    };
    u8 separate_code[] = {
        0xe8, 0, 0, 0, 0, 0xc3,
    };
    CodegenModuleEntry separate_entry = {
        .entity = defined_entity.id,
    };
    CodegenFunctionDescriptor separate_function = {
        .code_size = (u32)sizeof(separate_code),
    };
    CodegenModuleRelocation separate_relocation = {
        .entity =
            {
                .module = {.value = 12},
                .index = {.value = 3},
            },
        .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
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
    ObjectFile separate_object = object_from_codegen_module(arguments->arena, &separate_analysis, &separate_module,
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
        object_from_codegen_module(arguments->arena, &separate_analysis, &windows_unwind_module, windows_unwind_target);
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
        object_from_codegen_module(arguments->arena, &separate_analysis, &windows_arm64_module, windows_arm64_target);
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
    ObjectFile mach_cfi_object = object_from_codegen_module(arguments->arena, &separate_analysis, &mach_cfi_module,
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
    ObjectFile a64_cfi_object = object_from_codegen_module(arguments->arena, &separate_analysis, &a64_cfi_module,
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
    ObjectFile a64_mach_cfi_object = object_from_codegen_module(arguments->arena, &separate_analysis, &a64_cfi_module,
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
    ObjectFile invalid_object = object_from_codegen_module(arguments->arena, &separate_analysis, &invalid_module,
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
