#include <buster/tests/compiler/link/link_test.h>

BUSTER_GLOBAL_LOCAL String8 link_test_temporary_executable_path(Arena* arena, String8 name, String8 suffix)
{
#if BUSTER_ANDROID || BUSTER_IOS
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(name);
    BUSTER_UNUSED(suffix);
    return (String8){0};
#else
    return buster_test_temporary_path(arena, name, suffix);
#endif
}

BUSTER_GLOBAL_LOCAL ObjectFile link_test_object_make(Arena* arena, Target target, ByteSlice text, ObjectSymbol* symbols, u32 symbol_count,
                                                     ObjectRelocation* relocations, u32 relocation_count)
{
    ObjectSection* sections = arena_allocate(arena, ObjectSection, OBJECT_SECTION_COUNT);
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        sections[kind] = (ObjectSection){
            .name = object_section_name_for_kind((ObjectSectionKind)kind),
            .kind = (ObjectSectionKind)kind,
            .alignment = object_section_default_alignment((ObjectSectionKind)kind),
        };
    }
    sections[OBJECT_SECTION_TEXT].data = text;
    return (ObjectFile){
        .sections = sections,
        .symbols = symbols,
        .relocations = relocations,
        .target = target,
        .section_count = OBJECT_SECTION_COUNT,
        .symbol_count = symbol_count,
        .relocation_count = relocation_count,
    };
}

