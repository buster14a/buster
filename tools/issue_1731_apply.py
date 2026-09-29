from pathlib import Path


def replace_once(path, old, new):
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}")
    file.write_text(text.replace(old, new, 1))


replace_once(
    "src/buster/lib/compiler/frontend/c/c_internal.h",
    "BUSTER_C_EXTERN bool c_ir_decode_character_value(Arena* arena, char8 const* spelling_base, CToken token, Target target,\n",
    "BUSTER_C_EXTERN bool c_ir_target_supports_f80(Target target);\n"
    "BUSTER_C_EXTERN bool c_ir_decode_character_value(Arena* arena, char8 const* spelling_base, CToken token, Target target,\n",
)
replace_once(
    "src/buster/lib/compiler/frontend/c/c_gen.c",
    "BUSTER_C_INTERNAL bool c_ir_target_supports_f80(Target target)",
    "BUSTER_C_SHARED bool c_ir_target_supports_f80(Target target)",
)
file = Path("src/buster/lib/compiler/frontend/c/c_gen.c")
text = file.read_text()
anchor = '        if (source_value->kind == IR_TYPE_FLOAT && source_value->bit_width == 64)\n'
start = text.index('String8 runtime = S8("__truncsfhf2")')
at = text.index(anchor, start)
case = (
    '        if (source_value->kind == IR_TYPE_FLOAT && source_value->bit_width == 80 &&\n'
    '            c_ir_target_supports_f80(builder->target))\n'
    '        {\n'
    '            runtime = S8("__truncxfhf2");\n'
    '            runtime_parameter = source_type;\n'
    '        }\n'
    '        else '
)
file.write_text(text[:at] + case + text[at + 8:])
replace_once(
    "src/buster/lib/compiler/frontend/c/c_parse.c",
    '    else if (runtime && to == C_TYPE_FLOAT16 && (from == C_TYPE_LONG_DOUBLE || from == C_TYPE_LONG_DOUBLE_COMPLEX) &&\n'
    '             target_data_layout(target).long_double_type.bit_width > 64)\n'
    '        message = S8("C IR lowering does not support this runtime conversion to binary16");',
    '    else if (runtime && to == C_TYPE_FLOAT16 && (from == C_TYPE_LONG_DOUBLE || from == C_TYPE_LONG_DOUBLE_COMPLEX) &&\n'
    '             target_data_layout(target).long_double_type.bit_width > 64 && !c_ir_target_supports_f80(target))\n'
    '        message = S8("C IR lowering does not support this runtime conversion to binary16");',
)
replace_once(
    "docs/agents/frontend/wide-floats-assembly.md",
    "  `__extendhfsf2`, performs arithmetic in binary32, and rounds immediately back\n"
    "  through `__truncsfhf2`; a binary64 source uses `__truncdfhf2`. Darwin x86-64's\n",
    "  `__extendhfsf2`, performs arithmetic in binary32, and rounds immediately back\n"
    "  through `__truncsfhf2`; a binary64 source uses `__truncdfhf2`. On supported\n"
    "  System V x86-64 targets, an x87 `long double` source uses `__truncxfhf2`\n"
    "  directly, avoiding an intermediate binary64 rounding; widening a half to x87\n"
    "  remains exact through binary32. Darwin x86-64's\n",
)
driver_function = '''// A direct x87-to-binary16 conversion avoids the double rounding that occurs
// immediately above a binary16 half-way point. Keep all allocator/frontend/PIC
// objects strict and compare runtime bits with the host compiler on Linux x64.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_x86_64_f80_float16_runtime(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source_path = buster_test_temporary_path(arguments->arena, S8("buster-x64-f80-f16"), S8(".c"));
    String8 source = S8(
        "#if __LDBL_MANT_DIG__ == 64\\n"
        "typedef union HalfImage { _Float16 value; unsigned short bits; } HalfImage;\\n"
        "typedef union X87Image\\n"
        "{\\n"
        "    long double value;\\n"
        "    struct { unsigned long long significand; unsigned short sign_exponent; } bits;\\n"
        "} X87Image;\\n"
        "#ifdef BUSTER_F80_F16_LIBRARY\\n"
        "_Float16 f80_to_f16(long double value) { return (_Float16)value; }\\n"
        "_Float16 complex_f80_to_f16(_Complex long double value) { return (_Float16)value; }\\n"
        "long double f16_to_f80(_Float16 value) { return (long double)value; }\\n"
        "#else\\n"
        "_Float16 f80_to_f16(long double value);\\n"
        "_Float16 complex_f80_to_f16(_Complex long double value);\\n"
        "long double f16_to_f80(_Float16 value);\\n"
        "int main(void)\\n"
        "{\\n"
        "    volatile long double values[] = {\\n"
        "        0x1.002p0L, 0x1.0020000000000002p0L, 0x1.006p0L,\\n"
        "        0x1p-24L, 0x1p-25L, 0x1.0000000000000002p-25L,\\n"
        "        0x1.ffcp15L, 0x1.ffep15L, -0x1.0020000000000002p0L,\\n"
        "    };\\n"
        "    int failed = 0;\\n"
        "    for (unsigned index = 0; index < sizeof(values) / sizeof(values[0]); index += 1)\\n"
        "    {\\n"
        "        HalfImage expected = {.value = (_Float16)values[index]};\\n"
        "        HalfImage actual = {.value = f80_to_f16(values[index])};\\n"
        "        failed |= expected.bits != actual.bits;\\n"
        "    }\\n"
        "    X87Image nan = {0};\\n"
        "    nan.bits.significand = 0xc000000000000123ULL;\\n"
        "    nan.bits.sign_exponent = 0x7fff;\\n"
        "    HalfImage expected_nan = {.value = (_Float16)nan.value};\\n"
        "    HalfImage actual_nan = {.value = f80_to_f16(nan.value)};\\n"
        "    failed |= expected_nan.bits != actual_nan.bits;\\n"
        "    _Complex long double complex_value;\\n"
        "    __real__ complex_value = values[1];\\n"
        "    __imag__ complex_value = nan.value;\\n"
        "    HalfImage expected_complex = {.value = (_Float16)complex_value};\\n"
        "    HalfImage actual_complex = {.value = complex_f80_to_f16(complex_value)};\\n"
        "    failed |= expected_complex.bits != actual_complex.bits;\\n"
        "    unsigned short finite_bits[] = {0x0001, 0x03ff, 0x3c00, 0x7bff, 0x8001};\\n"
        "    for (unsigned index = 0; index < sizeof(finite_bits) / sizeof(finite_bits[0]); index += 1)\\n"
        "    {\\n"
        "        HalfImage input = {.bits = finite_bits[index]};\\n"
        "        failed |= f16_to_f80(input.value) != (long double)input.value;\\n"
        "    }\\n"
        "    HalfImage negative_zero = {.bits = 0x8000};\\n"
        "    X87Image wide_zero = {.value = f16_to_f80(negative_zero.value)};\\n"
        "    failed |= wide_zero.bits.significand != 0 || wide_zero.bits.sign_exponent != 0x8000;\\n"
        "    return failed;\\n"
        "}\\n"
        "#endif\\n"
        "#else\\n"
        "int main(void) { return 0; }\\n"
        "#endif\\n"
    );
    if (!BUSTER_REQUIRE(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(source))))
    {
        return result;
    }
    String8 modes[] = {S8("-fregister-allocator=none"), S8("-fregister-allocator=mir-stack"),
                       S8("-fregister-allocator=fast"), S8("-fregister-allocator=quality")};
    String8 frontends[] = {S8("-fno-frontend-ssa"), S8("-ffrontend-ssa")};
    String8 positions[] = {S8("-fno-pic"), S8("-fPIC")};
#if defined(BUSTER_HOST_C_COMPILER) && BUSTER_CPU_ARCH_X86_64 && BUSTER_LINUX && !BUSTER_ANDROID
    String8 host_object = buster_test_temporary_path(arguments->arena, S8("buster-x64-f80-f16-host"), S8(".o"));
    String8 host_command[12];
    u32 host_count = 0;
    host_command[host_count++] = S8(BUSTER_HOST_C_COMPILER);
    if (S8(BUSTER_HOST_C_COMPILER_ARG1).length) { host_command[host_count++] = S8(BUSTER_HOST_C_COMPILER_ARG1); }
    host_command[host_count++] = S8("-O0");
    host_command[host_count++] = S8("-fno-pie");
    host_command[host_count++] = S8("-c");
    host_command[host_count++] = source_path;
    host_command[host_count++] = S8("-o");
    host_command[host_count++] = host_object;
    ProcessSpawnResult host_spawn = os_process_spawn((SliceString8){.pointer = host_command, .length = host_count},
        (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true, .search_path = true});
    bool host_compiled = host_spawn.handle && os_process_wait_sync(arguments->arena, host_spawn).result == PROCESS_RESULT_SUCCESS;
    BUSTER_TEST(arguments, host_compiled);
#endif
    for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
    {
        for (u32 frontend = 0; frontend < BUSTER_ARRAY_LENGTH(frontends); frontend += 1)
        {
            for (u32 position = 0; position < BUSTER_ARRAY_LENGTH(positions); position += 1)
            {
                TemporalArena temporary = scratch_begin(&arguments->arena, 1);
                String8 object = buster_test_temporary_path(temporary.arena, S8("buster-x64-f80-f16"), S8(".o"));
                String8 command[] = {S8("-c"), S8("-g0"), S8("-target"), S8("x86_64-linux"), S8("-march=baseline"),
                    modes[mode], frontends[frontend], positions[position], S8("-DBUSTER_F80_F16_LIBRARY=1"),
                    S8("-fverify-codegen"), S8("-o"), object, source_path};
                CompilerDriverInvocation invocation = compiler_driver_parse_arguments(
                    temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                invocation.reject_machine_fallback = mode != 0;
                CompilerDriverResult compiled = compiler_driver_execute_invocation(temporary.arena, invocation);
                String8 description = string_format(temporary.arena, S8("x87 f80/f16 {S8} {S8} {S8}: {S8}"),
                    modes[mode], frontends[frontend], positions[position], compiled.diagnostic);
                BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object, description);
                BUSTER_TEST_RAW(arguments, compiled.codegen_statistics.function_count == 3 &&
                    compiled.codegen_statistics.fallback_function_count == 0, description);
                if (compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object)
                {
                    ObjectSymbol const* trunc_x = compiler_driver_test_object_symbol(&compiled.object, S8("__truncxfhf2"));
                    ObjectSymbol const* extend_h = compiler_driver_test_object_symbol(&compiled.object, S8("__extendhfsf2"));
                    ObjectSymbol const* trunc_d = compiler_driver_test_object_symbol(&compiled.object, S8("__truncdfhf2"));
                    ObjectSymbol const* trunc_s = compiler_driver_test_object_symbol(&compiled.object, S8("__truncsfhf2"));
                    BUSTER_TEST_RAW(arguments, trunc_x && trunc_x->section == OBJECT_SECTION_UNDEFINED, description);
                    BUSTER_TEST_RAW(arguments, extend_h && extend_h->section == OBJECT_SECTION_UNDEFINED, description);
                    BUSTER_TEST_RAW(arguments, !trunc_d && !trunc_s, description);
                }
#if defined(BUSTER_HOST_C_COMPILER) && BUSTER_CPU_ARCH_X86_64 && BUSTER_LINUX && !BUSTER_ANDROID
                if (host_compiled && compiled.error == COMPILER_DRIVER_ERROR_NONE)
                {
                    String8 executable = buster_test_temporary_path(temporary.arena, S8("buster-x64-f80-f16-run"), S8(".elf"));
                    String8 link[10];
                    u32 link_count = 0;
                    link[link_count++] = S8(BUSTER_HOST_C_COMPILER);
                    if (S8(BUSTER_HOST_C_COMPILER_ARG1).length) { link[link_count++] = S8(BUSTER_HOST_C_COMPILER_ARG1); }
                    link[link_count++] = S8("-no-pie");
                    link[link_count++] = host_object;
                    link[link_count++] = object;
                    link[link_count++] = S8("-lgcc");
                    link[link_count++] = S8("-o");
                    link[link_count++] = executable;
                    ProcessSpawnResult linked = os_process_spawn((SliceString8){.pointer = link, .length = link_count},
                        (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true, .search_path = true});
                    bool link_ok = linked.handle && os_process_wait_sync(temporary.arena, linked).result == PROCESS_RESULT_SUCCESS;
                    BUSTER_TEST_RAW(arguments, link_ok, description);
                    if (link_ok) { BUSTER_TEST_RAW(arguments, compiler_driver_test_process_success(temporary.arena, executable), description); }
                }
#endif
                scratch_end(temporary);
            }
        }
    }
    BUSTER_TEST(arguments, os_file_delete(source_path));
    return result;
}

'''
replace_once(
    "src/buster/tests/compiler/driver/driver_test.c",
    "// AAPCS64 binary128 transport is a deliberately narrower contract than full\n",
    driver_function + "// AAPCS64 binary128 transport is a deliberately narrower contract than full\n",
)
replace_once(
    "src/buster/tests/compiler/driver/driver_test.c",
    "    BUSTER_TEST_FIXTURE(arguments, compiler_driver_test_aarch64_float_to_f128);\n"
    "    BUSTER_TEST_FIXTURE(arguments, compiler_driver_test_aarch64_binary128_transport);\n",
    "    BUSTER_TEST_FIXTURE(arguments, compiler_driver_test_aarch64_float_to_f128);\n"
    "    BUSTER_TEST_FIXTURE(arguments, compiler_driver_test_x86_64_f80_float16_runtime);\n"
    "    BUSTER_TEST_FIXTURE(arguments, compiler_driver_test_aarch64_binary128_transport);\n",
)
