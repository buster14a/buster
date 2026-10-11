#pragma once

// Foreign callees deliberately leave every bit above their ABI result dirty.
// The host compiler assembles their exact RAX images; independent C clients
// validate literal expectations before any Buster object is executed.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_x64_narrow_call_results(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8(
        "extern _Bool foreign_bool_false(void);\n"
        "long long direct_bool_false(void) { return foreign_bool_false(); }\n"
        "long long indirect_bool_false(_Bool (*probe)(void)) { return probe(); }\n"
        "extern _Bool foreign_bool_true(void);\n"
        "long long direct_bool_true(void) { return foreign_bool_true(); }\n"
        "long long indirect_bool_true(_Bool (*probe)(void)) { return probe(); }\n"
        "extern signed char foreign_s8_min(void);\n"
        "long long direct_s8_min(void) { return foreign_s8_min(); }\n"
        "long long indirect_s8_min(signed char (*probe)(void)) { return probe(); }\n"
        "extern signed char foreign_s8_max(void);\n"
        "long long direct_s8_max(void) { return foreign_s8_max(); }\n"
        "long long indirect_s8_max(signed char (*probe)(void)) { return probe(); }\n"
        "extern unsigned char foreign_u8_max(void);\n"
        "long long direct_u8_max(void) { return foreign_u8_max(); }\n"
        "long long indirect_u8_max(unsigned char (*probe)(void)) { return probe(); }\n"
        "extern short foreign_s16_min(void);\n"
        "long long direct_s16_min(void) { return foreign_s16_min(); }\n"
        "long long indirect_s16_min(short (*probe)(void)) { return probe(); }\n"
        "extern short foreign_s16_max(void);\n"
        "long long direct_s16_max(void) { return foreign_s16_max(); }\n"
        "long long indirect_s16_max(short (*probe)(void)) { return probe(); }\n"
        "extern unsigned short foreign_u16_max(void);\n"
        "long long direct_u16_max(void) { return foreign_u16_max(); }\n"
        "long long indirect_u16_max(unsigned short (*probe)(void)) { return probe(); }\n"
        "extern int foreign_s32_min(void);\n"
        "long long direct_s32_min(void) { return foreign_s32_min(); }\n"
        "long long indirect_s32_min(int (*probe)(void)) { return probe(); }\n"
        "extern unsigned int foreign_u32_max(void);\n"
        "long long direct_u32_max(void) { return foreign_u32_max(); }\n"
        "long long indirect_u32_max(unsigned int (*probe)(void)) { return probe(); }\n"
        "extern unsigned long long foreign_u64_value(void);\n"
        "long long direct_u64_value(void) { return foreign_u64_value(); }\n"
        "long long indirect_u64_value(unsigned long long (*probe)(void)) { return probe(); }\n"
        "int direct_bool_branches(void) { int bad = 0; if (foreign_bool_false()) { bad |= 1; } if (!foreign_bool_true()) { bad |= 2; } return bad; }\n"
        "int indirect_bool_branches(_Bool (*a)(void), _Bool (*b)(void)) { int bad = 0; if (a()) { bad |= 1; } if (!b()) { bad |= 2; } return bad; }\n");
    String8 input = buster_test_temporary_path(arguments->arena, S8("buster-x64-narrow-results"), S8(".c"));
    bool written = file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(source));
    BUSTER_TEST(arguments, written);
