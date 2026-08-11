#include <buster/tests/compiler/jit/jit_test.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#if BUSTER_INCLUDE_TESTS

#if (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64) && !BUSTER_SANITIZE && !BUSTER_IOS && !BUSTER_ANDROID
BUSTER_GLOBAL_LOCAL u64 jit_test_host_value(void)
{
    return 73;
}
#endif

BUSTER_GLOBAL_LOCAL ObjectFile jit_test_object(ObjectSection* sections, u32 section_count)
{
    return (ObjectFile){
        .sections = sections,
        .target = target_native,
        .section_count = section_count,
    };
}

UnitTestResult jit_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {0};

    JitProgram null_program = jit_link_object(0, (JitOptions){0});
    BUSTER_TEST(arguments, null_program.error == JIT_ERROR_INVALID_INPUT);
    BUSTER_TEST(arguments, !null_program.allocation_base);

    u8 text_byte = 0xc3;
    ObjectSection text_section = {
        .name = S8(".text"),
        .data = {.pointer = &text_byte, .length = 1},
        .kind = OBJECT_SECTION_TEXT,
        .alignment = 1,
    };
    ObjectFile valid_object = jit_test_object(&text_section, 1);
    ObjectFile invalid_object = valid_object;
    invalid_object.error = OBJECT_ERROR_INVALID_INPUT;
    BUSTER_TEST(arguments, jit_link_object(&invalid_object, (JitOptions){0}).error == JIT_ERROR_INVALID_INPUT);

    ObjectFile foreign_object = valid_object;
    foreign_object.target.os = target_native.os == OPERATING_SYSTEM_LINUX ? OPERATING_SYSTEM_WINDOWS : OPERATING_SYSTEM_LINUX;
    BUSTER_TEST(arguments, jit_link_object(&foreign_object, (JitOptions){0}).error == JIT_ERROR_FOREIGN_TARGET);
    ObjectFile invalid_target_object = valid_object;
    invalid_target_object.target.cpu_model = CPU_MODEL_ERROR;
    BUSTER_TEST(arguments, jit_link_object(&invalid_target_object, (JitOptions){0}).error == JIT_ERROR_FOREIGN_TARGET);
#if BUSTER_CPU_ARCH_X86_64
    Target incompatible_target = target_native;
    TargetCpuFeatures native_features = target_cpu_features_effective(target_native);
    bool incompatible_target_found = false;
    for (u32 feature = (u32)TARGET_CPU_FEATURE_NONE + 1; feature < (u32)TARGET_CPU_FEATURE_COUNT; feature += 1)
    {
        Target candidate = target_native;
        candidate.cpu_features_explicit = true;
        candidate.cpu_features = target_cpu_features_add(native_features, (TargetCpuFeature)feature);
        if (!target_cpu_features_contains(native_features, (TargetCpuFeature)feature) && target_cpu_features_are_valid(candidate))
        {
            incompatible_target = candidate;
            incompatible_target_found = true;
            break;
        }
    }
    BUSTER_TEST(arguments, incompatible_target_found);
    ObjectFile incompatible_target_object = valid_object;
    incompatible_target_object.target = incompatible_target;
    BUSTER_TEST(arguments, jit_link_object(&incompatible_target_object, (JitOptions){0}).error == JIT_ERROR_FOREIGN_TARGET);
#endif

    ObjectSymbol invalid_name_symbol = {
        .name = {.length = 1},
        .size = 1,
        .section = 0,
        .kind = OBJECT_SYMBOL_DATA,
    };
    ObjectFile invalid_name_object = valid_object;
    invalid_name_object.symbols = &invalid_name_symbol;
    invalid_name_object.symbol_count = 1;
    BUSTER_TEST(arguments, jit_link_object(&invalid_name_object, (JitOptions){0}).error == JIT_ERROR_INVALID_INPUT);

#if !BUSTER_MACOS && !BUSTER_IOS && !BUSTER_ANDROID
    JitProgram missing_program = jit_link_object(&valid_object, (JitOptions){0});
    BUSTER_TEST(arguments, missing_program.error == JIT_ERROR_NONE);
    BUSTER_TEST(arguments, !jit_program_symbol(&missing_program, S8("missing_symbol")));
    BUSTER_TEST(arguments, missing_program.error == JIT_ERROR_SYMBOL_NOT_FOUND);
    BUSTER_STRING_TEST(arguments, missing_program.failing_symbol, S8("missing_symbol"));
    jit_program_release(&missing_program);
    BUSTER_TEST(arguments, !missing_program.allocation_base && !missing_program.object);

    ObjectSymbol out_of_bounds_symbol = {
        .name = S8("out_of_bounds"),
        .value = 2,
        .size = 1,
        .section = 0,
        .kind = OBJECT_SYMBOL_FUNCTION,
        .global = true,
    };
    ObjectFile bounds_object = valid_object;
    bounds_object.symbols = &out_of_bounds_symbol;
    bounds_object.symbol_count = 1;
    JitProgram bounds_program = jit_link_object(&bounds_object, (JitOptions){0});
    BUSTER_TEST(arguments, bounds_program.error == JIT_ERROR_NONE);
    BUSTER_TEST(arguments, !jit_program_symbol(&bounds_program, out_of_bounds_symbol.name));
    BUSTER_TEST(arguments, bounds_program.error == JIT_ERROR_SYMBOL_BOUNDS);
    BUSTER_STRING_TEST(arguments, bounds_program.failing_symbol, out_of_bounds_symbol.name);
    jit_program_release(&bounds_program);
