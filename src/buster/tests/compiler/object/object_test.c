#include <buster/tests/compiler/object/object_test.h>

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

BUSTER_TEST_F_DECL UnitTestResult object_tests(UnitTestArguments* arguments)
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
    return result;
}