#if defined(BUSTER_HOST_C_COMPILER) && BUSTER_CPU_ARCH_X86_64 && (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS
    String8 host_parts[] = {S8(
        "#if defined(__APPLE__)\n"
        "#define ABI_SYMBOL \"_\"\n"
        "#else\n"
        "#define ABI_SYMBOL \"\"\n"
        "#endif\n"
        "__asm__(\".text\\n.p2align 4\\n.globl \" ABI_SYMBOL \"foreign_bool_false\\n\" ABI_SYMBOL \"foreign_bool_false:\\nmovabsq $0x5a96c3e17b428d00, %rax\\nret\\n\");\n"
        "extern _Bool foreign_bool_false(void);\n"
        "extern long long direct_bool_false(void);\n"
        "extern long long indirect_bool_false(_Bool (*)(void));\n"
        "__asm__(\".text\\n.p2align 4\\n.globl \" ABI_SYMBOL \"foreign_bool_true\\n\" ABI_SYMBOL \"foreign_bool_true:\\nmovabsq $0x5a96c3e17b428d01, %rax\\nret\\n\");\n"
        "extern _Bool foreign_bool_true(void);\n"
        "extern long long direct_bool_true(void);\n"
        "extern long long indirect_bool_true(_Bool (*)(void));\n"
        "__asm__(\".text\\n.p2align 4\\n.globl \" ABI_SYMBOL \"foreign_s8_min\\n\" ABI_SYMBOL \"foreign_s8_min:\\nmovabsq $0x5a96c3e17b428d80, %rax\\nret\\n\");\n"
        "extern signed char foreign_s8_min(void);\n"
        "extern long long direct_s8_min(void);\n"
        "extern long long indirect_s8_min(signed char (*)(void));\n"
        "__asm__(\".text\\n.p2align 4\\n.globl \" ABI_SYMBOL \"foreign_s8_max\\n\" ABI_SYMBOL \"foreign_s8_max:\\nmovabsq $0x5a96c3e17b428d7f, %rax\\nret\\n\");\n"
        "extern signed char foreign_s8_max(void);\n"
        "extern long long direct_s8_max(void);\n"
        "extern long long indirect_s8_max(signed char (*)(void));\n"
        "__asm__(\".text\\n.p2align 4\\n.globl \" ABI_SYMBOL \"foreign_u8_max\\n\" ABI_SYMBOL \"foreign_u8_max:\\nmovabsq $0x5a96c3e17b428dff, %rax\\nret\\n\");\n"
        "extern unsigned char foreign_u8_max(void);\n"
        "extern long long direct_u8_max(void);\n"
        "extern long long indirect_u8_max(unsigned char (*)(void));\n"
        "__asm__(\".text\\n.p2align 4\\n.globl \" ABI_SYMBOL \"foreign_s16_min\\n\" ABI_SYMBOL \"foreign_s16_min:\\nmovabsq $0x5a96c3e17b428000, %rax\\nret\\n\");\n"
        "extern short foreign_s16_min(void);\n"
        "extern long long direct_s16_min(void);\n"
        "extern long long indirect_s16_min(short (*)(void));\n"
        "__asm__(\".text\\n.p2align 4\\n.globl \" ABI_SYMBOL \"foreign_s16_max\\n\" ABI_SYMBOL \"foreign_s16_max:\\nmovabsq $0x5a96c3e17b427fff, %rax\\nret\\n\");\n"
        "extern short foreign_s16_max(void);\n"
        "extern long long direct_s16_max(void);\n"
        "extern long long indirect_s16_max(short (*)(void));\n"
        "__asm__(\".text\\n.p2align 4\\n.globl \" ABI_SYMBOL \"foreign_u16_max\\n\" ABI_SYMBOL \"foreign_u16_max:\\nmovabsq $0x5a96c3e17b42ffff, %rax\\nret\\n\");\n"
        "extern unsigned short foreign_u16_max(void);\n"
        "extern long long direct_u16_max(void);\n"
        "extern long long indirect_u16_max(unsigned short (*)(void));\n"
        "__asm__(\".text\\n.p2align 4\\n.globl \" ABI_SYMBOL \"foreign_s32_min\\n\" ABI_SYMBOL \"foreign_s32_min:\\nmovabsq $0x5a96c3e180000000, %rax\\nret\\n\");\n"
        "extern int foreign_s32_min(void);\n"
        "extern long long direct_s32_min(void);\n"
        "extern long long indirect_s32_min(int (*)(void));\n"
        "__asm__(\".text\\n.p2align 4\\n.globl \" ABI_SYMBOL \"foreign_u32_max\\n\" ABI_SYMBOL \"foreign_u32_max:\\nmovabsq $0x5a96c3e1ffffffff, %rax\\nret\\n\");\n"
        "extern unsigned int foreign_u32_max(void);\n"
        "extern long long direct_u32_max(void);\n"
        "extern long long indirect_u32_max(unsigned int (*)(void));\n"
        "__asm__(\".text\\n.p2align 4\\n.globl \" ABI_SYMBOL \"foreign_u64_value\\n\" ABI_SYMBOL \"foreign_u64_value:\\nmovabsq $0x5a96c3e17b428d01, %rax\\nret\\n\");\n"
        "extern unsigned long long foreign_u64_value(void);\n"
        "extern long long direct_u64_value(void);\n"
        "extern long long indirect_u64_value(unsigned long long (*)(void));\n"), S8(
        "extern int direct_bool_branches(void);\n"
        "extern int indirect_bool_branches(_Bool (*)(void), _Bool (*)(void));\n"
        "int main(void)\n"
        "{\n"
        "    int bad = 0;\n"
        "    bad |= direct_bool_false() != 0;\n"
        "    bad |= indirect_bool_false(foreign_bool_false) != 0;\n"
        "    bad |= direct_bool_true() != 1;\n"
        "    bad |= indirect_bool_true(foreign_bool_true) != 1;\n"
        "    bad |= direct_s8_min() != -128;\n"
        "    bad |= indirect_s8_min(foreign_s8_min) != -128;\n"
        "    bad |= direct_s8_max() != 127;\n"
        "    bad |= indirect_s8_max(foreign_s8_max) != 127;\n"
        "    bad |= direct_u8_max() != 255;\n"
        "    bad |= indirect_u8_max(foreign_u8_max) != 255;\n"
        "    bad |= direct_s16_min() != -32768;\n"
        "    bad |= indirect_s16_min(foreign_s16_min) != -32768;\n"
        "    bad |= direct_s16_max() != 32767;\n"
        "    bad |= indirect_s16_max(foreign_s16_max) != 32767;\n"
        "    bad |= direct_u16_max() != 65535;\n"
        "    bad |= indirect_u16_max(foreign_u16_max) != 65535;\n"
        "    bad |= direct_s32_min() != (-2147483647LL - 1);\n"
        "    bad |= indirect_s32_min(foreign_s32_min) != (-2147483647LL - 1);\n"
        "    bad |= direct_u32_max() != 4294967295LL;\n"
        "    bad |= indirect_u32_max(foreign_u32_max) != 4294967295LL;\n"
        "    bad |= direct_u64_value() != 0x5a96c3e17b428d01LL;\n"
        "    bad |= indirect_u64_value(foreign_u64_value) != 0x5a96c3e17b428d01LL;\n"
        "    bad |= direct_bool_branches() != 0;\n"
        "    bad |= indirect_bool_branches(foreign_bool_false, foreign_bool_true) != 0;\n"
        "    return bad;\n"
        "}\n")};
    String8 host_source = string_join_arena(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_parts), false);
    String8 host_input = buster_test_temporary_path(arguments->arena, S8("buster-x64-narrow-results-host"), S8(".c"));
    String8 host_object = buster_test_temporary_path(arguments->arena, S8("buster-x64-narrow-results-host"), S8(".o"));
    bool host_written = file_write(host_input, BUSTER_SLICE_TO_BYTE_SLICE(host_source));
    BUSTER_TEST(arguments, host_written);
    ProcessSpawnOptions options = {.use_process_environment = true, .search_path = true, .new_process_group = true,
        .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)};
    String8 command[16];
    u32 count = 0;
    command[count++] = S8(BUSTER_HOST_C_COMPILER);
    if (S8(BUSTER_HOST_C_COMPILER_ARG1).length) { command[count++] = S8(BUSTER_HOST_C_COMPILER_ARG1); }
    command[count++] = S8("-O2");
    command[count++] = S8("-fno-lto");
    command[count++] = S8("-c");
    command[count++] = host_input;
    command[count++] = S8("-o");
    command[count++] = host_object;
    ProcessSpawnResult host_spawn = {0};
    if (host_written)
    {
        host_spawn = os_process_spawn((SliceString8){.pointer = command, .length = count}, (SliceString8){0}, (SliceString8){0}, options);
    }
    ProcessWaitResult host_wait = host_spawn.handle ? os_process_wait_deadline(arguments->arena, host_spawn, 30000000) : (ProcessWaitResult){0};
    bool host_compiled = host_spawn.handle && !host_wait.timed_out && !host_wait.capture_failed && host_wait.result == PROCESS_RESULT_SUCCESS;
    BUSTER_TEST_RAW(arguments, host_compiled, ((String8){.pointer = (char8*)host_wait.streams[STANDARD_STREAM_ERROR].pointer,
        .length = host_wait.streams[STANDARD_STREAM_ERROR].length}));
    bool reference_ok = false;
    if (written && host_compiled)
    {
        String8 reference = buster_test_temporary_path(arguments->arena, S8("buster-x64-narrow-results-reference"), S8(""));
        count = 0;
        command[count++] = S8(BUSTER_HOST_C_COMPILER);
        if (S8(BUSTER_HOST_C_COMPILER_ARG1).length) { command[count++] = S8(BUSTER_HOST_C_COMPILER_ARG1); }
        command[count++] = S8("-O2");
        command[count++] = S8("-fno-lto");
        command[count++] = S8("-fwrapv");
        command[count++] = S8("-fno-strict-aliasing");
        command[count++] = S8("-funsigned-char");
        command[count++] = input;
        command[count++] = host_object;
        command[count++] = S8("-o");
        command[count++] = reference;
        ProcessSpawnResult reference_spawn = os_process_spawn((SliceString8){.pointer = command, .length = count},
            (SliceString8){0}, (SliceString8){0}, options);
        ProcessWaitResult reference_wait = reference_spawn.handle ? os_process_wait_deadline(arguments->arena, reference_spawn, 30000000) : (ProcessWaitResult){0};
        reference_ok = reference_spawn.handle && !reference_wait.timed_out && !reference_wait.capture_failed &&
            reference_wait.result == PROCESS_RESULT_SUCCESS;
        BUSTER_TEST_RAW(arguments, reference_ok, ((String8){.pointer = (char8*)reference_wait.streams[STANDARD_STREAM_ERROR].pointer,
            .length = reference_wait.streams[STANDARD_STREAM_ERROR].length}));
        if (reference_ok)
        {
            reference_ok = compiler_driver_test_process_success(arguments->arena, reference);
            BUSTER_TEST(arguments, reference_ok);
        }
    }