#endif

    u8 relocation_bytes[8] = {0};
    ObjectSection relocation_section = {
        .name = S8(".text"),
        .data = BUSTER_ARRAY_TO_SLICE(relocation_bytes),
        .kind = OBJECT_SECTION_TEXT,
        .alignment = 4,
    };
    ObjectSymbol imported_function = {
        .name = S8("jit_test_missing_import"),
        .section = OBJECT_SECTION_UNDEFINED,
        .kind = OBJECT_SYMBOL_FUNCTION,
        .global = true,
    };
    ObjectRelocation imported_relocation = {
#if BUSTER_CPU_ARCH_X86_64
        .addend = -4,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
#else
        .kind = OBJECT_RELOCATION_AARCH64_CALL26,
#endif
        .section = 0,
        .symbol = 0,
    };
    ObjectFile imported_object = jit_test_object(&relocation_section, 1);
    imported_object.symbols = &imported_function;
    imported_object.symbol_count = 1;
    imported_object.relocations = &imported_relocation;
    imported_object.relocation_count = 1;
    JitProgram unresolved_program = jit_link_object(&imported_object, (JitOptions){0});
    BUSTER_TEST(arguments, unresolved_program.error == JIT_ERROR_UNRESOLVED_IMPORT);
    BUSTER_STRING_TEST(arguments, unresolved_program.failing_symbol, imported_function.name);
    BUSTER_TEST(arguments, !unresolved_program.allocation_base);

    JitHostBinding wrong_kind_binding = {
        .name = imported_function.name,
        .address = &text_byte,
        .kind = OBJECT_SYMBOL_DATA,
    };
    JitProgram wrong_kind_program = jit_link_object(&imported_object, (JitOptions){.bindings = &wrong_kind_binding, .binding_count = 1});
    BUSTER_TEST(arguments, wrong_kind_program.error == JIT_ERROR_BINDING_KIND);
    BUSTER_STRING_TEST(arguments, wrong_kind_program.failing_symbol, imported_function.name);

    // Exercise the AArch64 PAGE word patcher on every host architecture.  The
    // encoding is byte-level and does not require executing AArch64 code, so
    // malformed words, register-independent ADD, and LLVM's scaled LD/ST
    // forms remain covered on x86/Linux as well as the native Apple path below.
    u32 page21_word = UINT32_C(0x90000009);
    u32 expected_page21 = 0;
    bool expected_page21_valid = a64_adrp_encode(9, UINT64_C(0x1000), UINT64_C(0x12346000), &expected_page21);
    bool page21_applied = jit_apply_aarch64_mach_page_relocation(OBJECT_RELOCATION_AARCH64_MACH_PAGE21, (u8*)&page21_word,
                                                                  UINT64_C(0x1000), UINT64_C(0x12345000), 0x1000);
    BUSTER_TEST(arguments, expected_page21_valid && page21_applied && page21_word == expected_page21);
    u32 page21_misaligned = UINT32_C(0x90000009);
    BUSTER_TEST(arguments, !jit_apply_aarch64_mach_page_relocation(OBJECT_RELOCATION_AARCH64_MACH_PAGE21, (u8*)&page21_misaligned,
                                                                    UINT64_C(0x1002), UINT64_C(0x12345000), 0));
    u32 page21_stale = UINT32_C(0x90000029);
    BUSTER_TEST(arguments, !jit_apply_aarch64_mach_page_relocation(OBJECT_RELOCATION_AARCH64_MACH_PAGE21, (u8*)&page21_stale,
                                                                    UINT64_C(0x1000), UINT64_C(0x12345000), 0));
    u32 page21_xzr = UINT32_C(0x9000001f);
    u32 expected_page21_xzr = 0;
    bool expected_page21_xzr_valid = a64_adrp_encode(31, UINT64_C(0x1000), UINT64_C(0x12345000), &expected_page21_xzr);
    bool page21_xzr_applied = jit_apply_aarch64_mach_page_relocation(OBJECT_RELOCATION_AARCH64_MACH_PAGE21, (u8*)&page21_xzr,
                                                                      UINT64_C(0x1000), UINT64_C(0x12345000), 0);
    BUSTER_TEST(arguments, expected_page21_xzr_valid && page21_xzr_applied && page21_xzr == expected_page21_xzr);

    struct JitPageOffCase
    {
        u32 word;
        u64 target;
        s64 addend;
        u32 shift;
    } pageoff_cases[] = {
        {UINT32_C(0x91000041), UINT64_C(0x40001234), 4, 0},  // add x1, x2, #imm
        {UINT32_C(0xf9400083), UINT64_C(0x40001030), 8, 3},  // ldr x3, [x4]
        {UINT32_C(0x394000c5), UINT64_C(0x400012ab), 0, 0},  // ldrb w5, [x6]
        {UINT32_C(0x79400107), UINT64_C(0x400012a8), 0, 1},  // ldrh w7, [x8]
        {UINT32_C(0x3dc0020f), UINT64_C(0x40001030), 0x10, 4}, // ldr q15, [x16]
        {UINT32_C(0x3d80024f), UINT64_C(0x40001040), 0, 4},  // str q15, [x18]
    };
    for (u32 pageoff_index = 0; pageoff_index < BUSTER_ARRAY_LENGTH(pageoff_cases); pageoff_index += 1)
    {
        struct JitPageOffCase pageoff_case = pageoff_cases[pageoff_index];
        u32 word = pageoff_case.word;
        u64 address = pageoff_case.target + (u64)pageoff_case.addend;
        u32 expected = (word & ~(UINT32_C(0xfff) << 10)) |
                       (u32)((address & 0xfff) >> pageoff_case.shift) << 10;
        bool applied = jit_apply_aarch64_mach_page_relocation(OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12, (u8*)&word,
                                                               UINT64_C(0x2000), pageoff_case.target, pageoff_case.addend);
        BUSTER_TEST(arguments, applied && word == expected);
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
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(valid_pageoff_bases); index += 1)
    {
        u32 word = valid_pageoff_bases[index];
        BUSTER_TEST(arguments, jit_apply_aarch64_mach_page_relocation(OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12, (u8*)&word,
                                                                        UINT64_C(0x2000), UINT64_C(0x40001000), 0));
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid_pageoff_bases); index += 1)
    {
        u32 word = invalid_pageoff_bases[index];
        BUSTER_TEST(arguments, !jit_apply_aarch64_mach_page_relocation(OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12, (u8*)&word,
                                                                         UINT64_C(0x2000), UINT64_C(0x40001000), 0));
    }
    u32 pageoff_stale = UINT32_C(0x91000441);
    BUSTER_TEST(arguments, !jit_apply_aarch64_mach_page_relocation(OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12, (u8*)&pageoff_stale,
                                                                    UINT64_C(0x2000), UINT64_C(0x40001000), 0));
    u32 pageoff_wrong_opcode = UINT32_C(0x14000000);
    BUSTER_TEST(arguments, !jit_apply_aarch64_mach_page_relocation(OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12, (u8*)&pageoff_wrong_opcode,
                                                                    UINT64_C(0x2000), UINT64_C(0x40001000), 0));
    u32 pageoff_misaligned = UINT32_C(0xf9400083);
    BUSTER_TEST(arguments, !jit_apply_aarch64_mach_page_relocation(OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12, (u8*)&pageoff_misaligned,
                                                                    UINT64_C(0x2002), UINT64_C(0x40001000), 0));
    u32 pageoff_unaligned_scale = UINT32_C(0xf9400083);
    BUSTER_TEST(arguments, !jit_apply_aarch64_mach_page_relocation(OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12, (u8*)&pageoff_unaligned_scale,
                                                                    UINT64_C(0x2000), UINT64_C(0x40001001), 0));
    u32 pageoff_overflow = UINT32_C(0x91000041);
    BUSTER_TEST(arguments, !jit_apply_aarch64_mach_page_relocation(OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12, (u8*)&pageoff_overflow,
                                                                    UINT64_C(0x2000), 0, -1));
    BUSTER_TEST(arguments, !jit_apply_aarch64_mach_page_relocation(OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12, (u8*)&pageoff_overflow,
                                                                    UINT64_C(0x2000), UINT64_MAX, 1));

    // Darwin AArch64 direct PAGE relocations are valid function-address
    // materializations.  Keep this fixture target-gated so an x86/Linux host
    // still exercises the structural foreign-target path without attempting
    // to execute AArch64 code.
    u32 mach_page_words[] = {UINT32_C(0x90000009), UINT32_C(0x91000041)};
    ObjectSection mach_page_section = {
        .name = S8(".text"),
        .data = {.pointer = (u8*)mach_page_words, .length = sizeof(mach_page_words)},
        .kind = OBJECT_SECTION_TEXT,
        .alignment = 4,
    };
    ObjectSymbol mach_page_symbol = {
        .name = S8("jit_mach_page_function"),
        .section = OBJECT_SECTION_UNDEFINED,
        .kind = OBJECT_SYMBOL_FUNCTION,
        .global = true,
    };
    ObjectRelocation mach_page_relocations[] = {
        {
            .section = 0,
            .symbol = 0,
            .kind = OBJECT_RELOCATION_AARCH64_MACH_PAGE21,
        },
        {
            .addend = 4,
            .offset = sizeof(u32),
            .section = 0,
            .symbol = 0,
            .kind = OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12,
        },
    };
    ObjectFile mach_page_object = {
        .sections = &mach_page_section,
        .section_count = 1,
        .symbols = &mach_page_symbol,
        .symbol_count = 1,
        .relocations = mach_page_relocations,
        .relocation_count = BUSTER_ARRAY_LENGTH(mach_page_relocations),
        .target = target_native,
    };
