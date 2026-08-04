#include <buster/tests/compiler/driver/driver_test.h>

BUSTER_GLOBAL_LOCAL bool compiler_driver_test_elf_section_find(ByteSlice image, String8 name, u64* offset, u64* size, u64* address)
{
    enum
    {
        ELF_HEADER_SIZE = 64,
        ELF_SECTION_HEADER_SIZE = 64,
    };
    if (image.length < ELF_HEADER_SIZE || memcmp(image.pointer, "\x7f" "ELF", 4) != 0)
    {
        return false;
    }
    u64 section_table;
    u16 section_count;
    u16 string_index;
    memcpy(&section_table, image.pointer + 40, sizeof(section_table));
    memcpy(&section_count, image.pointer + 60, sizeof(section_count));
    memcpy(&string_index, image.pointer + 62, sizeof(string_index));
    if (!section_count || string_index >= section_count || section_table > image.length ||
        (u64)section_count * ELF_SECTION_HEADER_SIZE > image.length - section_table)
    {
        return false;
    }
    u64 string_header = section_table + (u64)string_index * ELF_SECTION_HEADER_SIZE;
    u64 string_offset;
    u64 string_size;
    memcpy(&string_offset, image.pointer + string_header + 24, sizeof(string_offset));
    memcpy(&string_size, image.pointer + string_header + 32, sizeof(string_size));
    if (string_offset > image.length || string_size > image.length - string_offset)
    {
        return false;
    }
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 header = section_table + (u64)section_index * ELF_SECTION_HEADER_SIZE;
        u32 name_offset;
        memcpy(&name_offset, image.pointer + header, sizeof(name_offset));
        if (name_offset >= string_size || string_size - name_offset <= name.length)
        {
            continue;
        }
        if (memcmp(image.pointer + string_offset + name_offset, name.pointer, name.length) != 0 ||
            image.pointer[string_offset + name_offset + name.length] != 0)
        {
            continue;
        }
        memcpy(offset, image.pointer + header + 24, sizeof(*offset));
        memcpy(size, image.pointer + header + 32, sizeof(*size));
        memcpy(address, image.pointer + header + 16, sizeof(*address));
        return *offset <= image.length && *size <= image.length - *offset;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL ByteSlice compiler_driver_test_elf_section(ByteSlice image, String8 name)
{
    u64 offset = 0;
    u64 size = 0;
    u64 address = 0;
    if (!compiler_driver_test_elf_section_find(image, name, &offset, &size, &address))
    {
        return (ByteSlice){0};
    }
    return (ByteSlice){
        .pointer = image.pointer + offset,
        .length = size,
    };
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u64 compiler_driver_test_elf_section_address(ByteSlice image, String8 name)
{
    u64 offset = 0;
    u64 size = 0;
    u64 address = 0;
    if (!compiler_driver_test_elf_section_find(image, name, &offset, &size, &address))
    {
        return 0;
    }
    return address;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool compiler_driver_bytes_contain(ByteSlice bytes, String8 needle)
{
    if (!needle.length || needle.length > bytes.length)
    {
        return false;
    }
    for (u64 offset = 0; offset + needle.length <= bytes.length; offset += 1)
    {
        if (memcmp(bytes.pointer + offset, needle.pointer, needle.length) == 0)
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL ByteSlice compiler_driver_test_archive(Arena* arena, ByteSlice* members, String8* names, u32 member_count)
{
    u64 size = 8;
    for (u32 member_index = 0; member_index < member_count; member_index += 1)
    {
        size += 60 + members[member_index].length;
        size += size & 1;
    }
    ByteSlice result = {
        .pointer = arena_allocate(arena, u8, size),
        .length = size,
    };
    memcpy(result.pointer, "!<arch>\n", 8);
    u64 cursor = 8;
    for (u32 member_index = 0; member_index < member_count; member_index += 1)
    {
        memset(result.pointer + cursor, ' ', 60);
        u64 name_length = BUSTER_MIN(names[member_index].length, (u64)15);
        memcpy(result.pointer + cursor, names[member_index].pointer, name_length);
        result.pointer[cursor + name_length] = '/';
        String8 member_size = string_format(arena, S8("{u64}"), members[member_index].length);
        memcpy(result.pointer + cursor + 48, member_size.pointer, member_size.length);
        result.pointer[cursor + 58] = '`';
        result.pointer[cursor + 59] = '\n';
        cursor += 60;
        memcpy(result.pointer + cursor, members[member_index].pointer, members[member_index].length);
        cursor += members[member_index].length;
        if (cursor & 1)
        {
            result.pointer[cursor++] = '\n';
        }
    }
    return result;
}

BUSTER_TEST_F_DECL UnitTestResult compiler_driver_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 command_line[] = {
        S8("-c"),
        S8("-std=gnu23"),
        S8("-target"),
        S8("aarch64-linux-android"),
        S8("-mcpu=apple-m4"),
        S8("--sysroot=/sdk"),
        S8("-Iinclude"),
        S8("-isystem"),
        S8("system"),
        S8("-DDEBUG=1"),
        S8("-U"),
        S8("NDEBUG"),
        S8("-O2"),
        S8("-g"),
        S8("-Wall"),
        S8("-fPIC"),
        S8("-pthread"),
        S8("-L/sdk/lib"),
        S8("-l:libandroid.so"),
        S8("-Wl,--gc-sections"),
        S8("-o"),
        S8("output.o"),
        S8("source.c"),
    };
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command_line));
    BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, invocation.action == COMPILER_DRIVER_ACTION_OBJECT);
    BUSTER_TEST(arguments, invocation.c_dialect == COMPILER_DRIVER_C_DIALECT_GNU23);
    BUSTER_TEST(arguments, invocation.target.cpu_arch == CPU_ARCH_AARCH64);
    BUSTER_TEST(arguments, invocation.target.os == OPERATING_SYSTEM_ANDROID);
    BUSTER_TEST(arguments, invocation.target.cpu_model == CPU_MODEL_A64_APPLE_M4);
    BUSTER_TEST(arguments, invocation.debug_info);
    BUSTER_TEST(arguments, invocation.include_path_count == 1);
    BUSTER_TEST(arguments, invocation.system_include_path_count >= 4);
    BUSTER_STRING_TEST(arguments, invocation.system_include_paths[0], S8("system"));
    bool found_sysroot_multiarch = false;
    bool found_sysroot_include = false;
    for (u32 path_index = 0; path_index < invocation.system_include_path_count; path_index += 1)
    {
        found_sysroot_multiarch |= string_equal(invocation.system_include_paths[path_index], S8("/sdk/usr/include/"
                                                                                                "aarch64-linux-android"));
        found_sysroot_include |= string_equal(invocation.system_include_paths[path_index], S8("/sdk/usr/include"));
    }
    BUSTER_TEST(arguments, found_sysroot_multiarch);
    BUSTER_TEST(arguments, found_sysroot_include);
    BUSTER_TEST(arguments, invocation.definition_count == 1);
    BUSTER_TEST(arguments, invocation.undefinition_count == 1);
    BUSTER_TEST(arguments, invocation.library_path_count == 1);
    BUSTER_TEST(arguments, invocation.library_count == 1);
    BUSTER_STRING_TEST(arguments, invocation.library_paths[0], S8("/sdk/lib"));
    BUSTER_STRING_TEST(arguments, invocation.libraries[0], S8(":libandroid.so"));
    BUSTER_TEST(arguments, invocation.linker_argument_count == 1);
    BUSTER_TEST(arguments, invocation.input_count == 1);
    BUSTER_STRING_TEST(arguments, invocation.output_path, S8("output.o"));
    BUSTER_STRING_TEST(arguments, invocation.sysroot, S8("/sdk"));
    String8 x86_cpu_command_line[] = {
        S8("-c"),
        S8("--target=x86_64-linux"),
        S8("-march=znver5"),
        S8("source.c"),
    };
    CompilerDriverInvocation x86_cpu_invocation = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(x86_cpu_command_line));
    BUSTER_TEST(arguments, x86_cpu_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, x86_cpu_invocation.target.cpu_model == CPU_MODEL_AMD_ZEN_5);
    BUSTER_TEST(arguments, target_vector_register_size(x86_cpu_invocation.target) == 64);
    String8 feature_command_line[] = {
        S8("-c"),
        S8("-mattr=+avx512f,+avx512vl,-avx2,+avx512bw"),
        S8("--target=x86_64-linux"),
        S8("-march=haswell"),
        S8("-masm"),
        S8("att"),
        S8("source.c"),
    };
    CompilerDriverInvocation feature_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(feature_command_line));
    BUSTER_TEST(arguments, feature_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, feature_invocation.assembly_syntax == ASSEMBLY_SYNTAX_ATT);
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_AVX));
    BUSTER_TEST(arguments, !target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_AVX2));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_AVX512F));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_AVX512VL));
    BUSTER_TEST(arguments, target_cpu_feature_has(feature_invocation.target, TARGET_CPU_FEATURE_X86_AVX512BW));
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, feature_invocation.target),
                       S8("avx,avx512bw,avx512f,avx512vl,sse2,sse3"));
    String8 ordered_feature_command_line[] = {
        S8("--target=x86_64-linux"), S8("-march=haswell"), S8("-mattr=+avx512f"), S8("-mattr=-avx512f"), S8("source.c"),
    };
    CompilerDriverInvocation ordered_features =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(ordered_feature_command_line));
    BUSTER_TEST(arguments, ordered_features.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, !target_cpu_feature_has(ordered_features.target, TARGET_CPU_FEATURE_X86_AVX512F));
    String8 invalid_feature_command_line[] = {S8("--target=x86_64-linux"), S8("-mattr=+future-isa"), S8("source.c")};
    CompilerDriverInvocation invalid_feature =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_feature_command_line));
    BUSTER_TEST(arguments, invalid_feature.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, invalid_feature.diagnostic, S8("unsupported target feature: future-isa"));
    String8 invalid_feature_syntax_command_line[] = {S8("--target=x86_64-linux"), S8("-mattr=avx"), S8("source.c")};
    CompilerDriverInvocation invalid_feature_syntax =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_feature_syntax_command_line));
    BUSTER_TEST(arguments, invalid_feature_syntax.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, invalid_feature_syntax.diagnostic, S8("invalid target feature override: avx"));
    String8 invalid_feature_combination_command_line[] = {S8("--target=x86_64-linux"), S8("-mattr=+avx2"), S8("source.c")};
    CompilerDriverInvocation invalid_feature_combination =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_feature_combination_command_line));
    BUSTER_TEST(arguments, invalid_feature_combination.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, invalid_feature_combination.diagnostic, S8("invalid target feature combination: avx2,sse2"));
    String8 aarch64_feature_command_line[] = {S8("--target=aarch64-linux"), S8("-mattr"), S8("-neon"), S8("source.c")};
    CompilerDriverInvocation aarch64_features =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(aarch64_feature_command_line));
    BUSTER_TEST(arguments, aarch64_features.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, aarch64_features.target), S8("none"));
    String8 incompatible_assembly_syntax_command_line[] = {S8("--target=aarch64-linux"), S8("-masm=intel"), S8("source.c")};
    CompilerDriverInvocation incompatible_assembly_syntax =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(incompatible_assembly_syntax_command_line));
    BUSTER_TEST(arguments, incompatible_assembly_syntax.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, incompatible_assembly_syntax.diagnostic, S8("assembly syntax is incompatible with target: intel"));
    String8 no_debug_command_line[] = {S8("-g0"), S8("-c"), S8("source.c")};
    CompilerDriverInvocation no_debug_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(no_debug_command_line));
    BUSTER_TEST(arguments, no_debug_invocation.error == COMPILER_DRIVER_ERROR_NONE && !no_debug_invocation.debug_info);
    String8 unsupported_debug_command_line[] = {S8("-g1"), S8("source.c")};
    CompilerDriverInvocation unsupported_debug =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(unsupported_debug_command_line));
    BUSTER_TEST(arguments, unsupported_debug.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, unsupported_debug.diagnostic, S8("unsupported debug option: -g1"));
    String8 incompatible_cpu_command_line[] = {
        S8("--target=x86_64-linux"),
        S8("-mcpu=apple-m4"),
        S8("-c"),
        S8("source.c"),
    };
    CompilerDriverInvocation incompatible_cpu =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(incompatible_cpu_command_line));
    BUSTER_TEST(arguments, incompatible_cpu.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, incompatible_cpu.diagnostic, S8("CPU model is incompatible with target: apple-m4"));
    String8 unknown_cpu_command_line[] = {
        S8("-march=future-fast"),
        S8("-c"),
        S8("source.c"),
    };
    CompilerDriverInvocation unknown_cpu = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(unknown_cpu_command_line));
    BUSTER_TEST(arguments, unknown_cpu.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, unknown_cpu.diagnostic, S8("unsupported CPU model: future-fast"));
    String8 isolated_command_line[] = {
        S8("-isysroot"), S8("/isolated-sdk"), S8("-nostdinc"), S8("-isystem"), S8("explicit-system"), S8("source.c"),
    };
    CompilerDriverInvocation isolated_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(isolated_command_line));
    BUSTER_TEST(arguments, isolated_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, isolated_invocation.no_standard_includes);
    BUSTER_STRING_TEST(arguments, isolated_invocation.sysroot, S8("/isolated-sdk"));
    BUSTER_TEST(arguments, isolated_invocation.system_include_path_count == 1);
    BUSTER_STRING_TEST(arguments, isolated_invocation.system_include_paths[0], S8("explicit-system"));
    String8 invalid_command_line[] = {
        S8("-target"),
        S8("riscv64-unknown-linux-gnu"),
        S8("source.c"),
    };
    CompilerDriverInvocation invalid_invocation = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_command_line));
    BUSTER_TEST(arguments, invalid_invocation.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    String8 preprocess_command_line[] = {
        S8("-E"),
        S8("-DADDED=5"),
        S8("tests/basic_c_driver.c"),
    };
    CompilerDriverInvocation preprocess_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(preprocess_command_line));
    CompilerDriverResult preprocess = compiler_driver_execute_invocation(arguments->arena, preprocess_invocation);
    BUSTER_TEST(arguments, preprocess.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(preprocess.output, S8("int answer = 37 ;")) != BUSTER_STRING_NO_MATCH);
    String8 warning_command_line[] = {
        S8("-fsyntax-only"),
        S8("tests/basic_c_preprocessor_warning.c"),
    };
    CompilerDriverResult warning = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(warning_command_line)));
    BUSTER_TEST(arguments, warning.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, warning.tokenizer_error_count == 0);
    BUSTER_TEST(arguments, warning.tokenizer_warning_count == 1);
    BUSTER_TEST(arguments, string_first_sequence(warning.warning, S8("warning: PREPROCESSOR_WARNING_TEXT")) != BUSTER_STRING_NO_MATCH);
    String8 warning_cross_target_command_line[] = {
        S8("-E"),
        S8("-target"),
        S8("x86_64-pc-windows-msvc"),
        S8("tests/basic_c_preprocessor_warning.c"),
    };
    CompilerDriverResult warning_cross_target = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(warning_cross_target_command_line)));
    BUSTER_TEST(arguments, warning_cross_target.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, warning_cross_target.tokenizer_error_count == 0);
    BUSTER_TEST(arguments, warning_cross_target.tokenizer_warning_count == 1);
    BUSTER_TEST(arguments, string_first_sequence(warning_cross_target.output, S8("int main ( void )")) != BUSTER_STRING_NO_MATCH);
    String8 error_command_line[] = {
        S8("-fsyntax-only"),
        S8("tests/basic_c_preprocessor_error.c"),
    };
    CompilerDriverResult preprocessor_error = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(error_command_line)));
    BUSTER_TEST(arguments, preprocessor_error.error == COMPILER_DRIVER_ERROR_TOKENIZE);
    BUSTER_TEST(arguments, preprocessor_error.tokenizer_error_count == 1);
    BUSTER_TEST(arguments, string_first_sequence(preprocessor_error.diagnostic, S8("expanded driver error")) != BUSTER_STRING_NO_MATCH);
    String8 warning_multi_command_line[] = {
        S8("-fsyntax-only"),
        S8("tests/basic_c_preprocessor_warning.c"),
        S8("tests/basic_c_preprocessor_warning_second.c"),
    };
    CompilerDriverResult warning_multi = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(warning_multi_command_line)));
    BUSTER_TEST(arguments, warning_multi.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, warning_multi.tokenizer_error_count == 0);
    BUSTER_TEST(arguments, warning_multi.tokenizer_warning_count == 2);
    u64 first_warning = string_first_sequence(warning_multi.warning, S8("warning: PREPROCESSOR_WARNING_TEXT"));
    u64 second_warning = string_first_sequence(warning_multi.warning, S8("warning: second driver warning"));
    BUSTER_TEST(arguments, first_warning != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, second_warning != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, first_warning < second_warning);
    String8 syntax_command_line[] = {
        S8("-fsyntax-only"),
        S8("tests/basic_c_driver.c"),
    };
    CompilerDriverResult syntax = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(syntax_command_line)));
    BUSTER_TEST(arguments, syntax.error == COMPILER_DRIVER_ERROR_NONE);
    String8 assembly_command_line[] = {
        S8("-S"),
        S8("-target"),
        S8("x86_64-unknown-linux-gnu"),
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverResult assembly = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_command_line)));
    BUSTER_TEST(arguments, assembly.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, assembly.output.length != 0);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\t.intel_syntax noprefix\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\t.text\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("main:\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\tpush rbp\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\tmov rbp, rsp\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\t.byte ")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_c23_command_line[] = {
        S8("-S"), S8("-std=c23"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("tests/basic_c_constexpr.c"),
    };
    CompilerDriverResult assembly_c23 = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_c23_command_line)));
    BUSTER_TEST(arguments, assembly_c23.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(assembly_c23.output, S8("[rip + \"offset\"]")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_tls_command_line[] = {
        S8("-S"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("tests/basic_c_thread_local.c"),
    };
    CompilerDriverResult assembly_tls = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_tls_command_line)));
    BUSTER_TEST(arguments, assembly_tls.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(assembly_tls.output, S8("@TPOFF")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_aarch64_command_line[] = {
        S8("-S"),
        S8("-target"),
        S8("aarch64-unknown-linux-gnu"),
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverResult assembly_aarch64 = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_aarch64_command_line)));
    BUSTER_TEST(arguments, assembly_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(assembly_aarch64.output, S8("\tstp x29, x30")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly_aarch64.output, S8("\tret\n")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_multi_command_line[] = {
        S8("-S"),
        S8("tests/basic_c_multi_main.c"),
        S8("tests/basic_c_multi_add.c"),
    };
    CompilerDriverResult assembly_multi = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_multi_command_line)));
    BUSTER_TEST(arguments, assembly_multi.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(assembly_multi.output, S8("main:\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly_multi.output, S8("add_values:\n")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_output_path = buster_test_temporary_path(arguments->arena, S8("buster-c-assembly"), S8(".s"));
    String8 assembly_file_command_line[] = {
        S8("-S"),
        S8("-o"),
        assembly_output_path,
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverResult assembly_file = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_file_command_line)));
    BUSTER_TEST(arguments, assembly_file.error == COMPILER_DRIVER_ERROR_NONE);
    ByteSlice assembly_file_bytes = file_read(arguments->arena, assembly_output_path, (FileReadOptions){0});
    BUSTER_TEST(arguments, assembly_file_bytes.length == assembly_file.output.length);
    if (assembly_file_bytes.length == assembly_file.output.length)
    {
        BUSTER_TEST(arguments, memcmp(assembly_file_bytes.pointer, assembly_file.output.pointer, assembly_file.output.length) == 0);
    }
    String8 buster_syntax_command_line[] = {
        S8("-fsyntax-only"),
        S8("tests/basic_minimal.bbb"),
    };
    CompilerDriverResult buster_syntax = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_syntax_command_line)));
    BUSTER_TEST(arguments, buster_syntax.error == COMPILER_DRIVER_ERROR_NONE);
    String8 buster_assembly_command_line[] = {
        S8("-S"),
        S8("-x"),
        S8("buster"),
        S8("tests/basic_minimal.bbb"),
    };
    CompilerDriverResult buster_assembly = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_assembly_command_line)));
    BUSTER_TEST(arguments, buster_assembly.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(buster_assembly.output, S8("main:\n")) != BUSTER_STRING_NO_MATCH);
    String8 buster_object_path = buster_test_temporary_path(arguments->arena, S8("buster-driver-object"), S8(".o"));
    String8 buster_object_command_line[] = {
        S8("-c"),
        S8("-o"),
        buster_object_path,
        S8("tests/basic_minimal.bbb"),
    };
    CompilerDriverResult buster_object = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_object_command_line)));
    BUSTER_TEST(arguments, buster_object.error == COMPILER_DRIVER_ERROR_NONE);
    ByteSlice buster_object_bytes = file_read(arguments->arena, buster_object_path, (FileReadOptions){0});
    BUSTER_TEST(arguments, buster_object_bytes.length != 0);
    String8 buster_module_command_line[] = {
        S8("-fsyntax-only"),
        S8("-fmodule-root"),
        S8("tests/modules"),
        S8("tests/basic_import.bbb"),
    };
    CompilerDriverResult buster_modules = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_module_command_line)));
    BUSTER_TEST(arguments, buster_modules.error == COMPILER_DRIVER_ERROR_NONE);
    String8 buster_preprocess_command_line[] = {
        S8("-E"),
        S8("tests/basic_minimal.bbb"),
    };
    CompilerDriverResult buster_preprocess = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_preprocess_command_line)));
    BUSTER_TEST(arguments, buster_preprocess.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, buster_preprocess.diagnostic, S8("Buster input does not support preprocessing"));
    String8 conflicting_actions[] = {
        S8("-c"),
        S8("-S"),
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverInvocation conflicting =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(conflicting_actions));
    BUSTER_TEST(arguments, conflicting.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    String8 dialect_flags[] = {
        S8("-std=gnu11"), S8("-std=gnu17"), S8("-std=gnu23"), S8("-std=c11"), S8("-std=c17"), S8("-std=c23"),
    };
    String8 dialect_versions[] = {
        S8("-DEXPECTED_STDC_VERSION=201112L"), S8("-DEXPECTED_STDC_VERSION=201710L"), S8("-DEXPECTED_STDC_VERSION=202311L"),
        S8("-DEXPECTED_STDC_VERSION=201112L"), S8("-DEXPECTED_STDC_VERSION=201710L"), S8("-DEXPECTED_STDC_VERSION=202311L"),
    };
    for (u32 dialect_index = 0; dialect_index < BUSTER_ARRAY_LENGTH(dialect_flags); dialect_index += 1)
    {
        TemporalArena dialect_temporary = arena_begin_temporal(arguments->arena);
        Arena* dialect_arena = dialect_temporary.arena;
        String8 dialect_object_path =
            buster_test_temporary_path(dialect_arena, S8("buster-c-dialect"), string_format(dialect_arena, S8("-{u32}.o"), dialect_index));
        String8 dialect_command_line[] = {
            S8("-c"), dialect_flags[dialect_index], dialect_versions[dialect_index], dialect_index < 3 ? S8("-DEXPECTED_GNU=1") : S8("-DEXPECTED_GNU=0"),
            S8("-o"), dialect_object_path,          S8("tests/basic_c_dialect.c"),
        };
        CompilerDriverResult dialect_result = compiler_driver_execute_invocation(
            dialect_arena, compiler_driver_parse_arguments(dialect_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(dialect_command_line)));
        BUSTER_TEST(arguments, dialect_result.error == COMPILER_DRIVER_ERROR_NONE);
        scratch_end(dialect_temporary);
    }
#if !BUSTER_ANDROID && !BUSTER_IOS
    Arena* c_object_conflicts[] = {
        arguments->arena,
    };
    TemporalArena c_object_temporary = scratch_begin(c_object_conflicts, BUSTER_ARRAY_LENGTH(c_object_conflicts));
    Arena* c_object_arena = c_object_temporary.arena;
    String8 c_object_path = buster_test_temporary_path(c_object_arena, S8("buster-c-driver"), S8(".o"));
    String8 c_object_command_line[] = {
        S8("-c"),
        S8("-o"),
        c_object_path,
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverResult c_object = compiler_driver_execute_invocation(
        c_object_arena, compiler_driver_parse_arguments(c_object_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_object_command_line)));
    BUSTER_TEST(arguments, c_object.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_object.codegen_statistics.function_count > 0);
    BUSTER_TEST(arguments, c_object.codegen_statistics.instruction_count > 0);
    BUSTER_TEST(arguments, c_object.codegen_statistics.value_count > 0);
    BUSTER_TEST(arguments, c_object.codegen_statistics.stack_value_bytes > 0);
    BUSTER_TEST(arguments, c_object.codegen_statistics.stack_frame_bytes >= c_object.codegen_statistics.maximum_stack_frame_bytes);
    BUSTER_TEST(arguments, c_object.codegen_statistics.code_bytes > 0);
    FileMapRead c_object_map = file_map_read(c_object_arena, c_object_path, (FileReadOptions){0});
    ByteSlice c_object_bytes = c_object_map.bytes;
    BUSTER_TEST(arguments, c_object_bytes.length != 0);
    file_map_unmap(c_object_map);
    String8 c_object_targets[] = {
        S8("x86_64-unknown-linux-gnu"),  S8("x86_64-pc-windows-msvc"),  S8("x86_64-apple-macos"),  S8("x86_64-linux-android"),  S8("x86_64-apple-ios"),
        S8("aarch64-unknown-linux-gnu"), S8("aarch64-pc-windows-msvc"), S8("aarch64-apple-macos"), S8("aarch64-linux-android"), S8("aarch64-apple-ios"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_object_targets); target_index += 1)
    {
        TemporalArena cross_temp = arena_begin_temporal(c_object_arena);
        String8 cross_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-object"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 cross_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), cross_object_path, S8("tests/basic_c_compile.c"),
        };
        CompilerDriverResult cross = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(cross_command_line)));
        BUSTER_TEST(arguments, cross.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead cross_map = file_map_read(cross_temp.arena, cross_object_path, (FileReadOptions){0});
        ByteSlice cross_bytes = cross_map.bytes;
        BUSTER_TEST(arguments, cross_bytes.length != 0);
        // Debug sections and their relocations must survive a round trip
        // through every object format, or linking a previously compiled
        // object back in fails.
        BUSTER_TEST(arguments, cross.has_object);
        if (cross.has_object)
        {
            ObjectFile cross_round_trip = object_read(cross_temp.arena, cross_bytes, cross.object.target);
            BUSTER_TEST(arguments, cross_round_trip.error == OBJECT_ERROR_NONE);
            if (cross_round_trip.error == OBJECT_ERROR_NONE)
            {
                bool debug_found = false;
                for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
                {
                    if (!object_section_kind_is_debug((ObjectSectionKind)kind))
                    {
                        continue;
                    }
                    BUSTER_TEST(arguments, cross_round_trip.sections[kind].data.length == cross.object.sections[kind].data.length);
                    debug_found = debug_found || cross_round_trip.sections[kind].data.length != 0;
                }
                BUSTER_TEST(arguments, debug_found);
            }
        }
        file_map_unmap(cross_map);
        String8 artifact_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-artifacts"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 artifact_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), artifact_object_path, S8("tests/basic_c_frontend_artifacts.c"),
        };
        CompilerDriverResult artifacts = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(artifact_command_line)));
        BUSTER_TEST(arguments, artifacts.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, artifacts.has_object);
        if (artifacts.has_object)
        {
            bool function_relocation = false;
            bool data_relocation = false;
            for (u32 relocation_index = 0; relocation_index < artifacts.object.relocation_count; relocation_index += 1)
            {
                ObjectRelocation relocation = artifacts.object.relocations[relocation_index];
                if (relocation.symbol >= artifacts.object.symbol_count)
                {
                    continue;
                }
                ObjectSymbol symbol = artifacts.object.symbols[relocation.symbol];
                function_relocation |= symbol.kind == OBJECT_SYMBOL_FUNCTION;
                data_relocation |= symbol.kind == OBJECT_SYMBOL_DATA;
            }
            BUSTER_TEST(arguments, function_relocation);
            BUSTER_TEST(arguments, data_relocation);
            FileMapRead artifact_map = file_map_read(cross_temp.arena, artifact_object_path, (FileReadOptions){0});
            BUSTER_TEST(arguments, artifact_map.bytes.length != 0);
            file_map_unmap(artifact_map);
        }
        String8 fixed_enum_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-fixed-enum"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 fixed_enum_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), fixed_enum_object_path, S8("tests/basic_c_fixed_enum.c"),
        };
        CompilerDriverResult fixed_enum = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fixed_enum_command_line)));
        BUSTER_TEST(arguments, fixed_enum.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead fixed_enum_map = file_map_read(cross_temp.arena, fixed_enum_object_path, (FileReadOptions){0});
        ByteSlice fixed_enum_bytes = fixed_enum_map.bytes;
        BUSTER_TEST(arguments, fixed_enum_bytes.length != 0);
        file_map_unmap(fixed_enum_map);
        String8 string_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-string"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 string_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), string_object_path, S8("tests/basic_c_string_concat.c"),
        };
        CompilerDriverResult string_literals = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(string_command_line)));
        BUSTER_TEST(arguments, string_literals.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead string_map = file_map_read(cross_temp.arena, string_object_path, (FileReadOptions){0});
        ByteSlice string_bytes = string_map.bytes;
        BUSTER_TEST(arguments, string_bytes.length != 0);
        file_map_unmap(string_map);
        String8 nullptr_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-nullptr"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 nullptr_command_line[] = {
            S8("-c"), S8("-std=c23"), S8("-target"), c_object_targets[target_index], S8("-o"), nullptr_object_path, S8("tests/basic_c_nullptr.c"),
        };
        CompilerDriverResult nullptr_result = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(nullptr_command_line)));
        BUSTER_TEST(arguments, nullptr_result.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead nullptr_map = file_map_read(cross_temp.arena, nullptr_object_path, (FileReadOptions){0});
        ByteSlice nullptr_bytes = nullptr_map.bytes;
        BUSTER_TEST(arguments, nullptr_bytes.length != 0);
        file_map_unmap(nullptr_map);
        String8 constexpr_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-constexpr"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 constexpr_command_line[] = {
            S8("-c"), S8("-std=c23"), S8("-target"), c_object_targets[target_index], S8("-o"), constexpr_object_path, S8("tests/basic_c_constexpr.c"),
        };
        CompilerDriverResult constexpr_result = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(constexpr_command_line)));
        BUSTER_TEST(arguments, constexpr_result.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead constexpr_map = file_map_read(cross_temp.arena, constexpr_object_path, (FileReadOptions){0});
        ByteSlice constexpr_bytes = constexpr_map.bytes;
        BUSTER_TEST(arguments, constexpr_bytes.length != 0);
        file_map_unmap(constexpr_map);
        String8 atomic_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-atomic"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 atomic_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), atomic_object_path, S8("tests/basic_c_atomic.c"),
        };
        CompilerDriverResult atomic = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_command_line)));
        BUSTER_TEST(arguments, atomic.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead atomic_map = file_map_read(cross_temp.arena, atomic_object_path, (FileReadOptions){0});
        ByteSlice atomic_bytes = atomic_map.bytes;
        BUSTER_TEST(arguments, atomic_bytes.length != 0);
        file_map_unmap(atomic_map);
        String8 stdatomic_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-stdatomic"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 stdatomic_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), stdatomic_object_path, S8("tests/basic_c_stdatomic.c"),
        };
        CompilerDriverResult stdatomic = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(stdatomic_command_line)));
        BUSTER_TEST(arguments, stdatomic.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead stdatomic_map = file_map_read(cross_temp.arena, stdatomic_object_path, (FileReadOptions){0});
        ByteSlice stdatomic_bytes = stdatomic_map.bytes;
        BUSTER_TEST(arguments, stdatomic_bytes.length != 0);
        file_map_unmap(stdatomic_map);
        scratch_end(cross_temp);
    }
    {
        TemporalArena large_frame_temporary = arena_begin_temporal(c_object_arena);
        String8 large_frame_object_path = buster_test_temporary_path(large_frame_temporary.arena, S8("buster-c-large-frame"), S8(".o"));
        String8 large_frame_command_line[] = {
            S8("-c"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), large_frame_object_path, S8("tests/basic_c_large_frame.c"),
        };
        CompilerDriverResult large_frame = compiler_driver_execute_invocation(
            large_frame_temporary.arena,
            compiler_driver_parse_arguments(large_frame_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(large_frame_command_line)));
        BUSTER_TEST(arguments, large_frame.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead large_frame_map = file_map_read(large_frame_temporary.arena, large_frame_object_path, (FileReadOptions){0});
        ByteSlice large_frame_bytes = large_frame_map.bytes;
        BUSTER_TEST(arguments, large_frame_bytes.length != 0);
        file_map_unmap(large_frame_map);
        scratch_end(large_frame_temporary);
    }
    {
        TemporalArena ucontext_temporary = arena_begin_temporal(c_object_arena);
        String8 ucontext_object_path = buster_test_temporary_path(ucontext_temporary.arena, S8("buster-c-ucontext"), S8(".o"));
        String8 ucontext_command_line[] = {
            S8("-c"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), ucontext_object_path, S8("tests/basic_c_ucontext.c"),
        };
        CompilerDriverResult ucontext = compiler_driver_execute_invocation(
            ucontext_temporary.arena, compiler_driver_parse_arguments(ucontext_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(ucontext_command_line)));
        BUSTER_TEST(arguments, ucontext.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead ucontext_map = file_map_read(ucontext_temporary.arena, ucontext_object_path, (FileReadOptions){0});
        ByteSlice ucontext_bytes = ucontext_map.bytes;
        BUSTER_TEST(arguments, ucontext_bytes.length != 0);
        file_map_unmap(ucontext_map);
        scratch_end(ucontext_temporary);
    }
    scratch_end(c_object_temporary);
#endif
#if BUSTER_LINK_LIBC && !BUSTER_ANDROID && !BUSTER_IOS && !BUSTER_SANITIZE
    String8 c_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-driver"),
#if BUSTER_WINDOWS
                                                           S8(".exe"));
#else
                                                           S8(""));
