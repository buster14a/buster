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
    NativeExecutableLinkResult native_executable = link_native_executable(arguments->arena, &linked.object,
                                                                          (NativeExecutableLinkOptions){
                                                                              .output_path = native_output_path,
                                                                              .entry_symbol = S8("main"),
                                                                          });
    BUSTER_TEST(arguments, native_executable.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments, native_executable.executable.length >= 4);
    if (native_executable.error == LINK_ERROR_NONE)
    {
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