#if BUSTER_CPU_ARCH_AARCH64 && (BUSTER_MACOS || BUSTER_IOS)
    u64 mach_page_binding_address = 0;
    JitHostBinding mach_page_binding = {
        .name = mach_page_symbol.name,
        .address = &mach_page_binding_address,
        .kind = OBJECT_SYMBOL_FUNCTION,
    };
    JitProgram mach_page_program = jit_link_object(&mach_page_object,
                                                    (JitOptions){.bindings = &mach_page_binding, .binding_count = 1});
    BUSTER_TEST(arguments, mach_page_program.error == JIT_ERROR_NONE);
    jit_program_release(&mach_page_program);
#else
    mach_page_object.target.cpu_arch = CPU_ARCH_AARCH64;
    mach_page_object.target.os = OPERATING_SYSTEM_MACOS;
    JitProgram mach_page_program = jit_link_object(&mach_page_object, (JitOptions){0});
    BUSTER_TEST(arguments, mach_page_program.error == JIT_ERROR_FOREIGN_TARGET);
#endif

    ObjectSymbol imported_data = imported_function;
    imported_data.name = S8("jit_test_external_data");
    imported_data.kind = OBJECT_SYMBOL_DATA;
    ObjectRelocation absolute_import = imported_relocation;
    absolute_import.addend = 0;
    absolute_import.kind = OBJECT_RELOCATION_ABSOLUTE64;
    ObjectFile external_data_object = imported_object;
    external_data_object.symbols = &imported_data;
    external_data_object.relocations = &absolute_import;
    JitProgram external_data_program = jit_link_object(&external_data_object, (JitOptions){0});
#if BUSTER_CPU_ARCH_X86_64
    BUSTER_TEST(arguments, external_data_program.error == JIT_ERROR_UNRESOLVED_IMPORT);
#else
    BUSTER_TEST(arguments, external_data_program.error == JIT_ERROR_EXTERNAL_DATA);
#endif
    BUSTER_STRING_TEST(arguments, external_data_program.failing_symbol, imported_data.name);
    BUSTER_TEST(arguments, !external_data_program.allocation_base);
    jit_program_release(&external_data_program);
    BUSTER_TEST(arguments, !external_data_program.allocation_base && !external_data_program.object);