#endif
    String8 c_link_command_line[] = {
        S8("-o"),
        c_executable_path,
        S8("tests/basic_c_operations.c"),
    };
    CompilerDriverResult c_link = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_link_command_line)));
    BUSTER_TEST(arguments, c_link.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_link.has_object);
    if (c_link.has_object)
    {
        BUSTER_TEST(arguments, c_link.object.sections[OBJECT_SECTION_ZERO].data.length == 0);
        BUSTER_TEST(arguments, c_link.object.sections[OBJECT_SECTION_ZERO].virtual_size >= BUSTER_MB(1));
        BUSTER_TEST(arguments, c_link.object.sections[OBJECT_SECTION_READ_ONLY_DATA].data.length >= 64);
        ByteSlice c_image = file_read(arguments->arena, c_executable_path, (FileReadOptions){0});
        BUSTER_TEST(arguments, c_image.length != 0 && c_image.length < BUSTER_MB(1));
    }
    if (c_link.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_run_arguments[] = {
            c_executable_path,
        };
        ProcessSpawnResult c_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_run_arguments), (SliceString8){0}, (SliceString8){0},
                                                      (ProcessSpawnOptions){
                                                          .use_process_environment = true,
                                                      });
        BUSTER_TEST(arguments, c_spawn.handle != 0);
        if (c_spawn.handle)
        {
            ProcessWaitResult c_wait = os_process_wait_sync(arguments->arena, c_spawn);
            BUSTER_TEST(arguments, c_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_auto_type_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-auto-type"),
#if BUSTER_WINDOWS
                                                                      S8(".exe"));
#else
                                                                      S8(""));