#endif
    String8 targets[] = {S8("x86_64-linux"), S8("x86_64-macos"), S8("x86_64-windows"), S8("x86_64-android"), S8("x86_64-ios")};
    String8 modes[] = {S8("-fregister-allocator=fast"), S8("-fregister-allocator=quality")};
    String8 frontends[] = {S8("-fno-frontend-ssa"), S8("-ffrontend-ssa")};
    for (u32 target = 0; written && target < BUSTER_ARRAY_LENGTH(targets); target += 1)
    {
        for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
        {
            for (u32 frontend = 0; frontend < BUSTER_ARRAY_LENGTH(frontends); frontend += 1)
            {
                TemporalArena temporary = scratch_begin(&arguments->arena, 1);
                String8 object = buster_test_temporary_path(temporary.arena, S8("buster-x64-narrow-results"), S8(".o"));
                String8 compile_command[] = {S8("-c"), S8("-O0"), S8("-g0"), S8("-target"), targets[target], modes[mode], frontends[frontend],
                    S8("-fno-canonical-fast"), S8("-fverify-codegen"), input, S8("-o"), object};
                CompilerDriverInvocation invocation = compiler_driver_parse_arguments(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(compile_command));
                invocation.reject_machine_fallback = true;
                CompilerDriverResult compiled = compiler_driver_execute_invocation(temporary.arena, invocation);
                String8 context = string_format(temporary.arena, S8("narrow call results {S8} {S8} {S8}: {S8}"),
                    targets[target], modes[mode], frontends[frontend], compiled.diagnostic);
                BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object, context);
                BUSTER_TEST(arguments, compiled.codegen_statistics.function_count == 24 && compiled.codegen_statistics.fallback_function_count == 0);
#if defined(BUSTER_HOST_C_COMPILER) && BUSTER_CPU_ARCH_X86_64 && (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS
                if (reference_ok && compiled.error == COMPILER_DRIVER_ERROR_NONE && ((target == 0 && BUSTER_LINUX) || (target == 1 && BUSTER_MACOS)))
                {
                    String8 executable = buster_test_temporary_path(temporary.arena, S8("buster-x64-narrow-results-run"), S8(""));
                    count = 0;
                    command[count++] = S8(BUSTER_HOST_C_COMPILER);
                    if (S8(BUSTER_HOST_C_COMPILER_ARG1).length) { command[count++] = S8(BUSTER_HOST_C_COMPILER_ARG1); }
#if BUSTER_LINUX
                    command[count++] = S8("-no-pie");
#endif
                    command[count++] = object;
                    command[count++] = host_object;
                    command[count++] = S8("-o");
                    command[count++] = executable;
                    ProcessSpawnResult link_spawn = os_process_spawn((SliceString8){.pointer = command, .length = count},
                        (SliceString8){0}, (SliceString8){0}, options);
                    ProcessWaitResult link_wait = link_spawn.handle ? os_process_wait_deadline(temporary.arena, link_spawn, 30000000) : (ProcessWaitResult){0};
                    bool linked = link_spawn.handle && !link_wait.timed_out && !link_wait.capture_failed && link_wait.result == PROCESS_RESULT_SUCCESS;
                    BUSTER_TEST_RAW(arguments, linked, ((String8){.pointer = (char8*)link_wait.streams[STANDARD_STREAM_ERROR].pointer,
                        .length = link_wait.streams[STANDARD_STREAM_ERROR].length}));
                    if (linked) { BUSTER_TEST_RAW(arguments, compiler_driver_test_process_success(temporary.arena, executable), context); }
                }
#endif
                scratch_end(temporary);
            }
        }
    }
    return result;
}