#if BUSTER_CPU_ARCH_X86_64
    u64 external_data_value = UINT64_C(0x1122334455667788);
    JitHostBinding external_data_binding = {
        .name = imported_data.name,
        .address = &external_data_value,
        .kind = OBJECT_SYMBOL_DATA,
    };
    ObjectRelocation unsupported_data_relocation = imported_relocation;
    ObjectFile unsupported_data_object = external_data_object;
    unsupported_data_object.relocations = &unsupported_data_relocation;
    JitProgram unsupported_data_program =
        jit_link_object(&unsupported_data_object, (JitOptions){.bindings = &external_data_binding, .binding_count = 1});
    BUSTER_TEST(arguments, unsupported_data_program.error == JIT_ERROR_EXTERNAL_DATA);
    BUSTER_STRING_TEST(arguments, unsupported_data_program.failing_symbol, imported_data.name);
    BUSTER_TEST(arguments, !unsupported_data_program.allocation_base);
    jit_program_release(&unsupported_data_program);
    BUSTER_TEST(arguments, !unsupported_data_program.allocation_base && !unsupported_data_program.object);

    JitHostBinding wrong_kind_data_binding = external_data_binding;
    wrong_kind_data_binding.kind = OBJECT_SYMBOL_FUNCTION;
    JitProgram wrong_kind_data_program =
        jit_link_object(&external_data_object, (JitOptions){.bindings = &wrong_kind_data_binding, .binding_count = 1});
    BUSTER_TEST(arguments, wrong_kind_data_program.error == JIT_ERROR_BINDING_KIND);
    BUSTER_STRING_TEST(arguments, wrong_kind_data_program.failing_symbol, imported_data.name);
    BUSTER_TEST(arguments, !wrong_kind_data_program.allocation_base);
    jit_program_release(&wrong_kind_data_program);
    BUSTER_TEST(arguments, !wrong_kind_data_program.allocation_base && !wrong_kind_data_program.object);

    JitHostBinding null_data_binding = external_data_binding;
    null_data_binding.address = 0;
    JitProgram null_data_program =
        jit_link_object(&external_data_object, (JitOptions){.bindings = &null_data_binding, .binding_count = 1});
    BUSTER_TEST(arguments, null_data_program.error == JIT_ERROR_INVALID_BINDING);
    BUSTER_STRING_TEST(arguments, null_data_program.failing_symbol, imported_data.name);
    BUSTER_TEST(arguments, !null_data_program.allocation_base);
    jit_program_release(&null_data_program);
    BUSTER_TEST(arguments, !null_data_program.allocation_base && !null_data_program.object);

    // Keep successful direct-link coverage off every macOS build: Apple's public
    // MAP_JIT contract permits only one region per process, while this test module
    // already links the native JIT fixture below.
#if !BUSTER_SANITIZE && !BUSTER_MACOS && !BUSTER_IOS && !BUSTER_ANDROID
    JitProgram bound_data_program =
        jit_link_object(&external_data_object, (JitOptions){.bindings = &external_data_binding, .binding_count = 1});
    BUSTER_TEST(arguments, bound_data_program.error == JIT_ERROR_NONE);
    BUSTER_TEST(arguments, bound_data_program.allocation_base && bound_data_program.section_addresses[0]);
    if (bound_data_program.error == JIT_ERROR_NONE && bound_data_program.section_addresses[0])
    {
        u64 linked_address = 0;
        memcpy(&linked_address, bound_data_program.section_addresses[0], sizeof(linked_address));
        BUSTER_TEST(arguments, linked_address == (u64)(uintptr_t)&external_data_value);
        external_data_value = UINT64_C(0x8877665544332211);
        u64 linked_value = 0;
        memcpy(&linked_value, (void*)(uintptr_t)linked_address, sizeof(linked_value));
        BUSTER_TEST(arguments, linked_value == external_data_value);
    }
    jit_program_release(&bound_data_program);
    BUSTER_TEST(arguments, !bound_data_program.allocation_base && !bound_data_program.object);

    absolute_import.addend = 8;
    JitProgram positive_addend_program =
        jit_link_object(&external_data_object, (JitOptions){.bindings = &external_data_binding, .binding_count = 1});
    BUSTER_TEST(arguments, positive_addend_program.error == JIT_ERROR_NONE);
    if (positive_addend_program.error == JIT_ERROR_NONE && positive_addend_program.section_addresses[0])
    {
        u64 linked_address = 0;
        memcpy(&linked_address, positive_addend_program.section_addresses[0], sizeof(linked_address));
        BUSTER_TEST(arguments, linked_address == (u64)(uintptr_t)&external_data_value + 8);
    }
    jit_program_release(&positive_addend_program);
    BUSTER_TEST(arguments, !positive_addend_program.allocation_base && !positive_addend_program.object);

    absolute_import.addend = -8;
    JitProgram negative_addend_program =
        jit_link_object(&external_data_object, (JitOptions){.bindings = &external_data_binding, .binding_count = 1});
    BUSTER_TEST(arguments, negative_addend_program.error == JIT_ERROR_NONE);
    if (negative_addend_program.error == JIT_ERROR_NONE && negative_addend_program.section_addresses[0])
    {
        u64 linked_address = 0;
        memcpy(&linked_address, negative_addend_program.section_addresses[0], sizeof(linked_address));
        BUSTER_TEST(arguments, linked_address == (u64)(uintptr_t)&external_data_value - 8);
    }
    jit_program_release(&negative_addend_program);
    BUSTER_TEST(arguments, !negative_addend_program.allocation_base && !negative_addend_program.object);

    JitHostBinding overflow_data_binding = external_data_binding;
    overflow_data_binding.address = (void*)(uintptr_t)UINT64_MAX;
    absolute_import.addend = 1;
    JitProgram overflow_data_program =
        jit_link_object(&external_data_object, (JitOptions){.bindings = &overflow_data_binding, .binding_count = 1});
    BUSTER_TEST(arguments, overflow_data_program.error == JIT_ERROR_CAPACITY);
    BUSTER_STRING_TEST(arguments, overflow_data_program.failing_symbol, imported_data.name);
    BUSTER_TEST(arguments, !overflow_data_program.allocation_base && !overflow_data_program.auxiliary_allocation_base);
    jit_program_release(&overflow_data_program);
    BUSTER_TEST(arguments, !overflow_data_program.allocation_base && !overflow_data_program.object);

    JitHostBinding underflow_data_binding = external_data_binding;
    underflow_data_binding.address = (void*)(uintptr_t)1;
    absolute_import.addend = -2;
    JitProgram underflow_data_program =
        jit_link_object(&external_data_object, (JitOptions){.bindings = &underflow_data_binding, .binding_count = 1});
    BUSTER_TEST(arguments, underflow_data_program.error == JIT_ERROR_CAPACITY);
    BUSTER_STRING_TEST(arguments, underflow_data_program.failing_symbol, imported_data.name);
    BUSTER_TEST(arguments, !underflow_data_program.allocation_base && !underflow_data_program.auxiliary_allocation_base);
    jit_program_release(&underflow_data_program);
    BUSTER_TEST(arguments, !underflow_data_program.allocation_base && !underflow_data_program.object);
    absolute_import.addend = 0;
#endif
#endif