#endif
    String8 c_auto_type_command_line[] = {
        S8("-std=gnu23"),
        S8("-o"),
        c_auto_type_executable_path,
        S8("tests/basic_c_auto_type.c"),
    };
    CompilerDriverResult c_auto_type = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_auto_type_command_line)));
    BUSTER_TEST(arguments, c_auto_type.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_auto_type.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_auto_type_run_arguments[] = {
            c_auto_type_executable_path,
        };
        ProcessSpawnResult c_auto_type_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_auto_type_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_auto_type_spawn.handle != 0);
        if (c_auto_type_spawn.handle)
        {
            ProcessWaitResult c_auto_type_wait = os_process_wait_sync(arguments->arena, c_auto_type_spawn);
            BUSTER_TEST(arguments, c_auto_type_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_artifact_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-frontend-artifacts"),
#if BUSTER_WINDOWS
                                                                     S8(".exe"));
#else
                                                                     S8(""));
#endif
    String8 c_artifact_command_line[] = {
        S8("-o"),
        c_artifact_executable_path,
        S8("tests/basic_c_frontend_artifacts.c"),
    };
    CompilerDriverResult c_artifacts = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_artifact_command_line)));
    BUSTER_TEST(arguments, c_artifacts.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_artifacts.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_artifact_run_arguments[] = {
            c_artifact_executable_path,
        };
        ProcessSpawnResult c_artifact_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_artifact_run_arguments), (SliceString8){0}, (SliceString8){0},
                                                                (ProcessSpawnOptions){
                                                                    .use_process_environment = true,
                                                                });
        BUSTER_TEST(arguments, c_artifact_spawn.handle != 0);
        if (c_artifact_spawn.handle)
        {
            ProcessWaitResult c_artifact_wait = os_process_wait_sync(arguments->arena, c_artifact_spawn);
            BUSTER_TEST(arguments, c_artifact_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // Windows targets must select CodeView rather than DWARF: the COFF object
    // carries .debug$S/.debug$T with SECREL32/SECTION relocations, and no
    // DWARF sections at all.
    {
        TemporalArena codeview_temporary = scratch_begin(&arguments->arena, 1);
        String8 codeview_object_path = buster_test_temporary_path(codeview_temporary.arena, S8("buster-c-codeview"), S8(".o"));
        String8 codeview_command_line[] = {
            S8("-target"), S8("x86_64-windows"), S8("-c"), S8("-o"), codeview_object_path, S8("tests/basic_c_operations.c"),
        };
        CompilerDriverResult codeview_compile = compiler_driver_execute_invocation(
            codeview_temporary.arena, compiler_driver_parse_arguments(codeview_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(codeview_command_line)));
        BUSTER_TEST(arguments, codeview_compile.error == COMPILER_DRIVER_ERROR_NONE);
        if (codeview_compile.error == COMPILER_DRIVER_ERROR_NONE)
        {
            BUSTER_TEST(arguments, codeview_compile.has_object);
            ObjectFile* codeview_object = &codeview_compile.object;
            ByteSlice codeview_symbols = codeview_object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data;
            ByteSlice codeview_types = codeview_object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_TYPES].data;
            BUSTER_TEST(arguments, codeview_symbols.length > 16);
            BUSTER_TEST(arguments, codeview_types.length >= 4);
            BUSTER_TEST(arguments, codeview_object->sections[OBJECT_SECTION_DEBUG_INFO].data.length == 0);
            BUSTER_TEST(arguments, codeview_object->sections[OBJECT_SECTION_DEBUG_LINE].data.length == 0);
            BUSTER_TEST(arguments, codeview_object->sections[OBJECT_SECTION_ZERO].data.length == 0);
            BUSTER_TEST(arguments, codeview_object->sections[OBJECT_SECTION_ZERO].virtual_size >= BUSTER_MB(1));
            BUSTER_TEST(arguments, codeview_object->sections[OBJECT_SECTION_READ_ONLY_DATA].data.length >= 64);
            ByteSlice codeview_pdata = codeview_object->sections[OBJECT_SECTION_WINDOWS_PDATA].data;
            ByteSlice codeview_xdata = codeview_object->sections[OBJECT_SECTION_WINDOWS_XDATA].data;
            BUSTER_TEST(arguments, codeview_pdata.length >= 12 && codeview_pdata.length % 12 == 0);
            BUSTER_TEST(arguments, codeview_xdata.length >= 4);
            bool codeview_unwind_relocation = false;
            for (u32 relocation_index = 0; relocation_index < codeview_object->relocation_count; relocation_index += 1)
            {
                ObjectRelocation* relocation = codeview_object->relocations + relocation_index;
                codeview_unwind_relocation |= relocation->section == OBJECT_SECTION_WINDOWS_PDATA &&
                                              relocation->kind == OBJECT_RELOCATION_COFF_ADDR32NB;
            }
            BUSTER_TEST(arguments, codeview_unwind_relocation);
            ByteSlice codeview_file = file_read(codeview_temporary.arena, codeview_object_path, (FileReadOptions){0});
            BUSTER_TEST(arguments, codeview_file.length != 0 && codeview_file.length < BUSTER_MB(1));
            u32 codeview_signature = 0;
            memcpy(&codeview_signature, codeview_symbols.pointer, sizeof(codeview_signature));
            BUSTER_TEST(arguments, codeview_signature == 4);
            u32 secrel_count = 0;
            u32 section_count = 0;
            for (u32 relocation_index = 0; relocation_index < codeview_object->relocation_count; relocation_index += 1)
            {
                ObjectRelocation relocation = codeview_object->relocations[relocation_index];
                if (relocation.section != OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS)
                {
                    continue;
                }
                secrel_count += relocation.kind == OBJECT_RELOCATION_COFF_SECREL32;
                section_count += relocation.kind == OBJECT_RELOCATION_COFF_SECTION16;
                BUSTER_TEST(arguments, relocation.symbol < codeview_object->symbol_count);
                BUSTER_TEST(arguments, relocation.offset + 2 <= codeview_symbols.length);
            }
            // Functions and materialized globals each contribute matching
            // section-relative and section-index slots.
            BUSTER_TEST(arguments, secrel_count != 0 && secrel_count == section_count);
        }
        scratch_end(codeview_temporary);
    }
    // Windows ARM64 uses full unwind records so non-canonical frame-base
    // saves, page touches, and every emitted epilog remain explicit.
    {
        TemporalArena arm64_unwind_temporary = scratch_begin(&arguments->arena, 1);
        String8 arm64_unwind_object_path = buster_test_temporary_path(arm64_unwind_temporary.arena, S8("buster-c-arm64-unwind"), S8(".o"));
        String8 arm64_unwind_command_line[] = {
            S8("-target"), S8("aarch64-windows"), S8("-c"), S8("-g0"), S8("-o"), arm64_unwind_object_path, S8("tests/basic_c_operations.c"),
        };
        CompilerDriverResult arm64_unwind_compile = compiler_driver_execute_invocation(
            arm64_unwind_temporary.arena,
            compiler_driver_parse_arguments(arm64_unwind_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(arm64_unwind_command_line)));
        BUSTER_TEST(arguments, arm64_unwind_compile.error == COMPILER_DRIVER_ERROR_NONE);
        if (arm64_unwind_compile.error == COMPILER_DRIVER_ERROR_NONE)
        {
            ObjectFile* arm64_unwind_object = &arm64_unwind_compile.object;
            ByteSlice arm64_pdata = arm64_unwind_object->sections[OBJECT_SECTION_WINDOWS_PDATA].data;
            ByteSlice arm64_xdata = arm64_unwind_object->sections[OBJECT_SECTION_WINDOWS_XDATA].data;
            BUSTER_TEST(arguments, arm64_pdata.length >= 8 && arm64_pdata.length % 8 == 0);
            BUSTER_TEST(arguments, arm64_xdata.length >= 8);
            bool arm64_large_allocation = false;
            for (u64 byte_index = 0; byte_index < arm64_xdata.length; byte_index += 1)
            {
                arm64_large_allocation |= arm64_xdata.pointer[byte_index] == 0xe0;
            }
            BUSTER_TEST(arguments, arm64_large_allocation);
            u32 arm64_unwind_relocations = 0;
            for (u32 relocation_index = 0; relocation_index < arm64_unwind_object->relocation_count; relocation_index += 1)
            {
                ObjectRelocation* relocation = arm64_unwind_object->relocations + relocation_index;
                arm64_unwind_relocations += relocation->section == OBJECT_SECTION_WINDOWS_PDATA &&
                                            relocation->kind == OBJECT_RELOCATION_COFF_ADDR32NB;
            }
            BUSTER_TEST(arguments, arm64_unwind_relocations == arm64_pdata.length / 4);
            ByteSlice arm64_unwind_file = file_read(arm64_unwind_temporary.arena, arm64_unwind_object_path, (FileReadOptions){0});
            BUSTER_TEST(arguments, arm64_unwind_file.length != 0 && arm64_unwind_file.length < BUSTER_MB(1));
        }
        scratch_end(arm64_unwind_temporary);
    }
    // Recording line rows must not change the code that is generated for
    // them: the machine code has to be identical with and without -g.
    {
        TemporalArena debug_parity_temporary = scratch_begin(&arguments->arena, 1);
        String8 debug_parity_targets[] = {
            S8("x86_64-unknown-linux-gnu"),
            S8("aarch64-apple-macos"),
        };
        String8 debug_parity_sources[] = {
            S8("tests/basic_c_operations.c"),
            S8("tests/basic_c_archive_bias.c"),
            S8("tests/basic_c_multi_add.c"),
        };
        for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(debug_parity_targets); target_index += 1)
        {
            for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(debug_parity_sources); source_index += 1)
            {
                String8 source = debug_parity_sources[source_index];
                String8 debug_command_line[] = {
                    S8("-c"), S8("-g"), S8("-target"), debug_parity_targets[target_index], source,
                };
                String8 stripped_command_line[] = {
                    S8("-c"), S8("-g0"), S8("-target"), debug_parity_targets[target_index], source,
                };
                CompilerDriverResult with_debug = compiler_driver_execute_invocation(
                    debug_parity_temporary.arena,
                    compiler_driver_parse_arguments(debug_parity_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(debug_command_line)));
                CompilerDriverResult without_debug = compiler_driver_execute_invocation(
                    debug_parity_temporary.arena,
                    compiler_driver_parse_arguments(debug_parity_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(stripped_command_line)));
                BUSTER_TEST(arguments, with_debug.error == COMPILER_DRIVER_ERROR_NONE && with_debug.has_object);
                BUSTER_TEST(arguments, without_debug.error == COMPILER_DRIVER_ERROR_NONE && without_debug.has_object);
                if (with_debug.has_object && without_debug.has_object)
                {
                    ByteSlice debug_text = with_debug.object.sections[OBJECT_SECTION_TEXT].data;
                    ByteSlice stripped_text = without_debug.object.sections[OBJECT_SECTION_TEXT].data;
                    BUSTER_TEST(arguments, debug_text.length != 0);
                    BUSTER_TEST(arguments, debug_text.length == stripped_text.length);
                    if (debug_text.length == stripped_text.length)
                    {
                        BUSTER_TEST(arguments, memcmp(debug_text.pointer, stripped_text.pointer, debug_text.length) == 0);
                    }
                    BUSTER_TEST(arguments, with_debug.object.sections[OBJECT_SECTION_DEBUG_LINE].data.length != 0);
                    BUSTER_TEST(arguments, without_debug.object.sections[OBJECT_SECTION_DEBUG_LINE].data.length == 0);
                }
            }
        }
        scratch_end(debug_parity_temporary);
    }