BUSTER_GLOBAL_LOCAL bool link_test_elf_section_find(ByteSlice image, String8 name, u32* section_index, u64* section_header)
{
    if (image.length < 64 || image.pointer[0] != 0x7f || image.pointer[1] != 'E' || image.pointer[2] != 'L' || image.pointer[3] != 'F')
    {
        return false;
    }
    u64 table_offset = link_read_u64(image.pointer, 40);
    u16 header_size = 0;
    u16 section_count = 0;
    u16 string_index = 0;
    memcpy(&header_size, image.pointer + 58, sizeof(header_size));
    memcpy(&section_count, image.pointer + 60, sizeof(section_count));
    memcpy(&string_index, image.pointer + 62, sizeof(string_index));
    if (!table_offset || header_size < 64 || string_index >= section_count || table_offset > image.length ||
        (u64)section_count > (image.length - table_offset) / header_size)
    {
        return false;
    }
    u64 string_header = table_offset + (u64)string_index * header_size;
    u64 string_offset = link_read_u64(image.pointer, string_header + 24);
    u64 string_size = link_read_u64(image.pointer, string_header + 32);
    if (string_offset > image.length || string_size > image.length - string_offset)
    {
        return false;
    }
    for (u32 index = 1; index < section_count; index += 1)
    {
        u64 header = table_offset + (u64)index * header_size;
        u32 name_offset = link_read_u32(image.pointer, header);
        if (name_offset >= string_size)
        {
            continue;
        }
        u64 length = 0;
        while ((u64)name_offset + length < string_size && image.pointer[string_offset + name_offset + length])
        {
            length += 1;
        }
        if ((u64)name_offset + length < string_size && string_equal(
                                                              (String8){
                                                                  .pointer = (char8*)image.pointer + string_offset + name_offset,
                                                                  .length = length,
                                                              },
                                                              name))
        {
            if (section_index)
            {
                *section_index = index;
            }
            if (section_header)
            {
                *section_header = header;
            }
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool link_test_mach_section_find(ByteSlice image, String8 segment_name, String8 section_name, u64* section_header)
{
    if (image.length < 32 || link_read_u32(image.pointer, 0) != 0xfeedfacf)
    {
        return false;
    }
    u32 command_count = link_read_u32(image.pointer, 16);
    u64 command = 32;
    for (u32 command_index = 0; command_index < command_count; command_index += 1)
    {
        if (command > image.length || 8 > image.length - command)
        {
            return false;
        }
        u32 kind = link_read_u32(image.pointer, command);
        u32 size = link_read_u32(image.pointer, command + 4);
        if (size < 8 || size > image.length - command)
        {
            return false;
        }
        if (kind == 0x19 && size >= 72)
        {
            u32 section_count = link_read_u32(image.pointer, command + 64);
            if ((u64)section_count > (size - 72) / 80)
            {
                return false;
            }
            for (u32 index = 0; index < section_count; index += 1)
            {
                u64 section = command + 72 + (u64)index * 80;
                String8 candidate_section = {.pointer = (char8*)image.pointer + section, .length = 0};
                String8 candidate_segment = {.pointer = (char8*)image.pointer + section + 16, .length = 0};
                while (candidate_section.length < 16 && candidate_section.pointer[candidate_section.length])
                {
                    candidate_section.length += 1;
                }
                while (candidate_segment.length < 16 && candidate_segment.pointer[candidate_segment.length])
                {
                    candidate_segment.length += 1;
                }
                if (string_equal(candidate_segment, segment_name) && string_equal(candidate_section, section_name))
                {
                    if (section_header)
                    {
                        *section_header = section;
                    }
                    return true;
                }
            }
        }
        command += size;
    }
    return false;
}

#if BUSTER_CPU_ARCH_X86_64
BUSTER_GLOBAL_LOCAL bool link_test_pe_import_matches(ByteSlice executable, String8 library, String8 symbol)
{
    if (executable.length < 0x40 || executable.pointer[0] != 'M' || executable.pointer[1] != 'Z')
    {
        return false;
    }
    u32 pe_offset = link_read_u32(executable.pointer, 0x3c);
    if (pe_offset > executable.length || executable.length - pe_offset < 24 || memcmp(executable.pointer + pe_offset, "PE\0\0", 4) != 0)
    {
        return false;
    }
    u16 section_count = 0;
    u16 optional_size = 0;
    memcpy(&section_count, executable.pointer + pe_offset + 6, sizeof(section_count));
    memcpy(&optional_size, executable.pointer + pe_offset + 20, sizeof(optional_size));
    u64 optional = pe_offset + 24;
    if (optional > executable.length || optional_size > executable.length - optional || optional_size < 128)
    {
        return false;
    }
    u32 import_rva = link_read_u32(executable.pointer, optional + 120);
    u64 section_table = optional + optional_size;
    u64 import_offset = 0;
    bool import_mapped = false;
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 section = section_table + (u64)section_index * 40;
        if (section > executable.length || 40 > executable.length - section)
        {
            return false;
        }
        u32 virtual_size = link_read_u32(executable.pointer, section + 8);
        u32 virtual_address = link_read_u32(executable.pointer, section + 12);
        u32 raw_size = link_read_u32(executable.pointer, section + 16);
        u32 raw_offset = link_read_u32(executable.pointer, section + 20);
        u64 span = BUSTER_MAX(virtual_size, raw_size);
        if (import_rva >= virtual_address && (u64)import_rva - virtual_address < span)
        {
            import_offset = raw_offset + ((u64)import_rva - virtual_address);
            import_mapped = import_offset < executable.length;
            break;
        }
    }
    if (!import_mapped)
    {
        return false;
    }
    for (u32 descriptor_index = 0; descriptor_index <= (u32)section_count + 2; descriptor_index += 1)
    {
        u64 descriptor = import_offset + (u64)descriptor_index * 20;
        if (descriptor > executable.length || 20 > executable.length - descriptor)
        {
            return false;
        }
        u32 lookup_rva = link_read_u32(executable.pointer, descriptor);
        u32 name_rva = link_read_u32(executable.pointer, descriptor + 12);
        if (!lookup_rva && !name_rva)
        {
            return false;
        }
        u64 name_offset = 0;
        u64 lookup_offset = 0;
        bool name_mapped = false;
        bool lookup_mapped = false;
        for (u16 section_index = 0; section_index < section_count; section_index += 1)
        {
            u64 section = section_table + (u64)section_index * 40;
            u32 virtual_size = link_read_u32(executable.pointer, section + 8);
            u32 virtual_address = link_read_u32(executable.pointer, section + 12);
            u32 raw_size = link_read_u32(executable.pointer, section + 16);
            u32 raw_offset = link_read_u32(executable.pointer, section + 20);
            u64 span = BUSTER_MAX(virtual_size, raw_size);
            if (name_rva >= virtual_address && (u64)name_rva - virtual_address < span)
            {
                name_offset = raw_offset + ((u64)name_rva - virtual_address);
                name_mapped = name_offset < executable.length;
            }
            if (lookup_rva >= virtual_address && (u64)lookup_rva - virtual_address < span)
            {
                lookup_offset = raw_offset + ((u64)lookup_rva - virtual_address);
                lookup_mapped = lookup_offset < executable.length;
            }
        }
        if (!name_mapped || !lookup_mapped)
        {
            return false;
        }
        u64 name_length = 0;
        while (name_offset + name_length < executable.length && executable.pointer[name_offset + name_length])
        {
            name_length += 1;
        }
        if (name_offset + name_length >= executable.length || !string_equal(
                                                                  (String8){
                                                                      .pointer = (char8*)executable.pointer + name_offset,
                                                                      .length = name_length,
                                                                  },
                                                                  library))
        {
            continue;
        }
        for (u32 thunk_index = 0; lookup_offset + (u64)thunk_index * 8 + sizeof(u64) <= executable.length; thunk_index += 1)
        {
            u64 name_entry = link_read_u64(executable.pointer, lookup_offset + (u64)thunk_index * 8);
            if (!name_entry)
            {
                return false;
            }
            if (name_entry >> 63)
            {
                continue;
            }
            u64 symbol_offset = 0;
            bool symbol_mapped = false;
            for (u16 section_index = 0; section_index < section_count; section_index += 1)
            {
                u64 section = section_table + (u64)section_index * 40;
                u32 virtual_size = link_read_u32(executable.pointer, section + 8);
                u32 virtual_address = link_read_u32(executable.pointer, section + 12);
                u32 raw_size = link_read_u32(executable.pointer, section + 16);
                u32 raw_offset = link_read_u32(executable.pointer, section + 20);
                u64 span = BUSTER_MAX(virtual_size, raw_size);
                if (name_entry >= virtual_address && name_entry - virtual_address < span)
                {
                    symbol_offset = raw_offset + (name_entry - virtual_address) + 2;
                    symbol_mapped = symbol_offset < executable.length;
                    break;
                }
            }
            if (!symbol_mapped)
            {
                return false;
            }
            u64 symbol_length = 0;
            while (symbol_offset + symbol_length < executable.length && executable.pointer[symbol_offset + symbol_length])
            {
                symbol_length += 1;
            }
            if (symbol_offset + symbol_length >= executable.length)
            {
                return false;
            }
            if (string_equal(
                    (String8){
                        .pointer = (char8*)executable.pointer + symbol_offset,
                        .length = symbol_length,
                    },
                    symbol))
            {
                return true;
            }
        }
    }
    return false;
}
#endif

BUSTER_TEST_F_DECL UnitTestResult link_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    static u8 const sha256_abc[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    u8 sha256_result[32] = {0};
    link_sha256(arguments->arena, (u8 const*)"abc", 3, sha256_result);
    BUSTER_TEST(arguments, memcmp(sha256_result, sha256_abc, sizeof(sha256_abc)) == 0);
    {
        ObjectSectionKind symbol_sections[] = {
            OBJECT_SECTION_TEXT,
            OBJECT_SECTION_READ_ONLY_DATA,
            OBJECT_SECTION_DATA,
            OBJECT_SECTION_ZERO,
            OBJECT_SECTION_THREAD_LOCAL_DATA,
            OBJECT_SECTION_THREAD_LOCAL_ZERO,
        };
        u32 expected_output_sections[] = {0, 1, 2, 3, 4, 4};
        u64 expected_section_offsets[] = {0x40, 0x10, 0x20, 0x30, 0x50, 0x90};
        u8 codeview_bytes[BUSTER_ARRAY_LENGTH(symbol_sections) * 8] = {0};
        ObjectSection sections[OBJECT_SECTION_COUNT] = {0};
        sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data = (ByteSlice)BUSTER_ARRAY_TO_SLICE(codeview_bytes);
        ObjectSymbol symbols[BUSTER_ARRAY_LENGTH(symbol_sections)] = {0};
        ObjectRelocation relocations[BUSTER_ARRAY_LENGTH(symbol_sections) * 2] = {0};
        u32 object_output_sections[OBJECT_SECTION_COUNT];
        u64 object_section_offsets[OBJECT_SECTION_COUNT] = {0};
        memset(object_output_sections, 0xff, sizeof(object_output_sections));
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(symbol_sections); index += 1)
        {
            ObjectSectionKind section = symbol_sections[index];
            symbols[index] = (ObjectSymbol){
                .value = 0x100 + (u64)index * 0x10,
                .section = section,
                .kind = OBJECT_SYMBOL_DATA,
            };
            relocations[index * 2] = (ObjectRelocation){
                .addend = (s64)index - 2,
                .offset = (u64)index * 8,
                .section = OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS,
                .symbol = index,
                .kind = OBJECT_RELOCATION_COFF_SECREL32,
            };
            relocations[index * 2 + 1] = (ObjectRelocation){
                .offset = (u64)index * 8 + 4,
                .section = OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS,
                .symbol = index,
                .kind = OBJECT_RELOCATION_COFF_SECTION16,
            };
            object_output_sections[section] = expected_output_sections[index];
            object_section_offsets[section] = expected_section_offsets[index];
        }
        ObjectDebugModule debug_module = {.symbols_size = sizeof(codeview_bytes)};
        ObjectFile object = {
            .sections = sections,
            .symbols = symbols,
            .relocations = relocations,
            .section_count = OBJECT_SECTION_COUNT,
            .symbol_count = BUSTER_ARRAY_LENGTH(symbols),
            .relocation_count = BUSTER_ARRAY_LENGTH(relocations),
        };
        ByteSlice resolved = link_pe_resolved_codeview(arguments->arena, &object, &debug_module, object_output_sections,
                                                       object_section_offsets, 5);
        BUSTER_TEST(arguments, resolved.length == sizeof(codeview_bytes));
        if (resolved.length == sizeof(codeview_bytes))
        {
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(symbol_sections); index += 1)
            {
                u32 section_offset = 0;
                u16 section_number = 0;
                memcpy(&section_offset, resolved.pointer + (u64)index * 8, sizeof(section_offset));
                memcpy(&section_number, resolved.pointer + (u64)index * 8 + 4, sizeof(section_number));
                s64 addend = (s64)index - 2;
                u32 expected_section_offset = (u32)((s64)symbols[index].value + (s64)expected_section_offsets[index] + addend);
                BUSTER_TEST(arguments, section_offset == expected_section_offset);
                BUSTER_TEST(arguments, section_number == expected_output_sections[index] + 1);
            }
        }
        object_output_sections[OBJECT_SECTION_DATA] = UINT32_MAX;
        BUSTER_TEST(arguments,
                    link_pe_resolved_codeview(arguments->arena, &object, &debug_module, object_output_sections, object_section_offsets, 5).length == 0);
    }
    Target target = target_native;
#if BUSTER_CPU_ARCH_X86_64
    u8 answer_text[] = {
        0xb8, 42, 0, 0, 0, 0xc3,
    };
    u8 main_text[] = {
        0xe8, 0, 0, 0, 0, 0x83, 0xe8, 42, 0xc3,
    };
    ByteSlice answer_bytes = (ByteSlice)BUSTER_ARRAY_TO_SLICE(answer_text);
    ByteSlice main_bytes = (ByteSlice)BUSTER_ARRAY_TO_SLICE(main_text);
    ObjectRelocation main_relocation = {
        .addend = -4,
        .offset = 1,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
#elif BUSTER_CPU_ARCH_AARCH64
    u32 answer_instructions[] = {
        0x52800540,
        0xd65f03c0,
    };
    u32 main_instructions[] = {
        0xa9bf7bfd, 0x910003fd, 0x94000000, 0x5100a800, 0xa8c17bfd, 0xd65f03c0,
    };
    ByteSlice answer_bytes = {
        .pointer = (u8*)answer_instructions,
        .length = sizeof(answer_instructions),
    };
    ByteSlice main_bytes = {
        .pointer = (u8*)main_instructions,
        .length = sizeof(main_instructions),
    };
    ObjectRelocation main_relocation = {
        .offset = 8,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_AARCH64_CALL26,
    };
#endif
#if BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64
    ObjectSymbol answer_symbols[] = {
        {
            .name = S8("answer"),
            .size = answer_bytes.length,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectSymbol main_symbols[] = {
        {
            .name = S8("main"),
            .size = main_bytes.length,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("answer"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectFile objects[] = {
        link_test_object_make(arguments->arena, target, answer_bytes, answer_symbols, BUSTER_ARRAY_LENGTH(answer_symbols), 0, 0),
        link_test_object_make(arguments->arena, target, main_bytes, main_symbols, BUSTER_ARRAY_LENGTH(main_symbols), &main_relocation, 1),
    };
#if BUSTER_LINUX
    CodegenUnwindAction main_unwind_actions[4] = {0};
    u32 main_unwind_action_count = 0;
#if BUSTER_CPU_ARCH_AARCH64
    main_unwind_actions[0] = (CodegenUnwindAction){.code_offset = 4, .value = 16, .kind = CODEGEN_UNWIND_ACTION_ALLOCATE_STACK};
    main_unwind_actions[1] = (CodegenUnwindAction){.code_offset = 4, .kind = CODEGEN_UNWIND_ACTION_SAVE_REGISTER, .register_index = 29};
    main_unwind_actions[2] = (CodegenUnwindAction){.code_offset = 4, .value = 8, .kind = CODEGEN_UNWIND_ACTION_SAVE_REGISTER, .register_index = 30};
    main_unwind_actions[3] = (CodegenUnwindAction){.code_offset = 8, .kind = CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, .register_index = 29};
    main_unwind_action_count = BUSTER_ARRAY_LENGTH(main_unwind_actions);
#endif
    CodegenFunctionDescriptor answer_descriptor = {
        .code_size = (u32)answer_bytes.length,
    };
    CodegenFunctionDescriptor main_descriptor = {
        .unwind_actions = main_unwind_actions,
        .code_size = (u32)main_bytes.length,
        .prolog_size = main_unwind_action_count ? 8 : 0,
        .unwind_action_count = main_unwind_action_count,
    };
    DwarfCfiResult answer_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                                      .functions = &answer_descriptor,
                                                                      .target = target,
                                                                      .function_count = 1,
                                                                  });
    DwarfCfiResult main_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                                    .functions = &main_descriptor,
                                                                    .target = target,
                                                                    .function_count = 1,
                                                                });
    BUSTER_TEST(arguments, answer_cfi.valid && answer_cfi.relocation_count == 1);
    BUSTER_TEST(arguments, main_cfi.valid && main_cfi.relocation_count == 1);
    ObjectRelocation answer_relocation = {
        .offset = answer_cfi.relocations[0].offset,
        .section = OBJECT_SECTION_UNWIND,
        .symbol = 0,
        .kind = target.cpu_arch == CPU_ARCH_X86_64 ? OBJECT_RELOCATION_X86_64_PC32 : OBJECT_RELOCATION_AARCH64_PREL32,
    };
    ObjectRelocation main_relocations[] = {
        main_relocation,
        {
            .offset = main_cfi.relocations[0].offset,
            .section = OBJECT_SECTION_UNWIND,
            .symbol = 0,
            .kind = target.cpu_arch == CPU_ARCH_X86_64 ? OBJECT_RELOCATION_X86_64_PC32 : OBJECT_RELOCATION_AARCH64_PREL32,
        },
    };
    ObjectFile cfi_objects[] = {
        link_test_object_make(arguments->arena, target, answer_bytes, answer_symbols, BUSTER_ARRAY_LENGTH(answer_symbols), &answer_relocation, 1),
        link_test_object_make(arguments->arena, target, main_bytes, main_symbols, BUSTER_ARRAY_LENGTH(main_symbols), main_relocations,
                              BUSTER_ARRAY_LENGTH(main_relocations)),
    };
    cfi_objects[0].sections[OBJECT_SECTION_UNWIND].data = answer_cfi.bytes;
    cfi_objects[1].sections[OBJECT_SECTION_UNWIND].data = main_cfi.bytes;
    LinkObjectResult cfi_linked = link_objects(arguments->arena, cfi_objects, BUSTER_ARRAY_LENGTH(cfi_objects), (LinkOptions){0});
    BUSTER_TEST(arguments, cfi_linked.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments, cfi_linked.object.relocation_count == 3);
    BUSTER_TEST(arguments, cfi_linked.object.sections[OBJECT_SECTION_UNWIND].data.length == answer_cfi.bytes.length + main_cfi.bytes.length);

    Target a64_elf_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    u32 a64_return = 0xd65f03c0;
    ObjectSymbol a64_main_symbol = {
        .name = S8("main"),
        .size = sizeof(a64_return),
        .section = OBJECT_SECTION_TEXT,
        .kind = OBJECT_SYMBOL_FUNCTION,
        .global = true,
    };
    CodegenFunctionDescriptor a64_descriptor = {
        .code_size = sizeof(a64_return),
    };
    DwarfCfiResult a64_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                                    .functions = &a64_descriptor,
                                                                    .target = a64_elf_target,
                                                                    .function_count = 1,
                                                                });
    BUSTER_TEST(arguments, a64_cfi.valid && a64_cfi.relocation_count == 1);
    ObjectRelocation a64_cfi_relocation = {
        .offset = a64_cfi.relocations[0].offset,
        .section = OBJECT_SECTION_UNWIND,
        .symbol = 0,
        .kind = OBJECT_RELOCATION_AARCH64_PREL32,
    };
    ObjectFile a64_elf_object = link_test_object_make(arguments->arena, a64_elf_target,
                                                      (ByteSlice){
                                                          .pointer = (u8*)&a64_return,
                                                          .length = sizeof(a64_return),
                                                      },
                                                      &a64_main_symbol, 1, &a64_cfi_relocation, 1);
    a64_elf_object.sections[OBJECT_SECTION_UNWIND].data = a64_cfi.bytes;
    NativeExecutableLinkResult a64_elf_executable = link_native_executable(arguments->arena, &a64_elf_object,
                                                                           (NativeExecutableLinkOptions){
                                                                               .entry_symbol = S8("main"),
                                                                           });
    BUSTER_TEST(arguments, a64_elf_executable.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments, a64_elf_executable.executable.length >= 64);
    if (a64_elf_executable.error == LINK_ERROR_NONE)
    {
        u32 a64_header_index = 0;
        u64 a64_header = 0;
        BUSTER_TEST(arguments, a64_elf_executable.executable.pointer[18] == 183 && a64_elf_executable.executable.pointer[19] == 0);
        BUSTER_TEST(arguments, link_test_elf_section_find(a64_elf_executable.executable, S8(".eh_frame_hdr"), &a64_header_index, &a64_header));
    }
#endif
    LinkObjectResult linked = link_objects(arguments->arena, objects, BUSTER_ARRAY_LENGTH(objects), (LinkOptions){0});
    BUSTER_TEST(arguments, linked.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments, linked.object.symbol_count == 2);
    BUSTER_TEST(arguments, linked.object.relocation_count == 1);
    bool linked_text_size_matches = false;
    if (linked.object.sections && linked.object.section_count > OBJECT_SECTION_TEXT)
    {
        linked_text_size_matches = linked.object.sections[OBJECT_SECTION_TEXT].data.length == align_forward(answer_bytes.length, 16) + main_bytes.length;
    }
    BUSTER_TEST(arguments, linked_text_size_matches);
    ObjectFile duplicate_objects[] = {
        objects[0],
        objects[0],
    };
    LinkObjectResult duplicate = link_objects(arguments->arena, duplicate_objects, BUSTER_ARRAY_LENGTH(duplicate_objects), (LinkOptions){0});
    BUSTER_TEST(arguments, duplicate.error == LINK_ERROR_DUPLICATE_SYMBOL);
    BUSTER_STRING_TEST(arguments, duplicate.symbol, S8("answer"));
    ObjectFile unresolved_object = objects[1];
    LinkObjectResult unresolved = link_objects(arguments->arena, &unresolved_object, 1, (LinkOptions){0});
    BUSTER_TEST(arguments, unresolved.error == LINK_ERROR_UNRESOLVED_SYMBOL);
    BUSTER_STRING_TEST(arguments, unresolved.symbol, S8("answer"));
    LinkObjectResult permitted = link_objects(arguments->arena, &unresolved_object, 1,
                                              (LinkOptions){
                                                  .allow_undefined_symbols = true,
                                              });
    BUSTER_TEST(arguments, permitted.error == LINK_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64
    ObjectFile windows_object = linked.object;
    windows_object.target.os = OPERATING_SYSTEM_WINDOWS;
    String8 pe_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-link-test"), S8(".exe"));
    NativeExecutableLinkResult pe_executable = link_native_executable(arguments->arena, &windows_object,
                                                                      (NativeExecutableLinkOptions){
                                                                          .output_path = pe_output_path,
                                                                          .entry_symbol = S8("main"),
                                                                      });
    BUSTER_TEST(arguments, pe_executable.error == LINK_ERROR_NONE);
    bool pe_header_valid = pe_executable.executable.length > 0x84 && pe_executable.executable.pointer[0] == 'M' && pe_executable.executable.pointer[1] == 'Z' &&
                           memcmp(pe_executable.executable.pointer + 0x80, "PE\0\0", 4) == 0 && (pe_executable.executable.pointer[0x96] & 0x01) != 0;
    BUSTER_TEST(arguments, pe_header_valid);
    BUSTER_TEST(arguments, pe_executable.executable.length > 0xe8 && link_read_u64(pe_executable.executable.pointer, 0xe0) == LINK_TEST_PE_STACK_RESERVE);
    u8 const pe_argv_mode_prefix[] = {
        0x48, 0x83, 0xec, 0x38, 0x31, 0xc9, 0xff, 0xc1,
    };
    bool pe_argv_mode_valid = pe_executable.executable.length >= 0x400 + sizeof(pe_argv_mode_prefix) &&
                              memcmp(pe_executable.executable.pointer + 0x400, pe_argv_mode_prefix, sizeof(pe_argv_mode_prefix)) == 0;
    BUSTER_TEST(arguments, pe_argv_mode_valid);
    u8 pe_libc_main_text[] = {
        0x48, 0x83, 0xec, 0x28, 0xb9, 0xd6, 0xff, 0xff, 0xff, 0xe8, 0, 0, 0, 0, 0x83, 0xe8, 42, 0x48, 0x83, 0xc4, 0x28, 0xc3,
    };
    ObjectSymbol pe_libc_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(pe_libc_main_text),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("abs"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation pe_libc_relocation = {
        .addend = -4,
        .offset = 10,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    ObjectFile pe_libc_object = link_test_object_make(arguments->arena, windows_object.target, (ByteSlice)BUSTER_ARRAY_TO_SLICE(pe_libc_main_text),
                                                      pe_libc_symbols, BUSTER_ARRAY_LENGTH(pe_libc_symbols), &pe_libc_relocation, 1);
    String8 pe_libc_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-libc-link-test"), S8(".exe"));
    NativeExecutableLinkResult pe_libc_executable = link_native_executable(arguments->arena, &pe_libc_object,
                                                                           (NativeExecutableLinkOptions){
                                                                               .output_path = pe_libc_output_path,
                                                                               .entry_symbol = S8("main"),
                                                                           });
    BUSTER_TEST(arguments, pe_libc_executable.error == LINK_ERROR_NONE);
    String8 external_exports[] = {
        S8("external_value"),
    };
    NativeDynamicLibrary external_library = {
        .name = S8("external.dll"),
        .exported_symbols = external_exports,
        .exported_symbol_count = BUSTER_ARRAY_LENGTH(external_exports),
    };
    pe_libc_symbols[1].name = S8("external_value");
    NativeExecutableLinkResult pe_external_executable = link_native_executable(arguments->arena, &pe_libc_object,
                                                                               (NativeExecutableLinkOptions){
                                                                                   .entry_symbol = S8("main"),
                                                                                   .dynamic_libraries = &external_library,
                                                                                   .dynamic_library_count = 1,
                                                                               });
    BUSTER_TEST(arguments, pe_external_executable.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments, link_test_pe_import_matches(pe_external_executable.executable, S8("external.dll"), S8("external_value")));
    BUSTER_TEST(arguments, !link_test_pe_import_matches(pe_external_executable.executable, S8("ucrtbase.dll"), S8("external_value")));
    u8 pe_data_main_text[] = {
        0x48, 0x8d, 0x05, 0, 0, 0, 0, 0xc3,
    };
    ObjectSymbol pe_data_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(pe_data_main_text),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("external_value"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
    };
    ObjectRelocation pe_data_relocation = {
        .addend = -4,
        .offset = 3,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    ObjectFile pe_data_object = link_test_object_make(arguments->arena, windows_object.target, (ByteSlice)BUSTER_ARRAY_TO_SLICE(pe_data_main_text),
                                                       pe_data_symbols, BUSTER_ARRAY_LENGTH(pe_data_symbols), &pe_data_relocation, 1);
    NativeExecutableLinkResult pe_data_executable = link_native_executable(arguments->arena, &pe_data_object,
                                                                            (NativeExecutableLinkOptions){
                                                                                .entry_symbol = S8("main"),
                                                                                .dynamic_libraries = &external_library,
                                                                                .dynamic_library_count = 1,
                                                                            });
    BUSTER_TEST(arguments, pe_data_executable.error == LINK_ERROR_NONE);
    u64 pe_data_text_offset = 0x400 + align_forward(54, 16);
    BUSTER_TEST(arguments, pe_data_executable.executable.length > pe_data_text_offset + 2 && pe_data_executable.executable.pointer[pe_data_text_offset] == 0x48 &&
                           pe_data_executable.executable.pointer[pe_data_text_offset + 1] == 0x8b);
    BUSTER_TEST(arguments, link_test_pe_import_matches(pe_data_executable.executable, S8("external.dll"), S8("external_value")));
    pe_libc_symbols[1].name = S8("missing_value");
    NativeExecutableLinkResult pe_missing_external = link_native_executable(arguments->arena, &pe_libc_object,
                                                                            (NativeExecutableLinkOptions){
                                                                                .entry_symbol = S8("main"),
                                                                                .dynamic_libraries = &external_library,
                                                                                .dynamic_library_count = 1,
                                                                            });
    BUSTER_TEST(arguments, pe_missing_external.error == LINK_ERROR_UNRESOLVED_SYMBOL);
    BUSTER_STRING_TEST(arguments, pe_missing_external.symbol, S8("missing_value"));
#endif
    u32 aarch64_main_instructions[] = {
        0x52800000,
        0xd65f03c0,
    };
    ObjectSymbol aarch64_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(aarch64_main_instructions),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    Target aarch64_target = target;
    aarch64_target.cpu_arch = CPU_ARCH_AARCH64;
    aarch64_target.os = OPERATING_SYSTEM_LINUX;
    ObjectFile aarch64_object = link_test_object_make(arguments->arena, aarch64_target,
                                                      (ByteSlice){
                                                          .pointer = (u8*)aarch64_main_instructions,
                                                          .length = sizeof(aarch64_main_instructions),
                                                      },
                                                      aarch64_symbols, BUSTER_ARRAY_LENGTH(aarch64_symbols), 0, 0);
    String8 aarch64_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-aarch64-link-test"), S8(""));
    NativeExecutableLinkResult aarch64_executable = link_native_executable(arguments->arena, &aarch64_object,
                                                                           (NativeExecutableLinkOptions){
                                                                               .output_path = aarch64_output_path,
                                                                               .entry_symbol = S8("main"),
                                                                           });
    BUSTER_TEST(arguments, aarch64_executable.error == LINK_ERROR_NONE);
    bool aarch64_header_valid = aarch64_executable.executable.length > 20 && aarch64_executable.executable.pointer[0] == 0x7f &&
                                aarch64_executable.executable.pointer[1] == 'E' && aarch64_executable.executable.pointer[18] == 183;
    BUSTER_TEST(arguments, aarch64_header_valid);
    u64 aarch64_text_header = 0;
    u64 aarch64_bss_header = 0;
    u64 aarch64_debug_info_header = 0;
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_executable.executable, S8(".text"), 0, &aarch64_text_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_executable.executable, S8(".bss"), 0, &aarch64_bss_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_executable.executable, S8(".debug_info"), 0, &aarch64_debug_info_header));
    BUSTER_TEST(arguments, aarch64_text_header && link_read_u32(aarch64_executable.executable.pointer, aarch64_text_header + 4) == 1);
    BUSTER_TEST(arguments, aarch64_bss_header && link_read_u32(aarch64_executable.executable.pointer, aarch64_bss_header + 4) == 8);
    BUSTER_TEST(arguments, aarch64_debug_info_header && link_read_u64(aarch64_executable.executable.pointer, aarch64_debug_info_header + 32) == 0);
    u32 aarch64_libc_instructions[] = {
        0xa9bf7bfd, 0x910003fd, 0x52800540, 0x94000000, 0x5100a800, 0xa8c17bfd, 0xd65f03c0,
    };
    ObjectSymbol aarch64_libc_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(aarch64_libc_instructions),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("abs"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation aarch64_libc_relocation = {
        .offset = 3 * sizeof(u32),
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_AARCH64_CALL26,
    };
    ObjectFile aarch64_libc_object = link_test_object_make(arguments->arena, aarch64_target,
                                                           (ByteSlice){
                                                               .pointer = (u8*)aarch64_libc_instructions,
                                                               .length = sizeof(aarch64_libc_instructions),
                                                           },
                                                           aarch64_libc_symbols, BUSTER_ARRAY_LENGTH(aarch64_libc_symbols), &aarch64_libc_relocation, 1);
    String8 aarch64_libc_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-aarch64-libc-link-test"), S8(""));
    NativeExecutableLinkResult aarch64_libc_executable = link_native_executable(arguments->arena, &aarch64_libc_object,
                                                                                (NativeExecutableLinkOptions){
                                                                                    .output_path = aarch64_libc_output_path,
                                                                                    .entry_symbol = S8("main"),
                                                                                });
    BUSTER_TEST(arguments, aarch64_libc_executable.error == LINK_ERROR_NONE);
    String8 dynamic_section_names[] = {
        S8(".interp"), S8(".plt"), S8(".dynstr"), S8(".dynsym"), S8(".hash"), S8(".rela.plt"), S8(".got.plt"), S8(".dynamic"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(dynamic_section_names); index += 1)
    {
        BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_libc_executable.executable, dynamic_section_names[index], 0, 0));
    }
    u32 dynamic_string_index = 0;
    u32 dynamic_symbol_index = 0;
    u32 got_index = 0;
    u64 dynamic_symbol_header = 0;
    u64 hash_header = 0;
    u64 relocation_header = 0;
    u64 dynamic_header = 0;
    BUSTER_TEST(arguments,
                link_test_elf_section_find(aarch64_libc_executable.executable, S8(".dynstr"), &dynamic_string_index, 0));
    BUSTER_TEST(arguments,
                link_test_elf_section_find(aarch64_libc_executable.executable, S8(".dynsym"), &dynamic_symbol_index, &dynamic_symbol_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_libc_executable.executable, S8(".got.plt"), &got_index, 0));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_libc_executable.executable, S8(".hash"), 0, &hash_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_libc_executable.executable, S8(".rela.plt"), 0, &relocation_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_libc_executable.executable, S8(".dynamic"), 0, &dynamic_header));
    BUSTER_TEST(arguments, dynamic_symbol_header && link_read_u32(aarch64_libc_executable.executable.pointer, dynamic_symbol_header + 40) == dynamic_string_index);
    BUSTER_TEST(arguments, hash_header && link_read_u32(aarch64_libc_executable.executable.pointer, hash_header + 40) == dynamic_symbol_index);
    BUSTER_TEST(arguments, relocation_header && link_read_u32(aarch64_libc_executable.executable.pointer, relocation_header + 40) == dynamic_symbol_index &&
                               link_read_u32(aarch64_libc_executable.executable.pointer, relocation_header + 44) == got_index);
    BUSTER_TEST(arguments, dynamic_header && link_read_u32(aarch64_libc_executable.executable.pointer, dynamic_header + 40) == dynamic_string_index);
    ObjectFile aarch64_tls_object = aarch64_libc_object;
    ObjectSection* aarch64_tls_sections = arena_allocate(arguments->arena, ObjectSection, OBJECT_SECTION_COUNT);
    memcpy(aarch64_tls_sections, aarch64_libc_object.sections, sizeof(*aarch64_tls_sections) * OBJECT_SECTION_COUNT);
    aarch64_tls_object.sections = aarch64_tls_sections;
    u32 initialized_thread_local = 42;
    aarch64_tls_sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data = (ByteSlice){
        .pointer = (u8*)&initialized_thread_local,
        .length = sizeof(initialized_thread_local),
    };
    aarch64_tls_sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size = 32;
    NativeExecutableLinkResult aarch64_tls_executable = link_native_executable(arguments->arena, &aarch64_tls_object,
                                                                                (NativeExecutableLinkOptions){
                                                                                    .entry_symbol = S8("main"),
                                                                                });
    BUSTER_TEST(arguments, aarch64_tls_executable.error == LINK_ERROR_NONE);
    u64 thread_local_data_header = 0;
    u64 thread_local_zero_header = 0;
    u64 tls_got_header = 0;
    u64 tls_dynamic_header = 0;
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_tls_executable.executable, S8(".tdata"), 0, &thread_local_data_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_tls_executable.executable, S8(".tbss"), 0, &thread_local_zero_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_tls_executable.executable, S8(".got.plt"), 0, &tls_got_header));
    BUSTER_TEST(arguments, link_test_elf_section_find(aarch64_tls_executable.executable, S8(".dynamic"), 0, &tls_dynamic_header));
    u64 thread_local_data_address = thread_local_data_header ? link_read_u64(aarch64_tls_executable.executable.pointer, thread_local_data_header + 16) : 0;
    u64 thread_local_data_size = thread_local_data_header ? link_read_u64(aarch64_tls_executable.executable.pointer, thread_local_data_header + 32) : 0;
    u64 thread_local_zero_address = thread_local_zero_header ? link_read_u64(aarch64_tls_executable.executable.pointer, thread_local_zero_header + 16) : 0;
    u64 thread_local_zero_size = thread_local_zero_header ? link_read_u64(aarch64_tls_executable.executable.pointer, thread_local_zero_header + 32) : 0;
    u64 tls_got_address = tls_got_header ? link_read_u64(aarch64_tls_executable.executable.pointer, tls_got_header + 16) : 0;
    u64 tls_got_size = tls_got_header ? link_read_u64(aarch64_tls_executable.executable.pointer, tls_got_header + 32) : 0;
    u64 tls_dynamic_address = tls_dynamic_header ? link_read_u64(aarch64_tls_executable.executable.pointer, tls_dynamic_header + 16) : 0;
    u64 tls_dynamic_size = tls_dynamic_header ? link_read_u64(aarch64_tls_executable.executable.pointer, tls_dynamic_header + 32) : 0;
    BUSTER_TEST(arguments, thread_local_data_header && link_read_u32(aarch64_tls_executable.executable.pointer, thread_local_data_header + 4) == 1 &&
                               (link_read_u64(aarch64_tls_executable.executable.pointer, thread_local_data_header + 8) & UINT64_C(0x403)) == UINT64_C(0x403));
    BUSTER_TEST(arguments, thread_local_zero_header && link_read_u32(aarch64_tls_executable.executable.pointer, thread_local_zero_header + 4) == 8 &&
                               (link_read_u64(aarch64_tls_executable.executable.pointer, thread_local_zero_header + 8) & UINT64_C(0x403)) == UINT64_C(0x403));
    BUSTER_TEST(arguments, tls_got_address + tls_got_size <= tls_dynamic_address);
    BUSTER_TEST(arguments, tls_dynamic_address + tls_dynamic_size <= thread_local_data_address);
    BUSTER_TEST(arguments, thread_local_data_address + thread_local_data_size <= thread_local_zero_address);
    BUSTER_TEST(arguments, thread_local_zero_size == 32);
    u32 aarch64_data_main_instructions[] = {
        0x58000049, 0x14000003, 0, 0,
    };
    ObjectSymbol aarch64_data_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(aarch64_data_main_instructions),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("external_value"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
    };
    ObjectRelocation aarch64_data_relocation = {
        .offset = 2 * sizeof(u32),
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_ABSOLUTE64,
    };
    ObjectFile aarch64_data_object = link_test_object_make(arguments->arena, (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_WINDOWS},
                                                            (ByteSlice){
                                                                .pointer = (u8*)aarch64_data_main_instructions,
                                                                .length = sizeof(aarch64_data_main_instructions),
                                                            },
                                                            aarch64_data_symbols, BUSTER_ARRAY_LENGTH(aarch64_data_symbols), &aarch64_data_relocation, 1);
    String8 aarch64_data_exports[] = {
        S8("external_value"),
    };
    NativeDynamicLibrary aarch64_data_library = {
        .name = S8("external.dll"),
        .exported_symbols = aarch64_data_exports,
        .exported_symbol_count = BUSTER_ARRAY_LENGTH(aarch64_data_exports),
    };
    NativeExecutableLinkResult aarch64_data_executable = link_native_executable(arguments->arena, &aarch64_data_object,
                                                                                  (NativeExecutableLinkOptions){
                                                                                      .entry_symbol = S8("main"),
                                                                                      .dynamic_libraries = &aarch64_data_library,
                                                                                      .dynamic_library_count = 1,
                                                                                  });
    BUSTER_TEST(arguments, aarch64_data_executable.error == LINK_ERROR_NONE);
    u64 aarch64_data_text_offset = 0x400 + align_forward(20, 16);
    BUSTER_TEST(arguments, aarch64_data_executable.executable.length > aarch64_data_text_offset + 12 &&
                           link_read_u32(aarch64_data_executable.executable.pointer, aarch64_data_text_offset + 8) == UINT32_C(0x14000002));
    ObjectFile aarch64_pe_object = aarch64_object;
    aarch64_pe_object.target.os = OPERATING_SYSTEM_WINDOWS;
    String8 aarch64_pe_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-aarch64-pe-test"), S8(".exe"));
    NativeExecutableLinkResult aarch64_pe_executable = link_native_executable(arguments->arena, &aarch64_pe_object,
                                                                              (NativeExecutableLinkOptions){
                                                                                  .output_path = aarch64_pe_output_path,
                                                                                  .entry_symbol = S8("main"),
                                                                              });
    BUSTER_TEST(arguments, aarch64_pe_executable.error == LINK_ERROR_NONE);
    bool aarch64_pe_header_valid = aarch64_pe_executable.executable.length > 0x88 && aarch64_pe_executable.executable.pointer[0x84] == 0x64 &&
                                   aarch64_pe_executable.executable.pointer[0x85] == 0xaa;
    BUSTER_TEST(arguments, aarch64_pe_header_valid);
    ObjectFile aarch64_pe_libc_object = aarch64_libc_object;
    aarch64_pe_libc_object.target.os = OPERATING_SYSTEM_WINDOWS;
    NativeExecutableLinkResult aarch64_pe_libc_executable = link_native_executable(arguments->arena, &aarch64_pe_libc_object,
                                                                                   (NativeExecutableLinkOptions){
                                                                                       .entry_symbol = S8("main"),
                                                                                   });
    BUSTER_TEST(arguments, aarch64_pe_libc_executable.error == LINK_ERROR_NONE);
    ObjectFile android_object = aarch64_libc_object;
    android_object.target.os = OPERATING_SYSTEM_ANDROID;
    String8 android_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-android-test"), S8(""));
    NativeExecutableLinkResult android_executable = link_native_executable(arguments->arena, &android_object,
                                                                           (NativeExecutableLinkOptions){
                                                                               .output_path = android_output_path,
                                                                               .entry_symbol = S8("main"),
                                                                           });
    BUSTER_TEST(arguments, android_executable.error == LINK_ERROR_NONE);
    bool android_header_valid =
        android_executable.executable.length > 18 && android_executable.executable.pointer[16] == 3 && android_executable.executable.pointer[17] == 0;
    BUSTER_TEST(arguments, android_header_valid);
    ObjectFile aarch64_mach_object = aarch64_object;
    aarch64_mach_object.target.os = OPERATING_SYSTEM_MACOS;
    String8 aarch64_mach_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-aarch64-macho-test"), S8(""));
    NativeExecutableLinkResult aarch64_mach_executable = link_native_executable(arguments->arena, &aarch64_mach_object,
                                                                                (NativeExecutableLinkOptions){
                                                                                    .output_path = aarch64_mach_output_path,
                                                                                    .entry_symbol = S8("main"),
                                                                                });
    BUSTER_TEST(arguments, aarch64_mach_executable.error == LINK_ERROR_NONE);
    bool aarch64_mach_header_valid = aarch64_mach_executable.executable.length > 32 && aarch64_mach_executable.executable.pointer[0] == 0xcf &&
                                     aarch64_mach_executable.executable.pointer[1] == 0xfa && aarch64_mach_executable.executable.pointer[2] == 0xed &&
                                     aarch64_mach_executable.executable.pointer[3] == 0xfe && (aarch64_mach_executable.executable.pointer[26] & 0x20) != 0;
    BUSTER_TEST(arguments, aarch64_mach_header_valid);
    CodegenFunctionDescriptor aarch64_mach_cfi_descriptor = {
        .code_size = (u32)sizeof(aarch64_main_instructions),
    };
    DwarfCfiResult aarch64_mach_cfi = dwarf_cfi_build(arguments->arena, (DwarfCfiInput){
                                                                            .functions = &aarch64_mach_cfi_descriptor,
                                                                            .target = aarch64_mach_object.target,
                                                                            .function_count = 1,
                                                                        });
    BUSTER_TEST(arguments, aarch64_mach_cfi.valid && aarch64_mach_cfi.relocation_count == 1);
    ObjectRelocation aarch64_mach_cfi_relocation = {
        .offset = aarch64_mach_cfi.relocations[0].offset,
        .section = OBJECT_SECTION_UNWIND,
        .symbol = 0,
        .kind = OBJECT_RELOCATION_AARCH64_PREL32,
    };
    ObjectFile aarch64_mach_cfi_object = aarch64_mach_object;
    ObjectSection* aarch64_mach_cfi_sections = arena_allocate(arguments->arena, ObjectSection, OBJECT_SECTION_COUNT);
    memcpy(aarch64_mach_cfi_sections, aarch64_mach_object.sections, sizeof(*aarch64_mach_cfi_sections) * OBJECT_SECTION_COUNT);
    aarch64_mach_cfi_object.sections = aarch64_mach_cfi_sections;
    aarch64_mach_cfi_sections[OBJECT_SECTION_UNWIND].data = aarch64_mach_cfi.bytes;
    aarch64_mach_cfi_object.relocations = &aarch64_mach_cfi_relocation;
    aarch64_mach_cfi_object.relocation_count = 1;
    NativeExecutableLinkResult aarch64_mach_cfi_executable = link_native_executable(arguments->arena, &aarch64_mach_cfi_object,
                                                                                   (NativeExecutableLinkOptions){
                                                                                       .entry_symbol = S8("main"),
                                                                                   });
    BUSTER_TEST(arguments, aarch64_mach_cfi_executable.error == LINK_ERROR_NONE);
    u64 aarch64_mach_cfi_section = 0;
    bool aarch64_mach_cfi_found =
        link_test_mach_section_find(aarch64_mach_cfi_executable.executable, S8("__TEXT"), S8("__eh_frame"), &aarch64_mach_cfi_section);
    BUSTER_TEST(arguments, aarch64_mach_cfi_found);
    if (aarch64_mach_cfi_found)
    {
        BUSTER_TEST(arguments, link_read_u32(aarch64_mach_cfi_executable.executable.pointer, aarch64_mach_cfi_section + 64) == 0x6800000b);
        u64 unwind_address = link_read_u64(aarch64_mach_cfi_executable.executable.pointer, aarch64_mach_cfi_section + 32);
        u64 unwind_offset = link_read_u32(aarch64_mach_cfi_executable.executable.pointer, aarch64_mach_cfi_section + 48);
        s32 function_displacement = 0;
        memcpy(&function_displacement, aarch64_mach_cfi_executable.executable.pointer + unwind_offset + aarch64_mach_cfi.relocations[0].offset,
               sizeof(function_displacement));
        BUSTER_TEST(arguments, (s64)unwind_address + (s64)aarch64_mach_cfi.relocations[0].offset + function_displacement ==
                                   (s64)UINT64_C(0x100000000) + (s64)align_forward(32 + link_read_u32(aarch64_mach_cfi_executable.executable.pointer, 20), 16));
    }
    u32 aarch64_mach_data_instructions[] = {
        0x58000049, 0x14000003, 0, 0, 0x52800000, 0xd65f03c0,
    };
    u64 aarch64_mach_data_value = 42;
    ObjectSymbol aarch64_mach_data_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(aarch64_mach_data_instructions),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("data"),
            .size = sizeof(aarch64_mach_data_value),
            .section = OBJECT_SECTION_DATA,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
    };
    ObjectRelocation aarch64_mach_data_relocation = {
        .offset = 2 * sizeof(u32),
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_ABSOLUTE64,
    };
    ObjectFile aarch64_mach_data_object =
        link_test_object_make(arguments->arena, aarch64_mach_object.target,
                              (ByteSlice){
                                  .pointer = (u8*)aarch64_mach_data_instructions,
                                  .length = sizeof(aarch64_mach_data_instructions),
                              },
                              aarch64_mach_data_symbols, BUSTER_ARRAY_LENGTH(aarch64_mach_data_symbols), &aarch64_mach_data_relocation, 1);
    aarch64_mach_data_object.sections[OBJECT_SECTION_DATA].data = (ByteSlice){
        .pointer = (u8*)&aarch64_mach_data_value,
        .length = sizeof(aarch64_mach_data_value),
    };
    String8 aarch64_mach_data_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-aarch64-macho-data-test"), S8(""));
    NativeExecutableLinkResult aarch64_mach_data_executable = link_native_executable(arguments->arena, &aarch64_mach_data_object,
                                                                                     (NativeExecutableLinkOptions){
                                                                                         .output_path = aarch64_mach_data_output_path,
                                                                                         .entry_symbol = S8("main"),
                                                                                     });
    BUSTER_TEST(arguments, aarch64_mach_data_executable.error == LINK_ERROR_NONE);
    u32 aarch64_mach_data_adrp = 0;
    u32 aarch64_mach_data_add = 0;
    u32 aarch64_mach_data_branch = 0;
    if (aarch64_mach_data_executable.error == LINK_ERROR_NONE && aarch64_mach_data_executable.executable.length > 32)
    {
        u64 aarch64_mach_data_text_offset = align_forward(32 + link_read_u32(aarch64_mach_data_executable.executable.pointer, 20), 16);
        if (aarch64_mach_data_text_offset <= aarch64_mach_data_executable.executable.length &&
            3 * sizeof(u32) <= aarch64_mach_data_executable.executable.length - aarch64_mach_data_text_offset)
        {
            aarch64_mach_data_adrp = link_read_u32(aarch64_mach_data_executable.executable.pointer, aarch64_mach_data_text_offset);
            aarch64_mach_data_add = link_read_u32(aarch64_mach_data_executable.executable.pointer, aarch64_mach_data_text_offset + sizeof(u32));
            aarch64_mach_data_branch = link_read_u32(aarch64_mach_data_executable.executable.pointer, aarch64_mach_data_text_offset + 2 * sizeof(u32));
        }
    }
    BUSTER_TEST(arguments, (aarch64_mach_data_adrp & UINT32_C(0x9f00001f)) == UINT32_C(0x90000009));
    BUSTER_TEST(arguments, (aarch64_mach_data_add & UINT32_C(0xffc003ff)) == UINT32_C(0x91000129));
    BUSTER_TEST(arguments, aarch64_mach_data_branch == UINT32_C(0x14000002));
    ObjectFile aarch64_mach_libc_object = aarch64_libc_object;
    aarch64_mach_libc_object.target.os = OPERATING_SYSTEM_MACOS;
    String8 aarch64_mach_libc_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-aarch64-macho-libc-test"), S8(""));
    NativeExecutableLinkResult aarch64_mach_libc_executable = link_native_executable(arguments->arena, &aarch64_mach_libc_object,
                                                                                     (NativeExecutableLinkOptions){
                                                                                         .output_path = aarch64_mach_libc_output_path,
                                                                                         .entry_symbol = S8("main"),
                                                                                     });
    BUSTER_TEST(arguments, aarch64_mach_libc_executable.error == LINK_ERROR_NONE);
    // Debug sections must reach the Mach-O image in an unmapped __DWARF
    // segment, with their address relocations resolved statically.
    ObjectFile mach_debug_object = aarch64_mach_object;
    ObjectSection* mach_debug_sections = arena_allocate(arguments->arena, ObjectSection, OBJECT_SECTION_COUNT);
    memcpy(mach_debug_sections, mach_debug_object.sections, OBJECT_SECTION_COUNT * sizeof(*mach_debug_sections));
    mach_debug_object.sections = mach_debug_sections;
    u8 mach_debug_info[16] = {0};
    u8 mach_debug_line[8] = {0};
    mach_debug_sections[OBJECT_SECTION_DEBUG_INFO].data = (ByteSlice){
        .pointer = mach_debug_info,
        .length = sizeof(mach_debug_info),
    };
    mach_debug_sections[OBJECT_SECTION_DEBUG_LINE].data = (ByteSlice){
        .pointer = mach_debug_line,
        .length = sizeof(mach_debug_line),
    };
    ObjectRelocation mach_debug_relocation = {
        .offset = 0,
        .section = OBJECT_SECTION_DEBUG_INFO,
        .symbol = 0,
        .kind = OBJECT_RELOCATION_ABSOLUTE64,
    };
    mach_debug_object.relocations = &mach_debug_relocation;
    mach_debug_object.relocation_count = 1;
    NativeExecutableLinkResult mach_debug_executable = link_native_executable(arguments->arena, &mach_debug_object,
                                                                             (NativeExecutableLinkOptions){
                                                                                 .entry_symbol = S8("main"),
                                                                             });
    BUSTER_TEST(arguments, mach_debug_executable.error == LINK_ERROR_NONE);
    if (mach_debug_executable.error == LINK_ERROR_NONE)
    {
        ByteSlice image = mach_debug_executable.executable;
        BUSTER_TEST(arguments, image.length > 32 && link_read_u32(image.pointer, 0) == 0xfeedfacf);
        u32 mach_command_count = link_read_u32(image.pointer, 16);
        u64 mach_command = 32;
        bool found_dwarf = false;
        u64 dwarf_file_offset = 0;
        u32 dwarf_section_count = 0;
        for (u32 command_index = 0; command_index < mach_command_count && mach_command + 8 <= image.length; command_index += 1)
        {
            u32 command_kind = link_read_u32(image.pointer, mach_command);
            u32 command_length = link_read_u32(image.pointer, mach_command + 4);
            if (!command_length || mach_command + command_length > image.length)
            {
                break;
            }
            if (command_kind == 0x19 && memcmp(image.pointer + mach_command + 8, "__DWARF", 8) == 0)
            {
                found_dwarf = true;
                dwarf_file_offset = link_read_u64(image.pointer, mach_command + 40);
                dwarf_section_count = link_read_u32(image.pointer, mach_command + 64);
                // dyld rejects a segment whose file size exceeds its virtual
                // size, so the debug segment covers its bytes with a page
                // rounded read-only mapping.
                BUSTER_TEST(arguments, link_read_u64(image.pointer, mach_command + 48) == sizeof(mach_debug_info) + sizeof(mach_debug_line));
                BUSTER_TEST(arguments, link_read_u64(image.pointer, mach_command + 32) == 0x4000);
                BUSTER_TEST(arguments, link_read_u64(image.pointer, mach_command + 24) == UINT64_C(0x100000000) + dwarf_file_offset);
                BUSTER_TEST(arguments, link_read_u32(image.pointer, mach_command + 60) == 1);
                for (u32 section_index = 0; section_index < dwarf_section_count; section_index += 1)
                {
                    u64 section_command = mach_command + 72 + (u64)section_index * 80;
                    BUSTER_TEST(arguments, memcmp(image.pointer + section_command + 16, "__DWARF", 8) == 0);
                    BUSTER_TEST(arguments, (link_read_u32(image.pointer, section_command + 64) & 0x02000000) != 0);
                    // Sections must stay inside the address range of the
                    // segment that carries them.
                    BUSTER_TEST(arguments, link_read_u64(image.pointer, section_command + 32) ==
                                               UINT64_C(0x100000000) + link_read_u32(image.pointer, section_command + 48));
                }
            }
            mach_command += command_length;
        }
        BUSTER_TEST(arguments, found_dwarf);
        BUSTER_TEST(arguments, dwarf_section_count == 6);
        // The kernel loader refuses images with segments that are not page
        // aligned in the file.
        BUSTER_TEST(arguments, dwarf_file_offset % 0x4000 == 0);
        // The resolved slot must hold the link-time address of "main".
        BUSTER_TEST(arguments, dwarf_file_offset + 8 <= image.length);
        if (dwarf_file_offset + 8 <= image.length)
        {
            BUSTER_TEST(arguments, link_read_u64(image.pointer, dwarf_file_offset) >= UINT64_C(0x100000000));
        }
    }
    ObjectFile ios_mach_object = aarch64_mach_object;
    ios_mach_object.target.os = OPERATING_SYSTEM_IOS;
    String8 ios_mach_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-ios-macho-test"), S8(""));
    NativeExecutableLinkResult ios_mach_executable = link_native_executable(arguments->arena, &ios_mach_object,
                                                                            (NativeExecutableLinkOptions){
                                                                                .output_path = ios_mach_output_path,
                                                                                .entry_symbol = S8("main"),
                                                                            });
    BUSTER_TEST(arguments, ios_mach_executable.error == LINK_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64
    ObjectFile x86_mach_object = linked.object;
    x86_mach_object.target.os = OPERATING_SYSTEM_MACOS;
    String8 x86_mach_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-x86-macho-test"), S8(""));
    NativeExecutableLinkResult x86_mach_executable = link_native_executable(arguments->arena, &x86_mach_object,
                                                                            (NativeExecutableLinkOptions){
                                                                                .output_path = x86_mach_output_path,
                                                                                .entry_symbol = S8("main"),
                                                                            });
    BUSTER_TEST(arguments, x86_mach_executable.error == LINK_ERROR_NONE);
    u8 x86_mach_libc_text[] = {
        0x48, 0x83, 0xec, 0x08, 0xbf, 0xd6, 0xff, 0xff, 0xff, 0xe8, 0, 0, 0, 0, 0x83, 0xe8, 42, 0x48, 0x83, 0xc4, 0x08, 0xc3,
    };
    ObjectSymbol x86_mach_libc_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(x86_mach_libc_text),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("abs"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation x86_mach_libc_relocation = {
        .addend = -4,
        .offset = 10,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    ObjectFile x86_mach_libc_object = link_test_object_make(arguments->arena, x86_mach_object.target, (ByteSlice)BUSTER_ARRAY_TO_SLICE(x86_mach_libc_text),
                                                            x86_mach_libc_symbols, BUSTER_ARRAY_LENGTH(x86_mach_libc_symbols), &x86_mach_libc_relocation, 1);
    String8 x86_mach_libc_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-x86-macho-libc-test"), S8(""));
    NativeExecutableLinkResult x86_mach_libc_executable = link_native_executable(arguments->arena, &x86_mach_libc_object,
                                                                                 (NativeExecutableLinkOptions){
                                                                                     .output_path = x86_mach_libc_output_path,
                                                                                     .entry_symbol = S8("main"),
                                                                                 });
    BUSTER_TEST(arguments, x86_mach_libc_executable.error == LINK_ERROR_NONE);
#endif
#if BUSTER_MACOS
    String8* native_mach_paths = 0;
    u32 native_mach_path_count = 0;
#if BUSTER_CPU_ARCH_X86_64
    String8 x86_native_mach_paths[] = {
        x86_mach_output_path,
        x86_mach_libc_output_path,
    };
    native_mach_paths = x86_native_mach_paths;
    native_mach_path_count = BUSTER_ARRAY_LENGTH(x86_native_mach_paths);
#elif BUSTER_CPU_ARCH_AARCH64
    String8 aarch64_native_mach_paths[] = {
        aarch64_mach_output_path,
        aarch64_mach_data_output_path,
        aarch64_mach_libc_output_path,
    };
    native_mach_paths = aarch64_native_mach_paths;
    native_mach_path_count = BUSTER_ARRAY_LENGTH(aarch64_native_mach_paths);
#endif
    for (u32 path_index = 0; path_index < native_mach_path_count; path_index += 1)
    {
        String8 run_arguments[] = {
            native_mach_paths[path_index],
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                    (ProcessSpawnOptions){
                                                        .use_process_environment = true,
                                                    });
        BUSTER_TEST(arguments, spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait = os_process_wait_sync(arguments->arena, spawn);
            BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
#endif
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    String8 native_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-link-test"), S8(""));
    NativeExecutableLinkResult native_executable = link_native_executable(arguments->arena, &cfi_linked.object,
                                                                          (NativeExecutableLinkOptions){
                                                                              .output_path = native_output_path,
                                                                              .entry_symbol = S8("main"),
                                                                          });
    BUSTER_TEST(arguments, native_executable.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments, native_executable.executable.length >= 4);
    if (native_executable.error == LINK_ERROR_NONE)
    {
        u32 unwind_section_index = 0;
        u64 unwind_section_header = 0;
        u32 unwind_header_section_index = 0;
        u64 unwind_header_section_header = 0;
        u32 text_section_index = 0;
        u64 text_section_header = 0;
        bool unwind_section_found = link_test_elf_section_find(native_executable.executable, S8(".eh_frame"), &unwind_section_index, &unwind_section_header);
        bool unwind_header_section_found =
            link_test_elf_section_find(native_executable.executable, S8(".eh_frame_hdr"), &unwind_header_section_index, &unwind_header_section_header);
        bool text_section_found = link_test_elf_section_find(native_executable.executable, S8(".text"), &text_section_index, &text_section_header);
        BUSTER_TEST(arguments, unwind_section_found);
        BUSTER_TEST(arguments, unwind_header_section_found);
        BUSTER_TEST(arguments, text_section_found);
        if (unwind_section_found && unwind_header_section_found && text_section_found)
        {
            u64 unwind_flags = link_read_u64(native_executable.executable.pointer, unwind_section_header + 8);
            u64 unwind_address = link_read_u64(native_executable.executable.pointer, unwind_section_header + 16);
            u64 unwind_offset = link_read_u64(native_executable.executable.pointer, unwind_section_header + 24);
            u64 unwind_size = link_read_u64(native_executable.executable.pointer, unwind_section_header + 32);
            u64 unwind_header_address = link_read_u64(native_executable.executable.pointer, unwind_header_section_header + 16);
            u64 unwind_header_offset = link_read_u64(native_executable.executable.pointer, unwind_header_section_header + 24);
            u64 unwind_header_size = link_read_u64(native_executable.executable.pointer, unwind_header_section_header + 32);
            u64 text_address = link_read_u64(native_executable.executable.pointer, text_section_header + 16);
            BUSTER_TEST(arguments, unwind_flags == 0x2);
            BUSTER_TEST(arguments, unwind_size == cfi_linked.object.sections[OBJECT_SECTION_UNWIND].data.length);
            BUSTER_TEST(arguments, unwind_header_size == 28);
            if (unwind_header_offset + unwind_header_size <= native_executable.executable.length)
            {
                u8* header = native_executable.executable.pointer + unwind_header_offset;
                BUSTER_TEST(arguments, header[0] == 1 && header[1] == 0x1b && header[2] == 0x03 && header[3] == 0x3b);
                BUSTER_TEST(arguments, link_read_u32(header, 8) == 2);
                s32 frame_displacement = 0;
                s32 first_function = 0;
                s32 first_fde = 0;
                s32 second_function = 0;
                s32 second_fde = 0;
                memcpy(&frame_displacement, header + 4, sizeof(frame_displacement));
                memcpy(&first_function, header + 12, sizeof(first_function));
                memcpy(&first_fde, header + 16, sizeof(first_fde));
                memcpy(&second_function, header + 20, sizeof(second_function));
                memcpy(&second_fde, header + 24, sizeof(second_fde));
                BUSTER_TEST(arguments, (s64)unwind_header_address + 4 + frame_displacement == (s64)unwind_address);
                BUSTER_TEST(arguments, first_function < second_function);
                BUSTER_TEST(arguments,
                            (s64)unwind_header_address + first_fde ==
                                (s64)unwind_address + (s64)answer_cfi.relocations[0].offset - 8);
                BUSTER_TEST(arguments,
                            (s64)unwind_header_address + second_fde ==
                                (s64)unwind_address + (s64)answer_cfi.bytes.length + (s64)main_cfi.relocations[0].offset - 8);
            }
            else
            {
                BUSTER_TEST(arguments, false);
            }
            bool unwind_program_header_found = false;
            u64 program_header_offset = link_read_u64(native_executable.executable.pointer, 32);
            u32 program_header_size = native_executable.executable.pointer[54] | ((u32)native_executable.executable.pointer[55] << 8);
            u32 program_header_count = native_executable.executable.pointer[56] | ((u32)native_executable.executable.pointer[57] << 8);
            for (u32 program_header_index = 0; program_header_index < program_header_count; program_header_index += 1)
            {
                u64 header_offset = program_header_offset + (u64)program_header_index * program_header_size;
                if (header_offset + program_header_size <= native_executable.executable.length &&
                    link_read_u32(native_executable.executable.pointer, header_offset) == 0x6474e550)
                {
                    unwind_program_header_found = link_read_u64(native_executable.executable.pointer, header_offset + 8) == unwind_header_offset &&
                                                  link_read_u64(native_executable.executable.pointer, header_offset + 32) == unwind_header_size;
                }
            }
            BUSTER_TEST(arguments, unwind_program_header_found);
            if (unwind_offset + answer_cfi.relocations[0].offset + 4 <= native_executable.executable.length)
            {
                s32 displacement = 0;
                memcpy(&displacement, native_executable.executable.pointer + unwind_offset + answer_cfi.relocations[0].offset, sizeof(displacement));
                BUSTER_TEST(arguments,
                            (s64)unwind_address + (s64)answer_cfi.relocations[0].offset + displacement ==
                                (s64)text_address + (s64)cfi_linked.object.symbols[0].value);
            }
            else
            {
                BUSTER_TEST(arguments, false);
            }
        }
        String8 run_arguments[] = {
            native_output_path,
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                    (ProcessSpawnOptions){
                                                        .use_process_environment = true,
                                                    });
        BUSTER_TEST(arguments, spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait = os_process_wait_sync(arguments->arena, spawn);
            BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    NativeExecutableLinkResult unresolved_native = link_native_executable(arguments->arena, &permitted.object, (NativeExecutableLinkOptions){0});
    BUSTER_TEST(arguments, unresolved_native.error == LINK_ERROR_NONE);
    u8 libc_main_text[] = {
        0x48, 0x83, 0xec, 0x08, 0xbf, 0xd6, 0xff, 0xff, 0xff, 0xe8, 0, 0, 0, 0, 0x83, 0xe8, 42, 0x48, 0x83, 0xc4, 0x08, 0xc3,
    };
    ObjectSymbol libc_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(libc_main_text),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("abs"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation libc_relocation = {
        .addend = -4,
        .offset = 10,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    ObjectFile libc_object = link_test_object_make(arguments->arena, target, (ByteSlice)BUSTER_ARRAY_TO_SLICE(libc_main_text), libc_symbols,
                                                   BUSTER_ARRAY_LENGTH(libc_symbols), &libc_relocation, 1);
    String8 libc_output_path = link_test_temporary_executable_path(arguments->arena, S8("buster-native-libc-link-test"), S8(""));
    NativeExecutableLinkResult libc_executable = link_native_executable(arguments->arena, &libc_object,
                                                                        (NativeExecutableLinkOptions){
                                                                            .output_path = libc_output_path,
                                                                            .entry_symbol = S8("main"),
                                                                        });
    BUSTER_TEST(arguments, libc_executable.error == LINK_ERROR_NONE);
    if (libc_executable.error == LINK_ERROR_NONE)
    {
        String8 run_arguments[] = {
            libc_output_path,
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                    (ProcessSpawnOptions){
                                                        .use_process_environment = true,
                                                    });
        BUSTER_TEST(arguments, spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait = os_process_wait_sync(arguments->arena, spawn);
            BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
#endif
#else
    BUSTER_UNUSED(target);
#endif
    return result;
}