#if BUSTER_CPU_ARCH_AARCH64 && (BUSTER_MACOS || BUSTER_IOS)
    // An unbound external DATA symbol is rejected consistently for every
    // supported AArch64 direct relocation.  Keep the pair as one fixture so
    // the PAGE21/PAGEOFF12 contract cannot silently diverge.
    u32 unbound_data_words[] = {UINT32_C(0x90000009), UINT32_C(0x91000041)};
    ObjectSection unbound_data_section = {
        .name = S8(".text"),
        .data = {.pointer = (u8*)unbound_data_words, .length = sizeof(unbound_data_words)},
        .kind = OBJECT_SECTION_TEXT,
        .alignment = 4,
    };
    ObjectSymbol unbound_data_symbol = {
        .name = S8("jit_test_unbound_page_data"),
        .section = OBJECT_SECTION_UNDEFINED,
        .kind = OBJECT_SYMBOL_DATA,
        .global = true,
    };
    ObjectRelocation unbound_data_relocations[] = {
        {
            .offset = 0,
            .section = 0,
            .symbol = 0,
            .kind = OBJECT_RELOCATION_AARCH64_MACH_PAGE21,
        },
        {
            .offset = sizeof(u32),
            .section = 0,
            .symbol = 0,
            .kind = OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12,
        },
    };
    ObjectFile unbound_data_object = {
        .sections = &unbound_data_section,
        .section_count = 1,
        .symbols = &unbound_data_symbol,
        .symbol_count = 1,
        .relocations = unbound_data_relocations,
        .relocation_count = BUSTER_ARRAY_LENGTH(unbound_data_relocations),
        .target = target_native,
    };
    JitProgram unbound_page_pair_program = jit_link_object(&unbound_data_object, (JitOptions){0});
    BUSTER_TEST(arguments, unbound_page_pair_program.error == JIT_ERROR_EXTERNAL_DATA);
    BUSTER_STRING_TEST(arguments, unbound_page_pair_program.failing_symbol, unbound_data_symbol.name);
    BUSTER_TEST(arguments, !unbound_page_pair_program.allocation_base && !unbound_page_pair_program.allocation_size &&
                               !unbound_page_pair_program.auxiliary_allocation_base && !unbound_page_pair_program.auxiliary_allocation_size);
    for (u32 unbound_index = 0; unbound_index < BUSTER_ARRAY_LENGTH(unbound_data_relocations); unbound_index += 1)
    {
        ObjectRelocation unbound_relocation = unbound_data_relocations[unbound_index];
        ObjectFile unbound_object = unbound_data_object;
        unbound_object.relocations = &unbound_relocation;
        unbound_object.relocation_count = 1;
        JitProgram unbound_program = jit_link_object(&unbound_object, (JitOptions){0});
        BUSTER_TEST(arguments, unbound_program.error == JIT_ERROR_EXTERNAL_DATA);
        BUSTER_STRING_TEST(arguments, unbound_program.failing_symbol, unbound_data_symbol.name);
        BUSTER_TEST(arguments, !unbound_program.allocation_base && !unbound_program.allocation_size &&
                                   !unbound_program.auxiliary_allocation_base && !unbound_program.auxiliary_allocation_size);
    }
    ObjectRelocation unbound_prel32_relocation = unbound_data_relocations[0];
    unbound_prel32_relocation.kind = OBJECT_RELOCATION_AARCH64_PREL32;
    ObjectFile unbound_prel32_object = unbound_data_object;
    unbound_prel32_object.relocations = &unbound_prel32_relocation;
    unbound_prel32_object.relocation_count = 1;
    JitProgram unbound_prel32_program = jit_link_object(&unbound_prel32_object, (JitOptions){0});
    BUSTER_TEST(arguments, unbound_prel32_program.error == JIT_ERROR_EXTERNAL_DATA);
    BUSTER_STRING_TEST(arguments, unbound_prel32_program.failing_symbol, unbound_data_symbol.name);
    BUSTER_TEST(arguments, !unbound_prel32_program.allocation_base && !unbound_prel32_program.allocation_size &&
                               !unbound_prel32_program.auxiliary_allocation_base && !unbound_prel32_program.auxiliary_allocation_size);

    // A bound external DATA symbol may be materialized directly by PAGEOFF12.
    // This form has no page-range dependency, making it a stable native-target
    // test even when the host allocator chooses a distant JIT mapping.
    u32 bound_data_word = UINT32_C(0x91000041);
    ObjectSection bound_data_section = unbound_data_section;
    bound_data_section.data = (ByteSlice){.pointer = (u8*)&bound_data_word, .length = sizeof(bound_data_word)};
    ObjectRelocation bound_data_relocation = unbound_data_relocations[1];
    bound_data_relocation.offset = 0;
    bound_data_relocation.addend = 4;
    ObjectFile bound_data_object = unbound_data_object;
    bound_data_object.sections = &bound_data_section;
    bound_data_object.relocations = &bound_data_relocation;
    bound_data_object.relocation_count = 1;
    u64 bound_data_value = 0;
    JitHostBinding bound_data_binding = {
        .name = unbound_data_symbol.name,
        .address = &bound_data_value,
        .kind = OBJECT_SYMBOL_DATA,
    };
    JitProgram bound_data_program = jit_link_object(&bound_data_object,
                                                     (JitOptions){.bindings = &bound_data_binding, .binding_count = 1});
    BUSTER_TEST(arguments, bound_data_program.error == JIT_ERROR_NONE && bound_data_program.allocation_base);
    if (bound_data_program.error == JIT_ERROR_NONE && bound_data_program.section_addresses[0])
    {
        u32 bound_data_expected = UINT32_C(0x91000000) | (u32)(((u64)(uintptr_t)&bound_data_value + 4) & 0xfff) << 10 | (2u << 5) | 1u;
        u32 bound_data_actual = 0;
        memcpy(&bound_data_actual, bound_data_program.section_addresses[0], sizeof(bound_data_actual));
        BUSTER_TEST(arguments, bound_data_actual == bound_data_expected);
    }
    jit_program_release(&bound_data_program);

    // The first PAGEOFF12 patch is intentionally valid, while the following
    // PAGE21 word is malformed.  The failed link must discard the allocation
    // and leave the caller-owned object bytes unchanged.
    u32 transactional_words[] = {UINT32_C(0x91000041), UINT32_C(0x14000000)};
    ObjectSection transactional_section = bound_data_section;
    transactional_section.data = (ByteSlice){.pointer = (u8*)transactional_words, .length = sizeof(transactional_words)};
    ObjectRelocation transactional_relocations[] = {
        bound_data_relocation,
        {
            .offset = sizeof(u32),
            .section = 0,
            .symbol = 0,
            .kind = OBJECT_RELOCATION_AARCH64_MACH_PAGE21,
        },
    };
    ObjectFile transactional_object = bound_data_object;
    transactional_object.sections = &transactional_section;
    transactional_object.relocations = transactional_relocations;
    transactional_object.relocation_count = BUSTER_ARRAY_LENGTH(transactional_relocations);
    u32 transactional_snapshot[BUSTER_ARRAY_LENGTH(transactional_words)];
    memcpy(transactional_snapshot, transactional_words, sizeof(transactional_snapshot));
    JitProgram transactional_program = jit_link_object(&transactional_object,
                                                        (JitOptions){.bindings = &bound_data_binding, .binding_count = 1});
    BUSTER_TEST(arguments, transactional_program.error == JIT_ERROR_CAPACITY);
    BUSTER_STRING_TEST(arguments, transactional_program.failing_symbol, unbound_data_symbol.name);
    BUSTER_TEST(arguments, !transactional_program.allocation_base && !transactional_program.allocation_size &&
                               !transactional_program.auxiliary_allocation_base && !transactional_program.auxiliary_allocation_size &&
                               !transactional_program.executable_size && !transactional_program.section_addresses[0] &&
                               !transactional_program.section_sizes[0] &&
                               memcmp(transactional_words, transactional_snapshot, sizeof(transactional_snapshot)) == 0);