#if BUSTER_LINUX
    // -g must produce loadable executables that carry DWARF line and info
    // sections resolvable back to source lines.
    String8 c_debug_path = buster_test_temporary_path(arguments->arena, S8("buster-c-debug"), S8(""));
    String8 c_debug_command_line[] = {
        S8("-g"),
        S8("-o"),
        c_debug_path,
        S8("tests/basic_c_operations.c"),
    };
    CompilerDriverResult c_debug_link = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_debug_command_line)));
    BUSTER_TEST(arguments, c_debug_link.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_debug_link.error == COMPILER_DRIVER_ERROR_NONE)
    {
        ByteSlice c_debug_image = file_read(arguments->arena, c_debug_path, (FileReadOptions){0});
        ByteSlice c_debug_line = compiler_driver_test_elf_section(c_debug_image, S8(".debug_line"));
        ByteSlice c_debug_info = compiler_driver_test_elf_section(c_debug_image, S8(".debug_info"));
        ByteSlice c_debug_text = compiler_driver_test_elf_section(c_debug_image, S8(".text"));
        u64 c_debug_text_address = compiler_driver_test_elf_section_address(c_debug_image, S8(".text"));
        BUSTER_TEST(arguments, c_debug_line.length > 4);
        BUSTER_TEST(arguments, c_debug_info.length > 11);
        BUSTER_TEST(arguments, c_debug_text.length != 0);
        BUSTER_TEST(arguments, c_debug_text_address != 0);
        DwarfLineRow c_debug_row = {0};
        BUSTER_TEST(arguments, dwarf_line_lookup(c_debug_line, c_debug_text_address, &c_debug_row));
        BUSTER_TEST(arguments, c_debug_row.line != 0 && c_debug_row.file != 0);
        String8 c_debug_run_arguments[] = {
            c_debug_path,
        };
        ProcessSpawnResult c_debug_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_debug_run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .use_process_environment = true,
                                                            });
        BUSTER_TEST(arguments, c_debug_spawn.handle != 0);
        if (c_debug_spawn.handle)
        {
            ProcessWaitResult c_debug_wait = os_process_wait_sync(arguments->arena, c_debug_spawn);
            BUSTER_TEST(arguments, c_debug_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
#endif
    String8 string_concat_path = buster_test_temporary_path(arguments->arena, S8("buster-c-string-concat"),
#if BUSTER_WINDOWS
                                                            S8(".exe"));
#else
                                                            S8(""));
#endif
    String8 string_concat_command_line[] = {
        S8("-o"),
        string_concat_path,
        S8("tests/basic_c_string_concat.c"),
    };
    CompilerDriverResult string_concat_link = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(string_concat_command_line)));
    BUSTER_TEST(arguments, string_concat_link.error == COMPILER_DRIVER_ERROR_NONE);
    if (string_concat_link.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 string_concat_run_arguments[] = {
            string_concat_path,
        };
        ProcessSpawnResult string_concat_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(string_concat_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, string_concat_spawn.handle != 0);
        if (string_concat_spawn.handle)
        {
            ProcessWaitResult string_concat_wait = os_process_wait_sync(arguments->arena, string_concat_spawn);
            BUSTER_TEST(arguments, string_concat_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 nullptr_path = buster_test_temporary_path(arguments->arena, S8("buster-c-nullptr"),
#if BUSTER_WINDOWS
                                                      S8(".exe"));
#else
                                                      S8(""));
#endif
    String8 nullptr_command_line[] = {
        S8("-std=c23"),
        S8("-o"),
        nullptr_path,
        S8("tests/basic_c_nullptr.c"),
    };
    CompilerDriverResult nullptr_link = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(nullptr_command_line)));
    BUSTER_TEST(arguments, nullptr_link.error == COMPILER_DRIVER_ERROR_NONE);
    if (nullptr_link.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 nullptr_run_arguments[] = {
            nullptr_path,
        };
        ProcessSpawnResult nullptr_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(nullptr_run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .use_process_environment = true,
                                                            });
        BUSTER_TEST(arguments, nullptr_spawn.handle != 0);
        if (nullptr_spawn.handle)
        {
            ProcessWaitResult nullptr_wait = os_process_wait_sync(arguments->arena, nullptr_spawn);
            BUSTER_TEST(arguments, nullptr_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    {
        TemporalArena constexpr_temporary = arena_begin_temporal(arguments->arena);
        String8 constexpr_path = buster_test_temporary_path(constexpr_temporary.arena, S8("buster-c-constexpr"),
#if BUSTER_WINDOWS
                                                            S8(".exe"));
#else
                                                            S8(""));
#endif
        String8 constexpr_command_line[] = {
            S8("-std=c23"),
            S8("-o"),
            constexpr_path,
            S8("tests/basic_c_constexpr.c"),
        };
        CompilerDriverResult constexpr_link = compiler_driver_execute_invocation(
            constexpr_temporary.arena, compiler_driver_parse_arguments(constexpr_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(constexpr_command_line)));
        BUSTER_TEST(arguments, constexpr_link.error == COMPILER_DRIVER_ERROR_NONE);
        if (constexpr_link.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 constexpr_run_arguments[] = {
                constexpr_path,
            };
            ProcessSpawnResult constexpr_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(constexpr_run_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){
                                     .use_process_environment = true,
                                 });
            BUSTER_TEST(arguments, constexpr_spawn.handle != 0);
            if (constexpr_spawn.handle)
            {
                ProcessWaitResult constexpr_wait = os_process_wait_sync(constexpr_temporary.arena, constexpr_spawn);
                BUSTER_TEST(arguments, constexpr_wait.result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(constexpr_temporary);
    }
#if BUSTER_LINUX
    String8 c_dynamic_library_path = buster_test_temporary_path(arguments->arena, S8("buster-c-dynamic-library"), S8(""));
    String8 c_dynamic_library_command_line[] = {
        S8("-o"),
        c_dynamic_library_path,
        S8("-l:libm.so.6"),
        S8("tests/basic_c_dynamic_library.c"),
    };
    CompilerDriverResult c_dynamic_library = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_dynamic_library_command_line)));
    BUSTER_TEST(arguments, c_dynamic_library.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_dynamic_library.error == COMPILER_DRIVER_ERROR_NONE)
    {
        BUSTER_TEST(arguments, compiler_driver_bytes_contain(c_dynamic_library.native_link.executable, S8("libm.so.6")));
        String8 run_arguments[] = {
            c_dynamic_library_path,
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
    String8 c_atomic_path = buster_test_temporary_path(arguments->arena, S8("buster-c-atomic"),
#if BUSTER_WINDOWS
                                                       S8(".exe"));
#else
                                                       S8(""));
#endif
    String8 c_atomic_command_line[] = {
        S8("-o"),
        c_atomic_path,
        S8("tests/basic_c_atomic.c"),
    };
    CompilerDriverResult c_atomic = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_atomic_command_line)));
    BUSTER_TEST(arguments, c_atomic.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_atomic.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_atomic_arguments[] = {
            c_atomic_path,
        };
        ProcessSpawnResult c_atomic_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_atomic_arguments), (SliceString8){0}, (SliceString8){0},
                                                             (ProcessSpawnOptions){
                                                                 .use_process_environment = true,
                                                             });
        BUSTER_TEST(arguments, c_atomic_spawn.handle != 0);
        if (c_atomic_spawn.handle)
        {
            ProcessWaitResult c_atomic_wait = os_process_wait_sync(arguments->arena, c_atomic_spawn);
            BUSTER_TEST(arguments, c_atomic_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    TemporalArena c_stdatomic_temporary = arena_begin_temporal(arguments->arena);
    String8 c_stdatomic_path = buster_test_temporary_path(c_stdatomic_temporary.arena, S8("buster-c-stdatomic"),
#if BUSTER_WINDOWS
                                                          S8(".exe"));
#else
                                                          S8(""));
#endif
    String8 c_stdatomic_command_line[] = {
        S8("-o"),
        c_stdatomic_path,
        S8("tests/basic_c_stdatomic.c"),
    };
    CompilerDriverResult c_stdatomic = compiler_driver_execute_invocation(
        c_stdatomic_temporary.arena,
        compiler_driver_parse_arguments(c_stdatomic_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_stdatomic_command_line)));
    BUSTER_TEST(arguments, c_stdatomic.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_stdatomic.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_stdatomic_arguments[] = {
            c_stdatomic_path,
        };
        ProcessSpawnResult c_stdatomic_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_stdatomic_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_stdatomic_spawn.handle != 0);
        if (c_stdatomic_spawn.handle)
        {
            ProcessWaitResult c_stdatomic_wait = os_process_wait_sync(c_stdatomic_temporary.arena, c_stdatomic_spawn);
            BUSTER_TEST(arguments, c_stdatomic_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    scratch_end(c_stdatomic_temporary);
    String8 c_generic_path = buster_test_temporary_path(arguments->arena, S8("buster-c-generic"),
#if BUSTER_WINDOWS
                                                        S8(".exe"));
#else
                                                        S8(""));
#endif
    String8 c_generic_command_line[] = {
        S8("-o"),
        c_generic_path,
        S8("tests/basic_c_generic.c"),
    };
    CompilerDriverResult c_generic = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_generic_command_line)));
    BUSTER_TEST(arguments, c_generic.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_generic.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_generic_arguments[] = {
            c_generic_path,
        };
        ProcessSpawnResult c_generic_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_generic_arguments), (SliceString8){0}, (SliceString8){0},
                                                              (ProcessSpawnOptions){
                                                                  .use_process_environment = true,
                                                              });
        BUSTER_TEST(arguments, c_generic_spawn.handle != 0);
        if (c_generic_spawn.handle)
        {
            ProcessWaitResult c_generic_wait = os_process_wait_sync(arguments->arena, c_generic_spawn);
            BUSTER_TEST(arguments, c_generic_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_alignas_path = buster_test_temporary_path(arguments->arena, S8("buster-c-alignas"),
#if BUSTER_WINDOWS
                                                        S8(".exe"));
#else
                                                        S8(""));
#endif
    String8 c_alignas_command_line[] = {
        S8("-o"),
        c_alignas_path,
        S8("tests/basic_c_alignas.c"),
    };
    CompilerDriverResult c_alignas = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_alignas_command_line)));
    BUSTER_TEST(arguments, c_alignas.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_alignas.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_alignas_arguments[] = {
            c_alignas_path,
        };
        ProcessSpawnResult c_alignas_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_alignas_arguments), (SliceString8){0}, (SliceString8){0},
                                                              (ProcessSpawnOptions){
                                                                  .use_process_environment = true,
                                                              });
        BUSTER_TEST(arguments, c_alignas_spawn.handle != 0);
        if (c_alignas_spawn.handle)
        {
            ProcessWaitResult c_alignas_wait = os_process_wait_sync(arguments->arena, c_alignas_spawn);
            BUSTER_TEST(arguments, c_alignas_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_vla_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vla"),
#if BUSTER_WINDOWS
                                                    S8(".exe"));
#else
                                                    S8(""));
