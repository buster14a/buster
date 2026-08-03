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
    bool separate_symbol_is_undefined = false;
    if (separate_object.symbols && separate_object.symbol_count >= 2)
    {
        separate_symbol_is_undefined = separate_object.symbols[1].section == OBJECT_SECTION_UNDEFINED;
    }
    BUSTER_TEST(arguments, separate_symbol_is_undefined);
    BUSTER_TEST(arguments, separate_object.relocation_count == 1);
    ObjectArtifact separate_elf = object_write(arguments->arena, &separate_object, OBJECT_FORMAT_ELF64);
    BUSTER_TEST(arguments, separate_elf.error == OBJECT_ERROR_NONE);
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