#endif

    u8 tls_byte = 1;
    ObjectSection tls_section = {
        .name = S8(".tdata"),
        .data = {.pointer = &tls_byte, .length = 1},
        .kind = OBJECT_SECTION_THREAD_LOCAL_DATA,
        .alignment = 1,
    };
    ObjectFile tls_object = jit_test_object(&tls_section, 1);
    BUSTER_TEST(arguments, jit_link_object(&tls_object, (JitOptions){0}).error == JIT_ERROR_TLS_UNSUPPORTED);
    ObjectSection tbss_section = {
        .name = S8(".tbss"),
        .virtual_size = 1,
        .kind = OBJECT_SECTION_THREAD_LOCAL_ZERO,
        .alignment = 1,
    };
    ObjectFile tbss_object = jit_test_object(&tbss_section, 1);
    BUSTER_TEST(arguments, jit_link_object(&tbss_object, (JitOptions){0}).error == JIT_ERROR_TLS_UNSUPPORTED);

    ObjectSymbol local_symbol = {
        .name = S8("jit_test_local"),
        .size = 1,
        .section = 0,
        .kind = OBJECT_SYMBOL_DATA,
    };
    ObjectRelocation tls_relocation = {
        .section = 0,
        .symbol = 0,
        .kind = OBJECT_RELOCATION_X86_64_TPOFF32,
    };
    ObjectFile tls_relocation_object = jit_test_object(&relocation_section, 1);
    tls_relocation_object.symbols = &local_symbol;
    tls_relocation_object.symbol_count = 1;
    tls_relocation_object.relocations = &tls_relocation;
    tls_relocation_object.relocation_count = 1;
    JitProgram tls_relocation_program = jit_link_object(&tls_relocation_object, (JitOptions){0});
    BUSTER_TEST(arguments, tls_relocation_program.error == JIT_ERROR_TLS_UNSUPPORTED);
    BUSTER_STRING_TEST(arguments, tls_relocation_program.failing_symbol, local_symbol.name);

    ObjectRelocation unsupported_relocation = tls_relocation;
    unsupported_relocation.kind = OBJECT_RELOCATION_ABSOLUTE32;
    ObjectFile unsupported_object = tls_relocation_object;
    unsupported_object.relocations = &unsupported_relocation;
    JitProgram unsupported_program = jit_link_object(&unsupported_object, (JitOptions){0});
    BUSTER_TEST(arguments, unsupported_program.error == JIT_ERROR_UNSUPPORTED_RELOCATION);
    BUSTER_STRING_TEST(arguments, unsupported_program.failing_symbol, local_symbol.name);

#if !BUSTER_MACOS && !BUSTER_IOS && !BUSTER_ANDROID
    u8 debug_byte = 0;
    ObjectSection ignored_sections[] = {
        text_section,
        {
            .name = S8(".debug_info"),
            .data = {.pointer = &debug_byte, .length = 1},
            .kind = OBJECT_SECTION_DEBUG_INFO,
            .alignment = 1,
        },
    };
    ObjectRelocation ignored_relocation = {
        .offset = UINT64_MAX,
        .section = 1,
        .symbol = UINT32_MAX,
        .kind = OBJECT_RELOCATION_COUNT,
    };
    ObjectFile ignored_object = jit_test_object(ignored_sections, BUSTER_ARRAY_LENGTH(ignored_sections));
    ignored_object.relocations = &ignored_relocation;
    ignored_object.relocation_count = 1;
    JitProgram ignored_program = jit_link_object(&ignored_object, (JitOptions){0});
    BUSTER_TEST(arguments, ignored_program.error == JIT_ERROR_NONE);
    BUSTER_TEST(arguments, !ignored_program.section_addresses[1]);
    jit_program_release(&ignored_program);
#endif

    for (u32 error = 0; error < (u32)JIT_ERROR_COUNT; error += 1)
    {
        String8 diagnostic = jit_error_string((JitError)error);
        BUSTER_TEST(arguments, diagnostic.pointer && diagnostic.length);
    }
    BUSTER_STRING_TEST(arguments, jit_error_string((JitError)JIT_ERROR_COUNT), S8("unknown JIT error"));
    BUSTER_STRING_TEST(arguments, jit_error_string(JIT_ERROR_EXTERNAL_DATA), S8("external data relocation is unsupported for this JIT target"));

#if (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64) && !BUSTER_SANITIZE && !BUSTER_IOS && !BUSTER_ANDROID
    u8 native_text[48] = {0};