#endif
    String8 c_vla_command_line[] = {
        S8("-o"),
        c_vla_path,
        S8("tests/basic_c_vla.c"),
    };
    CompilerDriverResult c_vla = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vla_command_line)));
    BUSTER_TEST(arguments, c_vla.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_vla.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_vla_arguments[] = {
            c_vla_path,
        };
        ProcessSpawnResult c_vla_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_vla_arguments), (SliceString8){0}, (SliceString8){0},
                                                          (ProcessSpawnOptions){
                                                              .use_process_environment = true,
                                                          });
        BUSTER_TEST(arguments, c_vla_spawn.handle != 0);
        if (c_vla_spawn.handle)
        {
            ProcessWaitResult c_vla_wait = os_process_wait_sync(arguments->arena, c_vla_spawn);
            BUSTER_TEST(arguments, c_vla_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_aggregate_path = buster_test_temporary_path(arguments->arena, S8("buster-c-aggregate"),
#if BUSTER_WINDOWS
                                                          S8(".exe"));
#else
                                                          S8(""));
#endif
    String8 c_aggregate_command_line[] = {
        S8("-o"),
        c_aggregate_path,
        S8("tests/basic_c_argv_aggregate.c"),
    };
    CompilerDriverResult c_aggregate = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_aggregate_command_line)));
    BUSTER_TEST(arguments, c_aggregate.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_aggregate.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_aggregate_arguments[] = {
            c_aggregate_path,
            S8("cc"),
            S8("a"),
            S8("b"),
        };
        ProcessSpawnResult c_aggregate_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_aggregate_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_aggregate_spawn.handle != 0);
        if (c_aggregate_spawn.handle)
        {
            ProcessWaitResult c_aggregate_wait = os_process_wait_sync(arguments->arena, c_aggregate_spawn);
            BUSTER_TEST(arguments, c_aggregate_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
#if (BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64) || BUSTER_MACOS
    String8 c_thread_local_path = buster_test_temporary_path(arguments->arena, S8("buster-c-thread-local"), S8(""));
    String8 c_thread_local_command_line[] = {
        S8("-o"),
        c_thread_local_path,
        S8("tests/basic_c_thread_local.c"),
    };
    CompilerDriverResult c_thread_local = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_thread_local_command_line)));
    BUSTER_TEST(arguments, c_thread_local.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_thread_local.has_object);
    if (c_thread_local.has_object)
    {
        BUSTER_TEST(arguments, c_thread_local.object.sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length == sizeof(u32));
        BUSTER_TEST(arguments, c_thread_local.object.sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].data.length == 0);
        BUSTER_TEST(arguments, c_thread_local.object.sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size == sizeof(u32));
        bool found_thread_local_symbol = false;
        bool found_thread_local_zero_symbol = false;
        bool found_thread_local_relocation = false;
        for (u32 symbol_index = 0; symbol_index < c_thread_local.object.symbol_count; symbol_index += 1)
        {
            ObjectSymbol* symbol = &c_thread_local.object.symbols[symbol_index];
            found_thread_local_symbol |= symbol->section == OBJECT_SECTION_THREAD_LOCAL_DATA && string_equal(symbol->name, S8("thread_local_value"));
            found_thread_local_zero_symbol |= symbol->section == OBJECT_SECTION_THREAD_LOCAL_ZERO && string_equal(symbol->name, S8("thread_local_zero"));
        }
        for (u32 relocation_index = 0; relocation_index < c_thread_local.object.relocation_count; relocation_index += 1)
        {
            ObjectRelocationKind kind = c_thread_local.object.relocations[relocation_index].kind;
            found_thread_local_relocation |= kind == OBJECT_RELOCATION_X86_64_TPOFF32 || kind == OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32 ||
                                             kind == OBJECT_RELOCATION_PE_TLS_OFFSET32 || kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP ||
                                             kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12 || kind == OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12 ||
                                             kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12 || kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12 ||
                                             kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32 || kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21 ||
                                             kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12;
        }
        BUSTER_TEST(arguments, found_thread_local_symbol);
        BUSTER_TEST(arguments, found_thread_local_zero_symbol);
        BUSTER_TEST(arguments, found_thread_local_relocation);
    }
    if (c_thread_local.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_thread_local_arguments[] = {
            c_thread_local_path,
        };
        ProcessSpawnResult c_thread_local_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_thread_local_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_thread_local_spawn.handle != 0);
        if (c_thread_local_spawn.handle)
        {
            ProcessWaitResult c_thread_local_wait = os_process_wait_sync(arguments->arena, c_thread_local_spawn);
            BUSTER_TEST(arguments, c_thread_local_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_thread_local_aarch64_path = buster_test_temporary_path(arguments->arena, S8("buster-c-thread-local-aarch64"), S8(""));
    String8 c_thread_local_aarch64_command_line[] = {
        S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), c_thread_local_aarch64_path, S8("tests/basic_c_thread_local.c"),
    };
    CompilerDriverResult c_thread_local_aarch64 = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_thread_local_aarch64_command_line)));
    BUSTER_TEST(arguments, c_thread_local_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_thread_local_aarch64.error == COMPILER_DRIVER_ERROR_NONE)
    {
        ByteSlice executable = c_thread_local_aarch64.native_link.executable;
        bool machine_matches = executable.length >= 20 && executable.pointer[18] == 183 && executable.pointer[19] == 0;
        bool found_thread_pointer_add = false;
        static u8 const thread_pointer_add[] = {
            0x29,
            0x41,
            0x00,
            0x91,
        };
        for (u64 byte_index = 0; byte_index + sizeof(thread_pointer_add) <= executable.length; byte_index += 1)
        {
            if (memcmp(executable.pointer + byte_index, thread_pointer_add, sizeof(thread_pointer_add)) == 0)
            {
                found_thread_pointer_add = true;
                break;
            }
        }
        BUSTER_TEST(arguments, machine_matches);
        BUSTER_TEST(arguments, found_thread_pointer_add);
    }
    String8 c_thread_local_windows_targets[] = {
        S8("x86_64-pc-windows-msvc"),
        S8("aarch64-pc-windows-msvc"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_thread_local_windows_targets); target_index += 1)
    {
        String8 c_thread_local_windows_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-thread-local-windows"), string_format(arguments->arena, S8("-{u32}.exe"), target_index));
        String8 windows_tls_command_line[] = {
            S8("-target"), c_thread_local_windows_targets[target_index], S8("-o"), c_thread_local_windows_path, S8("tests/basic_c_thread_local.c"),
        };
        CompilerDriverResult windows_tls = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(windows_tls_command_line)));
        BUSTER_TEST(arguments, windows_tls.error == COMPILER_DRIVER_ERROR_NONE);
        if (windows_tls.error == COMPILER_DRIVER_ERROR_NONE)
        {
            ByteSlice executable = windows_tls.native_link.executable;
            bool pe_header = executable.length > 0x84 && executable.pointer[0] == 'M' && executable.pointer[1] == 'Z' && executable.pointer[0x80] == 'P' &&
                             executable.pointer[0x81] == 'E';
            u32 tls_directory_rva = 0;
            if (executable.length >= 0x80 + 4 + 20 + 188)
            {
                memcpy(&tls_directory_rva, executable.pointer + 0x80 + 4 + 20 + 184, sizeof(tls_directory_rva));
            }
            BUSTER_TEST(arguments, pe_header);
            BUSTER_TEST(arguments, tls_directory_rva != 0);
        }
    }
    String8 c_thread_local_apple_targets[] = {
        S8("x86_64-apple-macos"),
        S8("arm64-apple-macos"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_thread_local_apple_targets); target_index += 1)
    {
        String8 apple_tls_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-thread-local-apple"), string_format(arguments->arena, S8("-{u32}"), target_index));
        String8 apple_tls_command_line[] = {
            S8("-target"),  c_thread_local_apple_targets[target_index], S8("-framework"), S8("CoreFoundation"), S8("-o"),
            apple_tls_path, S8("tests/basic_c_thread_local.c"),
        };
        CompilerDriverResult apple_tls = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(apple_tls_command_line)));
        BUSTER_TEST(arguments, apple_tls.error == COMPILER_DRIVER_ERROR_NONE);
        if (apple_tls.error == COMPILER_DRIVER_ERROR_NONE)
        {
            ByteSlice executable = apple_tls.native_link.executable;
            u32 mach_flags = 0;
            if (executable.length >= 28)
            {
                memcpy(&mach_flags, executable.pointer + 24, sizeof(mach_flags));
            }
            BUSTER_TEST(arguments, executable.length >= 32 && executable.pointer[0] == 0xcf && executable.pointer[1] == 0xfa && executable.pointer[2] == 0xed &&
                                       executable.pointer[3] == 0xfe);
            BUSTER_TEST(arguments, (mach_flags & 0x800000) != 0);
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("__thread_vars")));
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("__thread_data")));
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("__thread_bss")));
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("__tlv_bootstrap")));
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("/System/Library/Frameworks/"
                                                                                "CoreFoundation.framework/"
                                                                                "CoreFoundation")));
            bool found_tlv_relocation = false;
            for (u32 relocation_index = 0; relocation_index < apple_tls.object.relocation_count; relocation_index += 1)
            {
                ObjectRelocationKind kind = apple_tls.object.relocations[relocation_index].kind;
                found_tlv_relocation |= kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32 || kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21;
            }
            BUSTER_TEST(arguments, found_tlv_relocation);
        }
    }
    String8 c_float_abi_path = buster_test_temporary_path(arguments->arena, S8("buster-c-float-abi"), S8(""));
    String8 c_float_abi_command_line[] = {
        S8("-o"),
        c_float_abi_path,
        S8("tests/basic_c_float_abi.c"),
    };
    CompilerDriverResult c_float_abi = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_abi_command_line)));
    BUSTER_TEST(arguments, c_float_abi.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_float_abi.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_float_abi_arguments[] = {
            c_float_abi_path,
        };
        ProcessSpawnResult c_float_abi_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_abi_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_float_abi_spawn.handle != 0);
        if (c_float_abi_spawn.handle)
        {
            ProcessWaitResult c_float_abi_wait = os_process_wait_sync(arguments->arena, c_float_abi_spawn);
            BUSTER_TEST(arguments, c_float_abi_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_vector_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector"),
#if BUSTER_WINDOWS
                                                       S8(".exe"));
#else
                                                       S8(""));
