#include <buster/tests/compiler/jit/jit_test.h>
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
#endif
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