#if BUSTER_CPU_ARCH_X86_64
    u8 constant_function[] = {0xb8, 42, 0, 0, 0, 0xc3};
    u8 imported_function_body[] = {
        0x48, 0x83, 0xec,
        target_native.os == OPERATING_SYSTEM_WINDOWS ? 0x28 : 0x08,
        0xe8, 0, 0, 0, 0,
        0x48, 0x83, 0xc4,
        target_native.os == OPERATING_SYSTEM_WINDOWS ? 0x28 : 0x08,
        0xc3,
    };
    memcpy(native_text, constant_function, sizeof(constant_function));
    memset(native_text + sizeof(constant_function), 0x90, 16 - sizeof(constant_function));
    memcpy(native_text + 16, imported_function_body, sizeof(imported_function_body));
    memcpy(native_text + 32, imported_function_body, sizeof(imported_function_body));
    u64 import_relocation_offset = 21;
    u64 internal_relocation_offset = 37;
#else
    u32 constant_function[] = {0x52800540, 0xd65f03c0};
    u32 imported_function_body[] = {0xa9bf7bfd, 0x94000000, 0xa8c17bfd, 0xd65f03c0};
    memcpy(native_text, constant_function, sizeof(constant_function));
    memcpy(native_text + 16, imported_function_body, sizeof(imported_function_body));
    memcpy(native_text + 32, imported_function_body, sizeof(imported_function_body));
    u64 import_relocation_offset = 20;
    u64 internal_relocation_offset = 36;
#endif
    u8 native_read_only_data[8] = {0};
    u64 read_only_value = 17;
    memcpy(native_read_only_data, &read_only_value, sizeof(read_only_value));
    u8 native_data[24] = {0};
    u64 initial_data = 11;
    memcpy(native_data, &initial_data, sizeof(initial_data));
#if BUSTER_MACOS
    u8 native_debug_byte = 0;