#endif
    String8 c_vector_command_line[] = {
        S8("-o"),
        c_vector_path,
        S8("tests/basic_c_vector.c"),
    };
    CompilerDriverResult c_vector = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_command_line)));
    BUSTER_TEST(arguments, c_vector.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, target_native.cpu_arch != CPU_ARCH_X86_64 || c_vector.codegen_statistics.native_vector_operation_count > 0);
    String8 c_vector_baseline_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector-baseline"), S8(".o"));
    String8 c_vector_baseline_command_line[] = {
        S8("-c"), S8("--target=x86_64-linux"), S8("-o"), c_vector_baseline_path, S8("tests/basic_c_vector.c"),
    };
    Arena* c_vector_target_arena = arena_create((ArenaCreation){0});
    CompilerDriverInvocation c_vector_baseline_invocation =
        compiler_driver_parse_arguments(c_vector_target_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_baseline_command_line));
    CompilerDriverResult c_vector_baseline = compiler_driver_execute_invocation(c_vector_target_arena, c_vector_baseline_invocation);
    BUSTER_TEST(arguments, c_vector_baseline.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_vector_baseline.codegen_statistics.split_vector_operation_count == 6);
    BUSTER_TEST(arguments, c_vector_baseline.codegen_statistics.vzeroupper_count == 0);
    BUSTER_TEST(arguments, c_vector_baseline.codegen_statistics.forwarded_wide_vector_load_count == 0);
    u64 c_vector_baseline_native_operations = c_vector_baseline.codegen_statistics.native_vector_operation_count;
    BUSTER_TEST(arguments, arena_destroy(c_vector_target_arena, 1));
    String8 c_vector_avx2_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector-avx2"), S8(".o"));
    String8 c_vector_avx2_command_line[] = {
        S8("-c"), S8("--target=x86_64-linux"), S8("-march=haswell"), S8("-o"), c_vector_avx2_path, S8("tests/basic_c_vector.c"),
    };
    c_vector_target_arena = arena_create((ArenaCreation){0});
    CompilerDriverInvocation c_vector_avx2_invocation =
        compiler_driver_parse_arguments(c_vector_target_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_avx2_command_line));
    CompilerDriverResult c_vector_avx2 = compiler_driver_execute_invocation(c_vector_target_arena, c_vector_avx2_invocation);
    BUSTER_TEST(arguments, c_vector_avx2.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_vector_avx2.codegen_statistics.split_vector_operation_count == 4);
    BUSTER_TEST(arguments, c_vector_avx2.codegen_statistics.native_vector_operation_count == c_vector_baseline_native_operations + 2);
    BUSTER_TEST(arguments, c_vector_avx2.codegen_statistics.vzeroupper_count == 1);
    BUSTER_TEST(arguments, c_vector_avx2.codegen_statistics.forwarded_wide_vector_load_count == 1);
    u64 c_vector_avx2_native_operations = c_vector_avx2.codegen_statistics.native_vector_operation_count;
    BUSTER_TEST(arguments, arena_destroy(c_vector_target_arena, 1));
    String8 c_vector_avx512_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector-avx512"), S8(".o"));
    String8 c_vector_avx512_command_line[] = {
        S8("-c"), S8("--target=x86_64-linux"), S8("-march=znver5"), S8("-o"), c_vector_avx512_path, S8("tests/basic_c_vector.c"),
    };
    c_vector_target_arena = arena_create((ArenaCreation){0});
    CompilerDriverInvocation c_vector_avx512_invocation =
        compiler_driver_parse_arguments(c_vector_target_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_avx512_command_line));
    CompilerDriverResult c_vector_avx512 = compiler_driver_execute_invocation(c_vector_target_arena, c_vector_avx512_invocation);
    BUSTER_TEST(arguments, c_vector_avx512.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_vector_avx512.codegen_statistics.split_vector_operation_count == 0);
    BUSTER_TEST(arguments, c_vector_avx512.codegen_statistics.native_vector_operation_count == c_vector_avx2_native_operations + 4);
    BUSTER_TEST(arguments, c_vector_avx512.codegen_statistics.vzeroupper_count == 3);
    BUSTER_TEST(arguments, c_vector_avx512.codegen_statistics.forwarded_wide_vector_load_count == 3);
    BUSTER_TEST(arguments, arena_destroy(c_vector_target_arena, 1));
    if (c_vector.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_vector_arguments[] = {
            c_vector_path,
        };
        ProcessSpawnResult c_vector_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_arguments), (SliceString8){0}, (SliceString8){0},
                                                             (ProcessSpawnOptions){
                                                                 .use_process_environment = true,
                                                             });
        BUSTER_TEST(arguments, c_vector_spawn.handle != 0);
        if (c_vector_spawn.handle)
        {
            ProcessWaitResult c_vector_wait = os_process_wait_sync(arguments->arena, c_vector_spawn);
            BUSTER_TEST(arguments, c_vector_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_vector_cross_targets[] = {
        S8("x86_64-unknown-linux-gnu"),  S8("x86_64-pc-windows-msvc"),  S8("x86_64-apple-macos"),  S8("x86_64-linux-android"),  S8("x86_64-apple-ios"),
        S8("aarch64-unknown-linux-gnu"), S8("aarch64-pc-windows-msvc"), S8("aarch64-apple-macos"), S8("aarch64-linux-android"), S8("aarch64-apple-ios"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_vector_cross_targets); target_index += 1)
    {
        String8 c_vector_cross_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-vector-cross"), string_format(arguments->arena, S8("-{u32}.o"), target_index));
        String8 c_vector_cross_command_line[] = {
            S8("-c"), S8("-target"), c_vector_cross_targets[target_index], S8("-o"), c_vector_cross_path, S8("tests/basic_c_vector.c"),
        };
        CompilerDriverResult c_vector_cross = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_cross_command_line)));
        BUSTER_TEST(arguments, c_vector_cross.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, c_vector_cross.has_object);
    }
    String8 c_conversions_path = buster_test_temporary_path(arguments->arena, S8("buster-c-conversions"), S8(""));
    String8 c_conversions_command_line[] = {
        S8("-o"),
        c_conversions_path,
        S8("tests/basic_c_conversions.c"),
    };
    CompilerDriverResult c_conversions = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_conversions_command_line)));
    BUSTER_TEST(arguments, c_conversions.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_conversions.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_conversions_arguments[] = {
            c_conversions_path,
        };
        ProcessSpawnResult c_conversions_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_conversions_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_conversions_spawn.handle != 0);
        if (c_conversions_spawn.handle)
        {
            ProcessWaitResult c_conversions_wait = os_process_wait_sync(arguments->arena, c_conversions_spawn);
            BUSTER_TEST(arguments, c_conversions_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 conversion_cross_targets[] = {
        S8("aarch64-unknown-linux-gnu"),
        S8("x86_64-pc-windows-msvc"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(conversion_cross_targets); target_index += 1)
    {
        String8 c_conversions_cross_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-conversions-cross"), string_format(arguments->arena, S8("-{u32}.o"), target_index));
        String8 c_conversions_cross_command_line[] = {
            S8("-c"), S8("-target"), conversion_cross_targets[target_index], S8("-o"), c_conversions_cross_path, S8("tests/basic_c_conversions.c"),
        };
        CompilerDriverResult c_conversions_cross = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_conversions_cross_command_line)));
        BUSTER_TEST(arguments, c_conversions_cross.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, c_conversions_cross.has_object);
    }
    String8 c_float_abi_aarch64_path = buster_test_temporary_path(arguments->arena, S8("buster-c-float-abi-aarch64"), S8(""));
    String8 c_float_abi_aarch64_command_line[] = {
        S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), c_float_abi_aarch64_path, S8("tests/basic_c_float_abi.c"),
    };
    CompilerDriverResult c_float_abi_aarch64 = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_abi_aarch64_command_line)));
    BUSTER_TEST(arguments, c_float_abi_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_float_abi_aarch64.error == COMPILER_DRIVER_ERROR_NONE)
    {
        static u8 const aarch64_float_add[] = {
            0x00,
            0x28,
            0x61,
            0x1e,
        };
        ByteSlice executable = c_float_abi_aarch64.native_link.executable;
        bool found_float_add = false;
        for (u64 byte_index = 0; byte_index + sizeof(aarch64_float_add) <= executable.length; byte_index += 1)
        {
            if (memcmp(executable.pointer + byte_index, aarch64_float_add, sizeof(aarch64_float_add)) == 0)
            {
                found_float_add = true;
                break;
            }
        }
        BUSTER_TEST(arguments, found_float_add);
    }
    String8 c_float_abi_windows_path = buster_test_temporary_path(arguments->arena, S8("buster-c-float-abi-windows"), S8(".obj"));
    String8 c_float_abi_windows_command_line[] = {
        S8("-c"), S8("-target"), S8("x86_64-pc-windows-msvc"), S8("-o"), c_float_abi_windows_path, S8("tests/basic_c_float_abi.c"),
    };
    CompilerDriverResult c_float_abi_windows = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_abi_windows_command_line)));
    BUSTER_TEST(arguments, c_float_abi_windows.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_float_abi_windows.error == COMPILER_DRIVER_ERROR_NONE)
    {
        static u8 const shadow_space[] = {
            0x48,
            0x83,
            0xec,
            0x20,
        };
        static u8 const load_xmm0[] = {
            0xf2,
            0x0f,
            0x10,
            0x85,
        };
        static u8 const load_indirect_second_part[] = {
            0x48,
            0x8b,
            0x41,
            0x08,
        };
        ByteSlice text = c_float_abi_windows.object.sections[OBJECT_SECTION_TEXT].data;
        bool found_shadow_space = false;
        bool found_load_xmm0 = false;
        bool found_indirect_second_part = false;
        for (u64 byte_index = 0; byte_index < text.length; byte_index += 1)
        {
            if (byte_index + sizeof(shadow_space) <= text.length && memcmp(text.pointer + byte_index, shadow_space, sizeof(shadow_space)) == 0)
            {
                found_shadow_space = true;
            }
            if (byte_index + sizeof(load_xmm0) <= text.length && memcmp(text.pointer + byte_index, load_xmm0, sizeof(load_xmm0)) == 0)
            {
                found_load_xmm0 = true;
            }
            if (byte_index + sizeof(load_indirect_second_part) <= text.length &&
                memcmp(text.pointer + byte_index, load_indirect_second_part, sizeof(load_indirect_second_part)) == 0)
            {
                found_indirect_second_part = true;
            }
        }
        BUSTER_TEST(arguments, found_shadow_space);
        BUSTER_TEST(arguments, found_load_xmm0);
        BUSTER_TEST(arguments, found_indirect_second_part);
    }
    Arena* c_asm_conflicts[] = {
        arguments->arena,
    };
    TemporalArena c_asm_temporary = scratch_begin(c_asm_conflicts, BUSTER_ARRAY_LENGTH(c_asm_conflicts));
    Arena* c_asm_arena = c_asm_temporary.arena;
    String8 c_asm_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-asm"), S8(""));
    String8 c_asm_command_line[] = {
        S8("-o"),
        c_asm_path,
        S8("tests/basic_c_asm.c"),
    };
    CompilerDriverResult c_asm =
        compiler_driver_execute_invocation(c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_asm_command_line)));
    BUSTER_TEST(arguments, c_asm.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_asm.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_asm_arguments[] = {
            c_asm_path,
        };
        ProcessSpawnResult c_asm_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_asm_arguments), (SliceString8){0}, (SliceString8){0},
                                                          (ProcessSpawnOptions){
                                                              .use_process_environment = true,
                                                          });
        BUSTER_TEST(arguments, c_asm_spawn.handle != 0);
        if (c_asm_spawn.handle)
        {
            ProcessWaitResult c_asm_wait = os_process_wait_sync(c_asm_arena, c_asm_spawn);
            BUSTER_TEST(arguments, c_asm_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_asm_aarch64_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-asm-aarch64"), S8(""));
    String8 c_asm_aarch64_command_line[] = {
        S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), c_asm_aarch64_path, S8("tests/basic_c_asm.c"),
    };
    CompilerDriverResult c_asm_aarch64 = compiler_driver_execute_invocation(
        c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_asm_aarch64_command_line)));
    BUSTER_TEST(arguments, c_asm_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_asm_aarch64.error == COMPILER_DRIVER_ERROR_NONE)
    {
        BUSTER_TEST(arguments, compiler_driver_bytes_contain(c_asm_aarch64.native_link.executable, (String8){
                                                                                                       .pointer =
                                                                                                           (char8*)(u8[]){
                                                                                                               0x1f,
                                                                                                               0x20,
                                                                                                               0x03,
                                                                                                               0xd5,
                                                                                                           },
                                                                                                       .length = 4,
                                                                                                   }));
    }
    scratch_end(c_asm_temporary);
