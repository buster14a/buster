// Included by driver_test.c. Every pass subset executes against fixed C
// answers on the native backend, and reaches all shared non-native consumers.
#include <buster/tests/compiler/codegen/ebpf_test_vm.h>
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_fast(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 default_command[] = {S8("source.c")};
    CompilerDriverInvocation default_invocation = compiler_driver_parse_arguments(arguments->arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(default_command));
    BUSTER_TEST(arguments, default_invocation.error == COMPILER_DRIVER_ERROR_NONE && default_invocation.fast_passes == IR_FAST_ALL);
    String8 disabled_command[] = {S8("-fcanonical-fast"), S8("-fno-canonical-fast"), S8("source.c")};
    CompilerDriverInvocation disabled_invocation = compiler_driver_parse_arguments(arguments->arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(disabled_command));
    BUSTER_TEST(arguments, disabled_invocation.error == COMPILER_DRIVER_ERROR_NONE && disabled_invocation.fast_passes == 0);
    String8 modes[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
    for (u32 backend = 0; backend < 7; backend += 1)
    {
        for (u32 mask = 0; mask <= IR_FAST_ALL; mask += 1)
        {
            TemporalArena temporary = arena_begin_temporal(arguments->arena);
            Arena* arena = temporary.arena;
            String8 path = buster_test_temporary_path(arena, S8("buster-canonical-fast"),
                backend < 4 && !BUSTER_ANDROID && !BUSTER_IOS ? S8(".exe") : S8(".artifact"));
            String8 command[16];
            u32 count = 0;
            command[count++] = S8("-nostdinc");
            command[count++] = S8("-o");
            command[count++] = path;
            command[count++] = backend < 4 ? S8("tests/basic_c_canonical_fast.c") : S8("tests/basic_c_canonical_fast_scalar.c");
            if (backend < 4)
            {
                command[count++] = string_format(arena, S8("-fregister-allocator={S8}"), modes[backend]);
#if BUSTER_ANDROID || BUSTER_IOS
                // Mobile tests run in an application process. They cannot
                // launch generated executables, but still validate every
                // native subset through machine selection and object writing.
                command[count++] = S8("-c");
#endif
            }
            else if (backend < 6)
            {
                command[count++] = S8("-target");
                command[count++] = backend == 4 ? S8("wasm64-unknown-freestanding") : S8("bpfel-unknown-linux");
            }
            else
            {
                command[count++] = S8("-emit-llvm");
                command[count++] = S8("-c");
            }
            command[count++] = S8("-fcanonical-fast");
            for (u32 pass = 0; pass < IR_FAST_PASS_COUNT; pass += 1)
            {
                if (!(mask & IR_FAST_PASS_BIT(pass))) command[count++] = string_format(arena, S8("-fno-canonical-fast-{S8}"), ir_fast_pass_name((IrFastPass)pass));
            }
            CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8){command, count});
            BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.fast_passes == mask);
            CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, invocation);
            if (compiled.error != COMPILER_DRIVER_ERROR_NONE) arguments->show(arguments, S8("FAST backend={u32} mask={u32}: {S8}\n"), backend, mask, compiled.diagnostic);
            BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
            if (compiled.error == COMPILER_DRIVER_ERROR_NONE)
            {
                if (backend < 4)
                {
                    BUSTER_TEST(arguments, !compiled.codegen_statistics.fallback_function_count);
#if !BUSTER_ANDROID && !BUSTER_IOS
                    String8 run[] = {path};
                    ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                                                               (ProcessSpawnOptions){.use_process_environment = 1});
                    BUSTER_TEST(arguments, spawn.handle != 0);
                    if (spawn.handle)
                    {
                        ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, 30000000);
                        BUSTER_TEST(arguments, !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS);
                    }
#else
                    ByteSlice object = file_read(arena, path, (FileReadOptions){0});
                    BUSTER_TEST(arguments, compiled.has_object && object.length >= 8);
#endif
                }
                else
                {
                    ByteSlice bytes = file_read(arena, path, (FileReadOptions){0});
                    BUSTER_TEST(arguments, bytes.length >= 8);
                    if (backend == 4) BUSTER_TEST(arguments, compiled.has_wasm64 && bytes.length >= 8 && memcmp(bytes.pointer, "\0asm\1\0\0\0", 8) == 0);
                    else if (backend == 5)
                    {
                        BUSTER_TEST(arguments, compiled.has_ebpf);
                        u64 values[] = {0, 1, 0xffffffffu, UINT64_MAX};
                        for (u32 input = 0; input < BUSTER_ARRAY_LENGTH(values); input += 1)
                        {
                            u64 actual = 0;
                            BUSTER_TEST(arguments, codegen_test_ebpf_execute(bytes, values[input], input & 1, &actual));
                            BUSTER_TEST(arguments, actual == values[input] + 7);
                        }
                    }
                    else BUSTER_TEST(arguments, bytes.length >= 4 && memcmp(bytes.pointer, "BC\xc0\xde", 4) == 0);
                }
            }
            scratch_end(temporary);
        }
    }
    String8 toggle[] = {S8("-fcanonical-fast"), S8("-fno-canonical-fast"), S8("-fcanonical-fast-fold"), S8("-ftime-canonical-fast"), S8("source.c")};
    CompilerDriverInvocation parsed = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(toggle));
    BUSTER_TEST(arguments, parsed.error == COMPILER_DRIVER_ERROR_NONE && parsed.fast_passes == IR_FAST_PASS_BIT(IR_FAST_FOLD) && parsed.measure_fast_passes);
    return result;
}