#endif
    ObjectSection native_sections[] = {
        {
            .name = S8(".text"),
            .data = BUSTER_ARRAY_TO_SLICE(native_text),
            .kind = OBJECT_SECTION_TEXT,
            .alignment = 16,
        },
        {
            .name = S8(".rodata"),
            .data = BUSTER_ARRAY_TO_SLICE(native_read_only_data),
            .kind = OBJECT_SECTION_READ_ONLY_DATA,
            .alignment = 8,
        },
        {
            .name = S8(".data"),
            .data = BUSTER_ARRAY_TO_SLICE(native_data),
            .kind = OBJECT_SECTION_DATA,
            .alignment = 8,
        },
        {
            .name = S8(".bss"),
            .virtual_size = 16,
            .kind = OBJECT_SECTION_ZERO,
            .alignment = 8,
        },
#if BUSTER_MACOS
        {
            .name = S8(".debug_info"),
            .data = {.pointer = &native_debug_byte, .length = 1},
            .kind = OBJECT_SECTION_DEBUG_INFO,
            .alignment = 1,
        },
#endif
    };
    ObjectSymbol native_symbols[] = {
        {
            .name = S8("jit_test_constant"),
            .size = 8,
            .section = 0,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("jit_test_import_call"),
            .value = 16,
            .size = 16,
            .section = 0,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("jit_test_host_value"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("jit_test_internal_call"),
            .value = 32,
            .size = 16,
            .section = 0,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("jit_test_read_only_data"),
            .size = 8,
            .section = 1,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
        {
            .name = S8("jit_test_data"),
            .size = 8,
            .section = 2,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
        {
            .name = S8("jit_test_bss"),
            .size = 16,
            .section = 3,
            .kind = OBJECT_SYMBOL_DATA,
            .global = true,
        },
#if BUSTER_MACOS
        {
            .name = S8("out_of_bounds"),
            .value = 49,
            .size = 1,
            .section = 0,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
#endif
    };
    ObjectRelocation native_relocations[] = {
        {
#if BUSTER_CPU_ARCH_X86_64
            .addend = -4,
            .kind = OBJECT_RELOCATION_X86_64_PC32,
#else
            .kind = OBJECT_RELOCATION_AARCH64_CALL26,
#endif
            .offset = import_relocation_offset,
            .section = 0,
            .symbol = 2,
        },
        {
#if BUSTER_CPU_ARCH_X86_64
            .addend = -4,
            .kind = OBJECT_RELOCATION_X86_64_PC32,
#else
            .kind = OBJECT_RELOCATION_AARCH64_CALL26,
#endif
            .offset = internal_relocation_offset,
            .section = 0,
            .symbol = 0,
        },
        {
            .offset = 8,
            .section = 2,
            .symbol = 6,
            .kind = OBJECT_RELOCATION_ABSOLUTE64,
        },
        {
            .offset = 16,
            .section = 2,
            .symbol = 4,
            .kind = OBJECT_RELOCATION_ABSOLUTE64,
        },
#if BUSTER_MACOS
        {
            .offset = UINT64_MAX,
            .section = 4,
            .symbol = UINT32_MAX,
            .kind = OBJECT_RELOCATION_COUNT,
        },
#endif
    };
    ObjectFile native_object = jit_test_object(native_sections, BUSTER_ARRAY_LENGTH(native_sections));
    native_object.symbols = native_symbols;
    native_object.symbol_count = BUSTER_ARRAY_LENGTH(native_symbols);
    native_object.relocations = native_relocations;
    native_object.relocation_count = BUSTER_ARRAY_LENGTH(native_relocations);

    typedef u64 JitTestFunction(void);
    JitTestFunction* host_function = &jit_test_host_value;
    void* host_address = 0;
    BUSTER_CT_CHECK(sizeof(host_function) == sizeof(host_address));
    memcpy(&host_address, &host_function, sizeof(host_address));
    JitHostBinding native_binding = {
        .name = native_symbols[2].name,
        .address = host_address,
        .kind = OBJECT_SYMBOL_FUNCTION,
    };
#if BUSTER_CPU_ARCH_AARCH64
    u8 saved_misaligned_bytes[sizeof(u32)] = {0};
    u32 misaligned_call = UINT32_C(0x94000000);
    memcpy(saved_misaligned_bytes, native_text + import_relocation_offset + 1, sizeof(saved_misaligned_bytes));
    memcpy(native_text + import_relocation_offset + 1, &misaligned_call, sizeof(misaligned_call));
    native_relocations[0].offset = import_relocation_offset + 1;
    JitProgram misaligned_branch_program = jit_link_object(&native_object, (JitOptions){.bindings = &native_binding, .binding_count = 1});
    BUSTER_TEST(arguments, misaligned_branch_program.error == JIT_ERROR_CAPACITY);
    BUSTER_STRING_TEST(arguments, misaligned_branch_program.failing_symbol, native_symbols[2].name);
    jit_program_release(&misaligned_branch_program);
    native_relocations[0].offset = import_relocation_offset;
    memcpy(native_text + import_relocation_offset + 1, saved_misaligned_bytes, sizeof(saved_misaligned_bytes));

    native_relocations[0].addend = 4;
    JitProgram imported_addend_program = jit_link_object(&native_object, (JitOptions){.bindings = &native_binding, .binding_count = 1});
    BUSTER_TEST(arguments, imported_addend_program.error == JIT_ERROR_UNSUPPORTED_RELOCATION);
    BUSTER_STRING_TEST(arguments, imported_addend_program.failing_symbol, native_symbols[2].name);
    jit_program_release(&imported_addend_program);
    native_relocations[0].addend = 0;
#endif
    JitProgram native_program = jit_link_object(&native_object, (JitOptions){.bindings = &native_binding, .binding_count = 1});
    BUSTER_TEST(arguments, native_program.error == JIT_ERROR_NONE);
    BUSTER_TEST(arguments, native_program.allocation_base && native_program.allocation_size && native_program.executable_size);
#if BUSTER_MACOS && BUSTER_CPU_ARCH_AARCH64
    BUSTER_TEST(arguments, native_program.auxiliary_allocation_base && native_program.auxiliary_allocation_size);
    BUSTER_TEST(arguments,
                (u8*)native_program.allocation_base + native_program.allocation_size == native_program.auxiliary_allocation_base);
#else
    BUSTER_TEST(arguments, !native_program.auxiliary_allocation_base && !native_program.auxiliary_allocation_size);
#endif
    BUSTER_TEST(arguments, native_program.section_addresses[0] && native_program.section_addresses[1] && native_program.section_addresses[2] &&
                               native_program.section_addresses[3]);
#if BUSTER_MACOS
    BUSTER_TEST(arguments, !native_program.section_addresses[4]);
#endif
    if (native_program.error == JIT_ERROR_NONE)
    {
        void* constant_address = jit_program_symbol(&native_program, native_symbols[0].name);
        JitTestFunction* constant = 0;
        BUSTER_CT_CHECK(sizeof(constant) == sizeof(constant_address));
        memcpy(&constant, &constant_address, sizeof(constant));
        BUSTER_TEST(arguments, constant && constant() == 42);

        void* import_address = jit_program_symbol(&native_program, native_symbols[1].name);
        JitTestFunction* call_import = 0;
        BUSTER_CT_CHECK(sizeof(call_import) == sizeof(import_address));
        memcpy(&call_import, &import_address, sizeof(call_import));
        BUSTER_TEST(arguments, call_import && call_import() == 73);

        void* internal_address = jit_program_symbol(&native_program, native_symbols[3].name);
        JitTestFunction* call_internal = 0;
        BUSTER_CT_CHECK(sizeof(call_internal) == sizeof(internal_address));
        memcpy(&call_internal, &internal_address, sizeof(call_internal));
        BUSTER_TEST(arguments, call_internal && call_internal() == 42);

        void* read_only_address = jit_program_symbol(&native_program, native_symbols[4].name);
        void* data_address = jit_program_symbol(&native_program, native_symbols[5].name);
        void* bss_address = jit_program_symbol(&native_program, native_symbols[6].name);
        BUSTER_TEST(arguments, read_only_address && data_address && bss_address);
        if (!read_only_address || !data_address || !bss_address)
        {
            jit_program_release(&native_program);
            return result;
        }
        u64 loaded_read_only_value = 0;
        u64 data_value = 0;
        u64 bss_value = 0;
        u64 relocated_bss = 0;
        u64 relocated_read_only = 0;
        memcpy(&loaded_read_only_value, read_only_address, sizeof(loaded_read_only_value));
        memcpy(&data_value, data_address, sizeof(data_value));
        memcpy(&bss_value, bss_address, sizeof(bss_value));
        memcpy(&relocated_bss, (u8*)data_address + 8, sizeof(relocated_bss));
        memcpy(&relocated_read_only, (u8*)data_address + 16, sizeof(relocated_read_only));
        BUSTER_TEST(arguments, loaded_read_only_value == 17);
        BUSTER_TEST(arguments, data_value == 11);
        BUSTER_TEST(arguments, bss_value == 0);
        BUSTER_TEST(arguments, relocated_bss == (u64)(uintptr_t)bss_address);
        BUSTER_TEST(arguments, relocated_read_only == (u64)(uintptr_t)read_only_address);
        data_value = 29;
        bss_value = 31;
        memcpy(data_address, &data_value, sizeof(data_value));
        memcpy(bss_address, &bss_value, sizeof(bss_value));
        data_value = 0;
        bss_value = 0;
        memcpy(&data_value, data_address, sizeof(data_value));
        memcpy(&bss_value, bss_address, sizeof(bss_value));
        BUSTER_TEST(arguments, data_value == 29);
        BUSTER_TEST(arguments, bss_value == 31);
#if BUSTER_MACOS
        BUSTER_TEST(arguments, !jit_program_symbol(&native_program, S8("missing_symbol")));
        BUSTER_TEST(arguments, native_program.error == JIT_ERROR_SYMBOL_NOT_FOUND);
        BUSTER_STRING_TEST(arguments, native_program.failing_symbol, S8("missing_symbol"));
        BUSTER_TEST(arguments, !jit_program_symbol(&native_program, native_symbols[7].name));
        BUSTER_TEST(arguments, native_program.error == JIT_ERROR_SYMBOL_BOUNDS);
        BUSTER_STRING_TEST(arguments, native_program.failing_symbol, native_symbols[7].name);
#endif
    }
    jit_program_release(&native_program);
    BUSTER_TEST(arguments, !native_program.allocation_base && !native_program.auxiliary_allocation_base && !native_program.object);
#endif

    return result;
}
#endif