#endif
    Arena* c_multi_conflicts[] = {
        arguments->arena,
    };
    TemporalArena c_multi_temporary = scratch_begin(c_multi_conflicts, BUSTER_ARRAY_LENGTH(c_multi_conflicts));
    Arena* c_multi_arena = c_multi_temporary.arena;
    String8 c_multi_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-multi-driver"),
#if BUSTER_WINDOWS
                                                      S8(".exe"));
#else
                                                      S8(""));
#endif
    String8 c_multi_command_line[] = {
        S8("-o"),
        c_multi_path,
        S8("tests/basic_c_multi_main.c"),
        S8("tests/basic_c_multi_add.c"),
    };
    CompilerDriverResult c_multi = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_multi_command_line)));
    BUSTER_TEST(arguments, c_multi.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_multi.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_multi_arguments[] = {
            c_multi_path,
        };
        ProcessSpawnResult c_multi_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_multi_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .use_process_environment = true,
                                                            });
        BUSTER_TEST(arguments, c_multi_spawn.handle != 0);
        if (c_multi_spawn.handle)
        {
            ProcessWaitResult c_multi_wait = os_process_wait_sync(c_multi_arena, c_multi_spawn);
            BUSTER_TEST(arguments, c_multi_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_multi_object_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-multi-object"),
#if BUSTER_WINDOWS
                                                             S8(".obj"));
#else
                                                             S8(".o"));
#endif
    String8 c_multi_object_command_line[] = {
        S8("-c"),
        S8("-o"),
        c_multi_object_path,
        S8("tests/basic_c_multi_add.c"),
    };
    CompilerDriverResult c_multi_object = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_multi_object_command_line)));
    BUSTER_TEST(arguments, c_multi_object.error == COMPILER_DRIVER_ERROR_NONE);
    String8 c_mixed_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-mixed-driver"),
#if BUSTER_WINDOWS
                                                      S8(".exe"));
#else
                                                      S8(""));
#endif
    String8 c_mixed_command_line[] = {
        S8("-o"),
        c_mixed_path,
        S8("tests/basic_c_multi_main.c"),
        c_multi_object_path,
    };
    CompilerDriverResult c_mixed = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_mixed_command_line)));
    BUSTER_TEST(arguments, c_mixed.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_mixed.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_mixed_arguments[] = {
            c_mixed_path,
        };
        ProcessSpawnResult c_mixed_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_mixed_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .use_process_environment = true,
                                                            });
        BUSTER_TEST(arguments, c_mixed_spawn.handle != 0);
        if (c_mixed_spawn.handle)
        {
            ProcessWaitResult c_mixed_wait = os_process_wait_sync(c_multi_arena, c_mixed_spawn);
            BUSTER_TEST(arguments, c_mixed_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_archive_bias_object_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-archive-bias"),
#if BUSTER_WINDOWS
                                                                    S8(".obj"));
#else
                                                                    S8(".o"));
#endif
    String8 c_archive_add_object_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-archive-add"),
#if BUSTER_WINDOWS
                                                                   S8(".obj"));
#else
                                                                   S8(".o"));
#endif
    String8 c_archive_bias_command_line[] = {
        S8("-c"),
        S8("-o"),
        c_archive_bias_object_path,
        S8("tests/basic_c_archive_bias.c"),
    };
    String8 c_archive_add_command_line[] = {
        S8("-c"),
        S8("-o"),
        c_archive_add_object_path,
        S8("tests/basic_c_archive_add.c"),
    };
    CompilerDriverResult c_archive_bias = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_archive_bias_command_line)));
    CompilerDriverResult c_archive_add = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_archive_add_command_line)));
    BUSTER_TEST(arguments, c_archive_bias.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_archive_add.error == COMPILER_DRIVER_ERROR_NONE);
    FileMapRead c_archive_bias_map = file_map_read(c_multi_arena, c_archive_bias_object_path, (FileReadOptions){0});
    FileMapRead c_archive_add_map = file_map_read(c_multi_arena, c_archive_add_object_path, (FileReadOptions){0});
    ByteSlice c_archive_members[] = {
        c_archive_bias_map.bytes,
        c_archive_add_map.bytes,
    };
    String8 c_archive_names[] = {
        S8("bias.o"),
        S8("add.o"),
    };
    ByteSlice c_archive_bytes = compiler_driver_test_archive(c_multi_arena, c_archive_members, c_archive_names, BUSTER_ARRAY_LENGTH(c_archive_members));
    file_map_unmap(c_archive_bias_map);
    file_map_unmap(c_archive_add_map);
    String8 c_archive_directory = buster_test_temporary_path(c_multi_arena, S8("buster-c-driver-archive"), S8(""));
    os_make_directory(c_archive_directory);
    String8 c_archive_path = string_format_z(c_multi_arena,
#if BUSTER_WINDOWS
                                             S8("{S8}/archive_chain.lib"),
#else
                                             S8("{S8}/libarchive_chain.a"),
#endif
                                             c_archive_directory);
    BUSTER_TEST(arguments, file_write(c_archive_path, c_archive_bytes));
    String8 c_archive_executable_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-archive-driver"),
#if BUSTER_WINDOWS
                                                                   S8(".exe"));
#else
                                                                   S8(""));
#endif
    String8 c_archive_command_line[] = {
        S8("-o"), c_archive_executable_path, S8("tests/basic_c_multi_main.c"), S8("-L"), c_archive_directory, S8("-larchive_chain"),
    };
    CompilerDriverResult c_archive = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_archive_command_line)));
    BUSTER_TEST(arguments, c_archive.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_archive.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_archive_arguments[] = {
            c_archive_executable_path,
        };
        ProcessSpawnResult c_archive_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_archive_arguments), (SliceString8){0}, (SliceString8){0},
                                                              (ProcessSpawnOptions){
                                                                  .use_process_environment = true,
                                                              });
        BUSTER_TEST(arguments, c_archive_spawn.handle != 0);
        if (c_archive_spawn.handle)
        {
            ProcessWaitResult c_archive_wait = os_process_wait_sync(c_multi_arena, c_archive_spawn);
            BUSTER_TEST(arguments, c_archive_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    scratch_end(c_multi_temporary);
    String8 source_path = buster_test_temporary_path(arguments->arena, S8("buster-driver-test"), S8(".bbb"));
    String8 output_path = buster_test_temporary_path(arguments->arena, S8("buster-driver-test"),
#if BUSTER_WINDOWS
                                                     S8(".exe"));
#else
                                                     S8(""));
#endif
    String8 source = S8("code main[export] : fn () s32\n"
                        "{\n"
                        "    return 0;\n"
                        "}\n");
    BUSTER_TEST(arguments, file_write(source_path, (ByteSlice){
                                                       .pointer = (u8*)source.pointer,
                                                       .length = source.length,
                                                   }));
    CompilerDriverResult compile = compiler_driver_compile(arguments->arena, (CompilerDriverOptions){
                                                                                 .source_path = source_path,
                                                                                 .output_path = output_path,
                                                                                 .target = target_native,
                                                                             });
    if (compile.error != COMPILER_DRIVER_ERROR_NONE && compile.diagnostic.length)
    {
        arguments->show(arguments, S8("compiler driver error: {S8}\n"), compile.diagnostic);
    }
    BUSTER_TEST(arguments, compile.error == COMPILER_DRIVER_ERROR_NONE);
    if (compile.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 run_arguments[] = {
            output_path,
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
#if BUSTER_LINUX
    // The buster driver path must also emit resolvable DWARF when asked.
    String8 buster_debug_path = buster_test_temporary_path(arguments->arena, S8("buster-driver-debug"), S8(""));
    CompilerDriverResult buster_debug_compile = compiler_driver_compile(arguments->arena, (CompilerDriverOptions){
                                                                                              .source_path = source_path,
                                                                                              .output_path = buster_debug_path,
                                                                                              .target = target_native,
                                                                                              .debug_info = true,
                                                                                          });
    BUSTER_TEST(arguments, buster_debug_compile.error == COMPILER_DRIVER_ERROR_NONE);
    if (buster_debug_compile.error == COMPILER_DRIVER_ERROR_NONE)
    {
        ByteSlice buster_debug_image = file_read(arguments->arena, buster_debug_path, (FileReadOptions){0});
        ByteSlice buster_debug_line = compiler_driver_test_elf_section(buster_debug_image, S8(".debug_line"));
        ByteSlice buster_debug_info = compiler_driver_test_elf_section(buster_debug_image, S8(".debug_info"));
        u64 buster_debug_text_address = compiler_driver_test_elf_section_address(buster_debug_image, S8(".text"));
        BUSTER_TEST(arguments, buster_debug_line.length > 4);
        BUSTER_TEST(arguments, buster_debug_info.length > 11);
        BUSTER_TEST(arguments, buster_debug_text_address != 0);
        DwarfLineRow buster_debug_row = {0};
        BUSTER_TEST(arguments, dwarf_line_lookup(buster_debug_line, buster_debug_text_address, &buster_debug_row));
        BUSTER_TEST(arguments, buster_debug_row.line == 1 && buster_debug_row.file == 1);
    }
#endif
    String8 module_directory = buster_test_temporary_path(arguments->arena, S8("buster-driver-modules"), S8(""));
    String8 module_child_directory = string_format_z(arguments->arena, S8("{S8}/core"), module_directory);
    String8 module_root_path = string_format_z(arguments->arena, S8("{S8}/main.bbb"), module_directory);
    String8 module_dependency_path = string_format_z(arguments->arena, S8("{S8}/core/math.bbb"), module_directory);
    String8 module_output_path = buster_test_temporary_path(arguments->arena, S8("buster-driver-modules-executable"),
#if BUSTER_WINDOWS
                                                            S8(".exe"));
#else
                                                            S8(""));
#endif
    os_make_directory(module_directory);
    os_make_directory(module_child_directory);
    String8 module_root_source = S8("import math = \"core/math\";\n"
                                    "code main[export] : fn[cc(c)] () s32\n"
                                    "{\n"
                                    "    return math.answer() - 42;\n"
                                    "}\n");
    String8 module_dependency_source = S8("code answer : fn () s32\n"
                                          "{\n"
                                          "    return 42;\n"
                                          "}\n");
    BUSTER_TEST(arguments, file_write(module_root_path, (ByteSlice){
                                                            .pointer = (u8*)module_root_source.pointer,
                                                            .length = module_root_source.length,
                                                        }));
    BUSTER_TEST(arguments, file_write(module_dependency_path, (ByteSlice){
                                                                  .pointer = (u8*)module_dependency_source.pointer,
                                                                  .length = module_dependency_source.length,
                                                              }));
    CompilerDriverResult module_compile = compiler_driver_compile(arguments->arena, (CompilerDriverOptions){
                                                                                        .source_path = module_root_path,
                                                                                        .output_path = module_output_path,
                                                                                        .module_root = module_directory,
                                                                                        .target = target_native,
                                                                                    });
    if (module_compile.error != COMPILER_DRIVER_ERROR_NONE && module_compile.diagnostic.length)
    {
        arguments->show(arguments, S8("module compiler driver error: {S8}\n"), module_compile.diagnostic);
    }
    BUSTER_TEST(arguments, module_compile.error == COMPILER_DRIVER_ERROR_NONE);
    if (module_compile.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 run_arguments[] = {
            module_output_path,
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
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}
