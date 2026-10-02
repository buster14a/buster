#include <buster/tests/compiler/llvm/bitcode_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/llvm/bitcode_internal.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/os.h>
#include <buster/lib/file.h>
#include <buster/lib/hash.h>

// Integer wire coverage: llvm_bitcode_test_integer_encoding exhausts the
// scalar operand boundary; llvm_bitcode_test_integer reads complete serialized
// modules and llvm_bitcode_test_consumers keeps independent Clang execution.
// Scalar ABI coverage: llvm_bitcode_test_scalar_abi_wire pins target attributes;
// llvm_bitcode_test_scalar_abi_runtime exchanges original C with independent compilers.
BUSTER_GLOBAL_LOCAL bool llvm_bitcode_test_integer_operand_matches(u64 encoded, u64 bits, u32 width)
{
    // Inverse of LLVM's Signed VBRs, not a second implementation of the writer:
    // https://llvm.org/docs/BitCodeFormat.html#signed-vbrs
    // The reserved operand 1 denotes INT64_MIN before declared-width truncation.
    u64 decoded = encoded >> 1;
    if (encoded == 1)
    {
        decoded = UINT64_C(1) << 63;
    }
    else if (encoded & 1)
    {
        decoded = 0 - decoded;
    }
    u64 mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
    return (decoded & mask) == (bits & mask);
}

BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_integer_encoding(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // Exact wire values prevent a matching round-trip mistake at signed minima.
    u32 widths[] = {1, 8, 16, 32, 64};
    u64 expected[] = {3, 257, 65537, UINT64_C(4294967297), 1};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(widths); index += 1)
    {
        u64 sign = UINT64_C(1) << (widths[index] - 1);
        BUSTER_TEST(arguments, llvm_bitcode_test_integer_operand(sign, widths[index]) == expected[index]);
    }

    u32 exhaustive_count = 0;
    for (u32 width = 1; width <= 64; width += 1)
    {
        u64 sign = UINT64_C(1) << (width - 1);
        u64 mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
        if (width <= 16)
        {
            // All 131,070 patterns across widths 1..16, through the actual
            // operand encoder. Constant stack storage; no full-module emission.
            for (u64 bits = 0; bits <= mask; bits += 1)
            {
                u64 encoded = llvm_bitcode_test_integer_operand(bits, width);
                BUSTER_TEST(arguments, llvm_bitcode_test_integer_operand_matches(encoded, bits, width));
                exhaustive_count += 1;
            }
        }

        // Also exercise discarded high bits at the narrow widths, and every
        // sign boundary through i64. UINT64_MAX must truncate before encoding.
        u64 boundaries[] = {0, 1, sign - 1, sign, sign + 1, mask, UINT64_MAX};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(boundaries); index += 1)
        {
            u64 encoded = llvm_bitcode_test_integer_operand(boundaries[index], width);
            BUSTER_TEST(arguments, llvm_bitcode_test_integer_operand_matches(encoded, boundaries[index], width));
        }

        if (width < 64)
        {
            // Historical #222 mutation: operand 1 for a narrow sign bit must
            // be rejected by this same oracle. At i64 it is the positive control.
            BUSTER_TEST(arguments, !llvm_bitcode_test_integer_operand_matches(1, sign, width));
        }
    }
    BUSTER_TEST(arguments, exhaustive_count == 131070);
    BUSTER_TEST(arguments, llvm_bitcode_test_integer_operand_matches(1, UINT64_C(1) << 63, 64));
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_uefi_boundary(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    typedef struct UefiAbiTarget UefiAbiTarget;
    struct UefiAbiTarget
    {
        String8 triple;
        CpuArch architecture;
        OperatingSystem os;
    };
    UefiAbiTarget targets[] = {
        {S8("aarch64-unknown-linux-gnu"), CPU_ARCH_AARCH64, OPERATING_SYSTEM_LINUX},
        {S8("aarch64-linux-android"), CPU_ARCH_AARCH64, OPERATING_SYSTEM_ANDROID},
        {S8("aarch64-apple-macos"), CPU_ARCH_AARCH64, OPERATING_SYSTEM_MACOS},
        {S8("aarch64-apple-ios"), CPU_ARCH_AARCH64, OPERATING_SYSTEM_IOS},
        {S8("aarch64-pc-windows-msvc"), CPU_ARCH_AARCH64, OPERATING_SYSTEM_WINDOWS},
        {S8("aarch64-unknown-uefi"), CPU_ARCH_AARCH64, OPERATING_SYSTEM_UEFI},
        {S8("x86_64-unknown-uefi"), CPU_ARCH_X86_64, OPERATING_SYSTEM_UEFI},
    };
    String8 modes[] = {S8("fast"), S8("none"), S8("mir-stack"), S8("quality")};
    String8 frontends[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(targets); target_index += 1)
    {
        UefiAbiTarget target = targets[target_index];
        bool uefi = target.os == OPERATING_SYSTEM_UEFI;
        u32 mode_count = uefi ? BUSTER_ARRAY_LENGTH(modes) : 1;
        u32 frontend_count = uefi ? BUSTER_ARRAY_LENGTH(frontends) : 1;
        for (u32 mode = 0; mode < mode_count; mode += 1)
        {
            for (u32 frontend = 0; frontend < frontend_count; frontend += 1)
            {
                TemporalArena temporary = scratch_begin(&arguments->arena, 1);
                Arena* arena = temporary.arena;
                String8 output = buster_test_temporary_path(arena, S8("buster-uefi-abi"), S8(".o"));
                String8 command[] = {
                    S8("-c"), S8("-g0"), string_format(arena, S8("--target={S8}"), target.triple),
                    string_format(arena, S8("-fregister-allocator={S8}"), modes[mode]), frontends[frontend],
                    S8("-o"), output, uefi ? S8("tests/basic_c_uefi.c") : S8("tests/basic_c_aarch64_abi_contract.c"),
                };
                CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE);
                BUSTER_TEST(arguments, invocation.target.cpu_arch == target.architecture && invocation.target.os == target.os);
                BUSTER_TEST(arguments, target_uses_llp64_data_model(invocation.target) ==
                                      (target.os == OPERATING_SYSTEM_WINDOWS || target.architecture == CPU_ARCH_X86_64));
                BUSTER_TEST(arguments, target_uses_pe_unwind(invocation.target) == (uefi || target.os == OPERATING_SYSTEM_WINDOWS));
                CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, invocation);
                if (compiled.error != COMPILER_DRIVER_ERROR_NONE)
                {
                    arguments->show(arguments, S8("UEFI ABI control {S8}/{S8}/{S8}: {S8}\n"),
                                    target.triple, modes[mode], frontends[frontend], compiled.diagnostic);
                }
                BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object);
                scratch_end(temporary);
            }
        }
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        bool rejected = uefi && target.architecture == CPU_ARCH_AARCH64;
        String8 output = buster_test_temporary_path(arena, string_format(arena, S8("buster-uefi-bitcode-{u32}"), target_index), S8(".bc"));
        String8 command[] = {S8("-emit-llvm"), string_format(arena, S8("--target={S8}"), target.triple),
                             S8("-o"), output, S8("tests/basic_c_llvm_uefi_varargs.c")};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE);
        CompilerDriverResult emitted = compiler_driver_execute_invocation(arena, invocation);
        if (rejected)
        {
            BUSTER_TEST(arguments, emitted.error == COMPILER_DRIVER_ERROR_ARGUMENT);
            BUSTER_TEST(arguments, !emitted.has_llvm_bitcode && !emitted.llvm_bitcode.bytes.length && !emitted.has_object);
            FileMapRead absent = file_map_read(arena, output, (FileReadOptions){0});
            BUSTER_TEST(arguments, !absent.bytes.pointer);
            file_map_unmap(absent);
            String8 sentinel = S8("must not overwrite an existing output");
            String8 inputs[] = {S8("tests/basic_c_llvm_uefi_varargs.c"), S8("tests/basic_c_llvm_uefi_varargs.c")};
            for (u32 form = 0; form < 3; form += 1)
            {
                BUSTER_TEST(arguments, file_write(output, (ByteSlice){.pointer = (u8*)sentinel.pointer, .length = sentinel.length}));
                CompilerDriverInvocation direct = form == 0 ? invocation : (CompilerDriverInvocation){
                    .target = invocation.target, .action = COMPILER_DRIVER_ACTION_OBJECT, .emit_llvm_bitcode = true,
                    .input_paths = inputs, .input_count = form, .output_path = output,
                };
                CompilerDriverResult failure = compiler_driver_execute_invocation(arena, direct);
                BUSTER_TEST(arguments, failure.error == COMPILER_DRIVER_ERROR_ARGUMENT);
                BUSTER_TEST(arguments, !failure.has_llvm_bitcode && !failure.llvm_bitcode.bytes.length && !failure.has_object);
                BUSTER_TEST(arguments, string_equal(failure.diagnostic,
                    S8("AArch64 UEFI LLVM bitcode output is unsupported: native UEFI requires LP64/AAPCS64")));
                FileMapRead retained = file_map_read(arena, output, (FileReadOptions){0});
                BUSTER_TEST(arguments, retained.bytes.length == sentinel.length &&
                                      !memcmp(retained.bytes.pointer, sentinel.pointer, sentinel.length));
                file_map_unmap(retained);
            }
        }
        else
        {
            BUSTER_TEST(arguments, emitted.error == COMPILER_DRIVER_ERROR_NONE && emitted.has_llvm_bitcode && emitted.llvm_bitcode.success);
        }
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_consumers(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 compiler = executable_resolve_in_path(arguments->arena, S8("clang"));
    typedef struct LlvmBitcodeConsumerFixture
    {
        String8 source;
        String8 caller;
        bool both_optimizations;
        bool generated;
    } LlvmBitcodeConsumerFixture;
    LlvmBitcodeConsumerFixture fixtures[] = {
        {.source = S8("tests/basic_c_llvm_scalars.c")},
        {.source = S8("tests/basic_c_llvm_layout.c")},
#if !BUSTER_ANDROID && !BUSTER_IOS
        // Mobile app bundles contain tests/ inputs, not additional source fixtures.
        {.source = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_pointer_addend.c"),
         .caller = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_pointer_addend_caller.c")},
        {.source = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_pointer_table.c"),
         .caller = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_pointer_table_caller.c")},
        {.source = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_unprototyped_definition.c"), .both_optimizations = true},
        {.source = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_block_function_declaration.c"), .both_optimizations = true},
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
        {.source = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_unaligned_pointer.c"),
         .caller = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_unaligned_pointer_caller.c")},
#endif
#endif
        {.generated = true},
#if BUSTER_CPU_ARCH_X86_64
        {.source = S8("tests/basic_c_llvm_aggregate_abi_callee.c"), .caller = S8("tests/basic_c_llvm_aggregate_abi_caller.c")},
        {.source = S8("tests/basic_c_llvm_aggregate_abi_caller.c"), .caller = S8("tests/basic_c_llvm_aggregate_abi_callee.c")},
        {.source = S8("tests/basic_c_llvm_vector_abi.c"), .caller = S8("tests/basic_c_llvm_vector_abi_main.c")},
#if BUSTER_LINUX || BUSTER_WINDOWS
        {.source = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_varargs.c"),
         .caller = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_varargs_check.c"),
         .both_optimizations = true},
#endif
#endif
        {.source = S8("tests/basic_c_llvm_integer_boundary_values.c"), .caller = S8("tests/basic_c_llvm_integer_boundary_check.c")},
    };
    for (u32 fixture = 0; fixture < BUSTER_ARRAY_LENGTH(fixtures); fixture += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        String8 output = buster_test_temporary_path(arena, S8("buster-llvm-consumer"), S8(".bc"));
        bool bit_counts = fixtures[fixture].generated;
        String8 source = fixtures[fixture].source;
        String8 caller = fixtures[fixture].caller;
        if (bit_counts)
        {
            // The Android test app packages the test module, not arbitrary
            // source fixtures. Write both sides of this LLVM execution oracle
            // into the test's own temporary directory.
            String8 source_code = S8(
                "unsigned bit_counts32(unsigned value)\n"
                "{\n"
                "    unsigned count = (unsigned)__builtin_popcount(value);\n"
                "    if (value != 0)\n"
                "    {\n"
                "        count += (unsigned)__builtin_clz(value);\n"
                "        count += (unsigned)__builtin_ctz(value);\n"
                "    }\n"
                "    return count;\n"
                "}\n"
                "unsigned long long bit_counts64(unsigned long long value)\n"
                "{\n"
                "    unsigned long long count = (unsigned)__builtin_popcountll(value);\n"
                "    if (value != 0)\n"
                "    {\n"
                "        count += (unsigned)__builtin_clzll(value);\n"
                "        count += (unsigned)__builtin_ctzll(value);\n"
                "    }\n"
                "    return count;\n"
                "}\n"
                "int bit_first32(int value) { return __builtin_ffs(value); }\n"
                "int bit_first64(long long value) { return __builtin_ffsll(value); }\n");
            String8 caller_code = S8(
                "extern unsigned bit_counts32(unsigned);\n"
                "extern unsigned long long bit_counts64(unsigned long long);\n"
                "extern int bit_first32(int);\n"
                "extern int bit_first64(long long);\n"
                "int main(void)\n"
                "{\n"
                "    unsigned inputs32[] = {0, 1, 0x80000000u, 0xaaaaaaaau, 0xffffffffu};\n"
                "    unsigned expected32[] = {0, 32, 32, 17, 32};\n"
                "    unsigned long long inputs64[] = {0, 1, 0x8000000000000000ull, 0xaaaaaaaaaaaaaaaaull, 0xffffffffffffffffull};\n"
                "    unsigned long long expected64[] = {0, 64, 64, 33, 64};\n"
                "    int failures = 0;\n"
                "    for (unsigned index = 0; index < 5; index += 1)\n"
                "    {\n"
                "        failures += bit_counts32(inputs32[index]) != expected32[index];\n"
                "        failures += bit_counts64(inputs64[index]) != expected64[index];\n"
                "    }\n"
                "    failures += bit_first32(0) != 0;\n"
                "    failures += bit_first32(1) != 1;\n"
                "    failures += bit_first32(0x80000000u) != 32;\n"
                "    failures += bit_first64(0) != 0;\n"
                "    failures += bit_first64(1) != 1;\n"
                "    failures += bit_first64(0x8000000000000000ull) != 64;\n"
                "    return failures;\n"
                "}\n");
            source = buster_test_temporary_path(arena, S8("buster-llvm-bit-counts"), S8(".c"));
            caller = buster_test_temporary_path(arena, S8("buster-llvm-bit-counts-main"), S8(".c"));
            BUSTER_TEST(arguments, file_write(source, (ByteSlice){.pointer = (u8*)source_code.pointer, .length = source_code.length}));
            BUSTER_TEST(arguments, file_write(caller, (ByteSlice){.pointer = (u8*)caller_code.pointer, .length = caller_code.length}));
        }
        String8 command[5];
        u32 command_count = 0;
        command[command_count++] = S8("-emit-llvm");
        if (bit_counts && BUSTER_CPU_ARCH_X86_64)
        {
            command[command_count++] = S8("-mattr=+popcnt");
        }
        command[command_count++] = S8("-o");
        u32 output_index = command_count;
        command[command_count++] = output;
        command[command_count++] = source;
        CompilerDriverResult emitted = compiler_driver_execute_invocation(
            arena, compiler_driver_parse_arguments(arena, (SliceString8){.pointer = command, .length = command_count}));
        if (emitted.error != COMPILER_DRIVER_ERROR_NONE)
        {
            arguments->show(arguments, S8("LLVM fixture {S8}: {S8}\n"), source, emitted.diagnostic);
        }
        BUSTER_TEST(arguments, emitted.error == COMPILER_DRIVER_ERROR_NONE && emitted.has_llvm_bitcode && emitted.llvm_bitcode.success);
        if (fixtures[fixture].both_optimizations && emitted.error == COMPILER_DRIVER_ERROR_NONE)
        {
            command[output_index] = buster_test_temporary_path(arena, S8("buster-llvm-repeat"), S8(".bc"));
            CompilerDriverResult repeated = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8){.pointer = command, .length = command_count}));
            BUSTER_TEST(arguments, repeated.error == COMPILER_DRIVER_ERROR_NONE && llvm_bitcode_artifact_is_valid(repeated.llvm_bitcode));
            BUSTER_TEST(arguments, emitted.llvm_bitcode.bytes.length == repeated.llvm_bitcode.bytes.length &&
                                  !memcmp(emitted.llvm_bitcode.bytes.pointer, repeated.llvm_bitcode.bytes.pointer,
                                          emitted.llvm_bitcode.bytes.length));
        }
        if (compiler.length && emitted.error == COMPILER_DRIVER_ERROR_NONE)
        {
            u32 optimization_count = bit_counts || fixtures[fixture].both_optimizations ? 2 : 1;
            for (u32 optimization = 0; optimization < optimization_count; optimization += 1)
            {
                String8 executable = buster_test_temporary_path(arena, S8("buster-llvm-consumer"),
#if BUSTER_WINDOWS
                                                              S8(".exe"));
#else
                                                              S8(""));
#endif
                String8 compile[6];
                u64 compile_count = 0;
                compile[compile_count++] = compiler;
                compile[compile_count++] = optimization_count == 2 && optimization == 0 ? S8("-O0") : S8("-O2");
                compile[compile_count++] = output;
                if (caller.length)
                {
                    compile[compile_count++] = caller;
                }
                compile[compile_count++] = S8("-o");
                compile[compile_count++] = executable;
                ProcessSpawnResult spawned = os_process_spawn((SliceString8){.pointer = compile, .length = compile_count}, (SliceString8){0}, (SliceString8){0},
                    (ProcessSpawnOptions){.use_process_environment = true, .search_path = true,
                        .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
                BUSTER_TEST(arguments, spawned.handle != 0);
                if (spawned.handle)
                {
                    ProcessWaitResult compiled = os_process_wait_sync(arena, spawned);
                    if (compiled.result != PROCESS_RESULT_SUCCESS)
                    {
                        ByteSlice errors = compiled.streams[STANDARD_STREAM_ERROR];
                        arguments->show(arguments, S8("LLVM consumer rejected {S8} at {S8}: {S8}\n"), source, compile[1],
                                        (String8){.pointer = (char8*)errors.pointer, .length = errors.length});
                    }
                    BUSTER_TEST(arguments, compiled.result == PROCESS_RESULT_SUCCESS);
                    if (compiled.result == PROCESS_RESULT_SUCCESS)
                    {
                        String8 run[] = {executable};
                        ProcessSpawnResult child = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                            (ProcessSpawnOptions){.use_process_environment = true, .search_path = true});
                        BUSTER_TEST(arguments, child.handle != 0);
                        if (child.handle)
                        {
                            ProcessWaitResult run_result = os_process_wait_sync(arena, child);
                            if (run_result.result != PROCESS_RESULT_SUCCESS && optimization_count == 2)
                            {
                                arguments->show(arguments, S8("LLVM consumer {S8} at {S8}: checker exit {u32}\n"),
                                                source, compile[1], run_result.platform_status);
                            }
                            BUSTER_TEST(arguments, run_result.result == PROCESS_RESULT_SUCCESS);
                        }
                    }
                }
            }
        }
        scratch_end(temporary);
    }
    if (!compiler.length)
    {
        arguments->show(arguments, S8("LLVM consumer execution skipped: clang is unavailable on PATH\n"));
    }
    return result;
}

// Run the bitcode with a separately compiled observer. The long repeated
// scope also exhausts an ordinary thread stack if restores are omitted.
BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_stack_scopes(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 vla_source = S8(
        "// The independent caller observes only live VLA elements. Every scope exit\n"
        "// must preserve the outer VLA while discarding allocations made after it.\n"
        "extern int observe_bytes(volatile unsigned char* bytes, int count, int first, int last);\n"
        "\n"
        "int scoped_vlas(int n, int repetitions)\n"
        "{\n"
        "    volatile unsigned char outer[n + 1];\n"
        "    outer[0] = 71;\n"
        "    outer[n] = 83;\n"
        "    int result = 0;\n"
        "    for (int i = 0; i < repetitions; i += 1)\n"
        "    {\n"
        "        volatile unsigned char bytes[n];\n"
        "        bytes[0] = (unsigned char)i;\n"
        "        bytes[n - 1] = (unsigned char)(n + i);\n"
        "        result += observe_bytes(bytes, n, (unsigned char)i, (unsigned char)(n + i));\n"
        "        if (i % 3 == 0)\n"
        "        {\n"
        "            volatile unsigned char nested[n + 3];\n"
        "            nested[0] = (unsigned char)(i + 3);\n"
        "            nested[n + 2] = 47;\n"
        "            result += observe_bytes(nested, n + 3, (unsigned char)(i + 3), 47);\n"
        "        }\n"
        "    }\n"
        "    return result + outer[0] + outer[n];\n"
        "}\n"
        "\n"
        "int scoped_exits(int n, int mode)\n"
        "{\n"
        "    volatile unsigned char outer[n + 3];\n"
        "    outer[0] = 19;\n"
        "    outer[n + 2] = 23;\n"
        "    int result = 0;\n"
        "    for (int i = 0; i < 4; i += 1)\n"
        "    {\n"
        "        volatile unsigned char inner[n];\n"
        "        inner[0] = (unsigned char)i;\n"
        "        inner[n - 1] = 29;\n"
        "        if (mode == 1 && i == 0)\n"
        "        {\n"
        "            continue;\n"
        "        }\n"
        "        if (mode == 2 && i == 1)\n"
        "        {\n"
        "            break;\n"
        "        }\n"
        "        if (mode == 3 && i == 2)\n"
        "        {\n"
        "            goto outer_exit;\n"
        "        }\n"
        "        if (mode == 4 && i == 0)\n"
        "        {\n"
        "            return outer[0] + outer[n + 2];\n"
        "        }\n"
        "        result += observe_bytes(inner, n, (unsigned char)i, 29);\n"
        "    }\n"
        "outer_exit:\n"
        "    return result + outer[0] + outer[n + 2];\n"
        "}\n");
    String8 caller = S8(
        "// Compiled by Clang, independently of the Buster-produced bitcode.\n"
        "int scoped_vlas(int n, int repetitions);\n"
        "int scoped_exits(int n, int mode);\n"
        "\n"
        "int observe_bytes(volatile unsigned char* bytes, int count, int first, int last)\n"
        "{\n"
        "    return bytes[0] == first && bytes[count - 1] == last ? 1 : -10000;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    int failures = 0;\n"
        "    failures += scoped_vlas(3, 5) != 71 + 83 + 5 + 2;\n"
        "    // 1024 x 16 KiB exceeds an ordinary thread stack if no loop restore runs.\n"
        "    failures += scoped_vlas(16384, 1024) != 71 + 83 + 1024 + 342;\n"
        "    failures += scoped_exits(23, 0) != 42 + 4;\n"
        "    failures += scoped_exits(23, 1) != 42 + 3;\n"
        "    failures += scoped_exits(23, 2) != 42 + 1;\n"
        "    failures += scoped_exits(23, 3) != 42 + 2;\n"
        "    failures += scoped_exits(23, 4) != 42;\n"
        "    return failures;\n"
        "}\n");
    String8 compiler = executable_resolve_in_path(arguments->arena, S8("clang"));
    String8 frontends[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
    String8 optimizations[] = {S8("-O0"), S8("-O2")};
    for (u32 frontend = 0; frontend < BUSTER_ARRAY_LENGTH(frontends); frontend += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        String8 input = buster_test_temporary_path(arena, S8("buster-llvm-stack"), S8(".c"));
        String8 caller_input = buster_test_temporary_path(arena, S8("buster-llvm-stack-caller"), S8(".c"));
        String8 output = buster_test_temporary_path(arena, S8("buster-llvm-stack"), S8(".bc"));
        BUSTER_TEST(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(vla_source)));
        BUSTER_TEST(arguments, file_write(caller_input, BUSTER_SLICE_TO_BYTE_SLICE(caller)));
        String8 command[] = {S8("-emit-llvm"), frontends[frontend], S8("-o"), output,
                             input};
        CompilerDriverResult emitted = compiler_driver_execute_invocation(
            arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
        if (emitted.error != COMPILER_DRIVER_ERROR_NONE)
        {
            arguments->show(arguments, S8("LLVM stack fixture {S8}: {S8}\n"), frontends[frontend], emitted.diagnostic);
        }
        BUSTER_TEST(arguments, emitted.error == COMPILER_DRIVER_ERROR_NONE && emitted.has_llvm_bitcode && emitted.llvm_bitcode.success);
        if (compiler.length && emitted.error == COMPILER_DRIVER_ERROR_NONE)
        {
            for (u32 optimization = 0; optimization < BUSTER_ARRAY_LENGTH(optimizations); optimization += 1)
            {
                String8 executable = buster_test_temporary_path(arena, S8("buster-llvm-stack"),
#if BUSTER_WINDOWS
                                                               S8(".exe"));
#else
                                                               S8(""));
#endif
                String8 compile[] = {compiler, optimizations[optimization], output, caller_input, S8("-o"), executable};
                ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(compile), (SliceString8){0},
                    (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true, .search_path = true,
                        .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
                BUSTER_TEST(arguments, spawned.handle != 0);
                if (spawned.handle)
                {
                    ProcessWaitResult compiled = os_process_wait_sync(arena, spawned);
                    if (compiled.result != PROCESS_RESULT_SUCCESS)
                    {
                        ByteSlice errors = compiled.streams[STANDARD_STREAM_ERROR];
                        arguments->show(arguments, S8("LLVM stack consumer {S8} {S8}: {S8}\n"), frontends[frontend],
                                        optimizations[optimization], (String8){.pointer = (char8*)errors.pointer, .length = errors.length});
                    }
                    BUSTER_TEST(arguments, compiled.result == PROCESS_RESULT_SUCCESS);
                    if (compiled.result == PROCESS_RESULT_SUCCESS)
                    {
                        String8 run[] = {executable};
                        ProcessSpawnResult child = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0},
                            (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true, .search_path = true});
                        BUSTER_TEST(arguments, child.handle != 0);
                        if (child.handle)
                        {
                            bool success = os_process_wait_sync(arena, child).result == PROCESS_RESULT_SUCCESS;
                            if (!success)
                            {
                                arguments->show(arguments, S8("LLVM stack answers differ: {S8} {S8}\n"), frontends[frontend],
                                                optimizations[optimization]);
                            }
                            BUSTER_TEST(arguments, success);
                        }
                    }
                }
            }
        }
        scratch_end(temporary);
    }
    if (!compiler.length)
    {
        arguments->show(arguments, S8("LLVM stack consumer execution skipped: clang is unavailable on PATH\n"));
    }
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        String8 source = S8("int invalid_stack(int n) { volatile unsigned char bytes[n]; bytes[0] = 1;"
                            " __builtin_debugtrap(); return bytes[0]; }\n");
        String8 sentinel = S8("existing bitcode must survive a failed emission");
        String8 input = buster_test_temporary_path(arena, S8("buster-llvm-stack-invalid"), S8(".c"));
        String8 output = buster_test_temporary_path(arena, S8("buster-llvm-stack-invalid"), S8(".bc"));
        BUSTER_TEST(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(source)));
        BUSTER_TEST(arguments, file_write(output, BUSTER_SLICE_TO_BYTE_SLICE(sentinel)));
        String8 command[] = {S8("-emit-llvm"), S8("-o"), output, input};
        CompilerDriverResult rejected = compiler_driver_execute_invocation(
            arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
        BUSTER_TEST(arguments, rejected.error == COMPILER_DRIVER_ERROR_LLVM_BITCODE && !rejected.has_llvm_bitcode &&
                               !rejected.llvm_bitcode.bytes.length && rejected.llvm_bitcode.error.opcode == IR_OPCODE_DEBUG_TRAP);
        FileMapRead preserved = file_map_read(arena, output, (FileReadOptions){0});
        BUSTER_TEST(arguments, preserved.bytes.length == sentinel.length && preserved.bytes.pointer &&
                               !memcmp(preserved.bytes.pointer, sentinel.pointer, sentinel.length));
        file_map_unmap(preserved);
        scratch_end(temporary);
    }
    return result;
}

// Keep the expected answers in a separately compiled consumer: valid bitcode
// can still branch to the wrong case, including in the program's own checker.
BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_switches(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 compiler = executable_resolve_in_path(arguments->arena, S8("clang"));
    String8 frontends[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
    String8 optimizations[] = {S8("-O0"), S8("-O1"), S8("-O2"), S8("-O3")};
    String8 allocators[] = {S8("-fregister-allocator=none"), S8("-fregister-allocator=mir-stack"),
                           S8("-fregister-allocator=fast"), S8("-fregister-allocator=quality")};
    u32 configuration_count = BUSTER_ARRAY_LENGTH(frontends) * BUSTER_ARRAY_LENGTH(optimizations) * BUSTER_ARRAY_LENGTH(allocators);
    for (u32 configuration = 0; configuration < configuration_count; configuration += 1)
    {
        u32 allocator = configuration % BUSTER_ARRAY_LENGTH(allocators);
        u32 optimization = configuration / BUSTER_ARRAY_LENGTH(allocators) % BUSTER_ARRAY_LENGTH(optimizations);
        u32 frontend = configuration / (BUSTER_ARRAY_LENGTH(allocators) * BUSTER_ARRAY_LENGTH(optimizations));
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        String8 output = buster_test_temporary_path(arena, S8("buster-llvm-switch"), S8(".bc"));
        String8 command[] = {S8("-emit-llvm"), frontends[frontend], optimizations[optimization], allocators[allocator],
                             S8("-fno-target-local-promotion"), S8("-o"), output, S8("tests/basic_c_llvm_switch.c")};
        CompilerDriverResult emitted = compiler_driver_execute_invocation(
            arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
        if (emitted.error != COMPILER_DRIVER_ERROR_NONE)
        {
            arguments->show(arguments, S8("LLVM switch {S8} {S8} {S8}: {S8}\n"),
                            frontends[frontend], optimizations[optimization], allocators[allocator], emitted.diagnostic);
        }
        BUSTER_TEST(arguments, emitted.error == COMPILER_DRIVER_ERROR_NONE && emitted.has_llvm_bitcode && emitted.llvm_bitcode.success);
        if (compiler.length && emitted.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 executable = buster_test_temporary_path(arena, S8("buster-llvm-switch"),
#if BUSTER_WINDOWS
                                                          S8(".exe"));
#else
                                                          S8(""));
#endif
            String8 compile[] = {compiler, S8("-O0"), output, S8("tests/basic_c_llvm_switch_main.c"), S8("-o"), executable};
            ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(compile), (SliceString8){0}, (SliceString8){0},
                (ProcessSpawnOptions){.use_process_environment = true, .search_path = true,
                    .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
            BUSTER_TEST(arguments, spawned.handle != 0);
            if (spawned.handle)
            {
                ProcessWaitResult compiled = os_process_wait_sync(arena, spawned);
                if (compiled.result != PROCESS_RESULT_SUCCESS)
                {
                    ByteSlice errors = compiled.streams[STANDARD_STREAM_ERROR];
                    arguments->show(arguments, S8("LLVM switch consumer {S8} {S8} {S8}: {S8}\n"),
                                    frontends[frontend], optimizations[optimization], allocators[allocator],
                                    (String8){.pointer = (char8*)errors.pointer, .length = errors.length});
                }
                BUSTER_TEST(arguments, compiled.result == PROCESS_RESULT_SUCCESS);
                if (compiled.result == PROCESS_RESULT_SUCCESS)
                {
                    String8 run[] = {executable};
                    ProcessSpawnResult child = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                        (ProcessSpawnOptions){.use_process_environment = true, .search_path = true});
                    BUSTER_TEST(arguments, child.handle != 0);
                    if (child.handle)
                    {
                        bool success = os_process_wait_sync(arena, child).result == PROCESS_RESULT_SUCCESS;
                        if (!success)
                        {
                            arguments->show(arguments, S8("LLVM switch answers differ: {S8} {S8} {S8}\n"),
                                            frontends[frontend], optimizations[optimization], allocators[allocator]);
                        }
                        BUSTER_TEST(arguments, success);
                    }
                }
            }
        }
        scratch_end(temporary);
    }
    if (!compiler.length)
    {
        arguments->show(arguments, S8("LLVM switch execution skipped: clang is unavailable on PATH\n"));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_abi_diagnostics(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 targets[] = {S8("aarch64-unknown-linux-gnu"), S8("wasm64-unknown-freestanding"), S8("bpfel-unknown-linux"), S8("x86_64-unknown-linux-gnu")};
    String8 modes[] = {S8("-DABI_TEST_MODE=0"), S8("-DABI_TEST_MODE=1"), S8("-DABI_TEST_MODE=2"), S8("-DABI_TEST_MODE=3"),
                       S8("-DABI_TEST_MODE=4"), S8("-DABI_TEST_MODE=5"), S8("-DABI_TEST_MODE=6")};
    for (u32 target = 0; target < BUSTER_ARRAY_LENGTH(targets); target += 1)
    {
        for (u32 mode = target == 3 ? 3 : 0; mode < BUSTER_ARRAY_LENGTH(modes) - (target != 3); mode += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            Arena* arena = temporary.arena;
            String8 output = buster_test_temporary_path(arena, S8("buster-llvm-unsupported-abi"), S8(".bc"));
            String8 command[] = {S8("-emit-llvm"), S8("-target"), targets[target], modes[mode], S8("-o"), output,
                                 S8("tests/basic_c_llvm_abi_unsupported.c")};
            CompilerDriverResult emitted = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
            if (mode != 3)
            {
                // A SysV record containing only ignored padding has no
                // transport parts. The frontend's unsupported zero-part
                // signature gate now refuses it before LLVM emission.
                bool ignored_padding = target == 3 && mode == 6;
                CompilerDriverError expected = ignored_padding ? COMPILER_DRIVER_ERROR_ANALYSIS : COMPILER_DRIVER_ERROR_LLVM_BITCODE;
                BUSTER_TEST_RAW(arguments, emitted.error == expected, emitted.diagnostic);
                BUSTER_TEST(arguments, !emitted.has_llvm_bitcode && !emitted.llvm_bitcode.success && emitted.llvm_bitcode.bytes.length == 0);
                String8 diagnostic = ignored_padding
                    ? S8("C IR lowering does not yet support the parameter or return value types of function 'take_padding'")
                    : mode == 4 ? S8("aggregate variadic arguments") : S8("aggregate function ABI");
                BUSTER_TEST_RAW(arguments, string_first_sequence(emitted.diagnostic, diagnostic) != BUSTER_STRING_NO_MATCH, emitted.diagnostic);
            }
            else
            {
                BUSTER_TEST(arguments, emitted.error == COMPILER_DRIVER_ERROR_NONE);
                BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(emitted.llvm_bitcode));
            }
            scratch_end(temporary);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_variadic_diagnostics(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    (void)arguments;
#if BUSTER_CPU_ARCH_X86_64 && (BUSTER_LINUX || BUSTER_WINDOWS)
    String8 targets[] = {S8("aarch64-unknown-linux-gnu"), S8("x86_64-apple-macosx"),
#if BUSTER_WINDOWS
                         S8("x86_64-pc-windows-msvc")};
#else
                         S8("x86_64-unknown-linux-gnu")};
#endif
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(targets); index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        String8 output = buster_test_temporary_path(arena, S8("buster-llvm-variadic-negative"), S8(".bc"));
        String8 command[] = {S8("-emit-llvm"), S8("-target"), targets[index], S8("-o"), output,
                             S8("-DBUSTER_LLVM_VARIADIC_UNSUPPORTED_ARG=1"),
                             S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_varargs.c")};
        CompilerDriverResult emitted = compiler_driver_execute_invocation(
            arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
        BUSTER_TEST(arguments, emitted.error == COMPILER_DRIVER_ERROR_LLVM_BITCODE);
        BUSTER_TEST(arguments, !emitted.llvm_bitcode.success && !emitted.llvm_bitcode.bytes.length);
        String8 diagnostic = index == 2 ? S8("va_arg requires a promoted") : S8("va_list operations require x86-64 Linux SysV or Windows Win64");
        BUSTER_TEST(arguments, string_first_sequence(emitted.diagnostic, diagnostic) != BUSTER_STRING_NO_MATCH);
        // The refusal names the function it stopped in and points at it.
        String8 function = index == 2 ? S8(" (in function 'llvm_wide_arg')") : S8(" (in function 'llvm_sum_ints')");
        BUSTER_TEST_RAW(arguments,
                        string_starts_with_sequence(emitted.diagnostic, S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_varargs.c:")) &&
                            string_ends_with_sequence(emitted.diagnostic, function),
                        emitted.diagnostic);
        FileMapRead absent = file_map_read(arena, output, (FileReadOptions){0});
        BUSTER_TEST(arguments, !absent.bytes.pointer);
        file_map_unmap(absent);
        if (index == 2)
        {
            String8 sentinel = S8("retain previous bitcode output");
            BUSTER_TEST(arguments, file_write(output, (ByteSlice){.pointer = (u8*)sentinel.pointer, .length = sentinel.length}));
            CompilerDriverResult repeated = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
            BUSTER_TEST(arguments, repeated.error == COMPILER_DRIVER_ERROR_LLVM_BITCODE);
            FileMapRead retained = file_map_read(arena, output, (FileReadOptions){0});
            BUSTER_TEST(arguments, retained.bytes.length == sentinel.length &&
                                  !memcmp(retained.bytes.pointer, sentinel.pointer, sentinel.length));
            file_map_unmap(retained);
        }
        scratch_end(temporary);
    }
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_variadic_win64_object(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    (void)arguments;
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    String8 bitcode = buster_test_temporary_path(arena, S8("buster-llvm-win64-varargs"), S8(".bc"));
    String8 command[] = {S8("-emit-llvm"), S8("-target"), S8("x86_64-pc-windows-msvc"), S8("-o"), bitcode,
                         S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_varargs.c")};
    CompilerDriverResult emitted = compiler_driver_execute_invocation(
        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
    if (emitted.error != COMPILER_DRIVER_ERROR_NONE)
    {
        arguments->show(arguments, S8("Win64 variadic bitcode: {S8}\n"), emitted.diagnostic);
    }
    BUSTER_TEST(arguments, emitted.error == COMPILER_DRIVER_ERROR_NONE && llvm_bitcode_artifact_is_valid(emitted.llvm_bitcode));
    String8 compiler = executable_resolve_in_path(arena, S8("clang"));
    if (compiler.length && emitted.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 object = buster_test_temporary_path(arena, S8("buster-llvm-win64-varargs"), S8(".obj"));
        String8 compile[] = {compiler, S8("-target"), S8("x86_64-pc-windows-msvc"), S8("-O0"), S8("-c"), bitcode,
                             S8("-o"), object};
        ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(compile), (SliceString8){0}, (SliceString8){0},
            (ProcessSpawnOptions){.use_process_environment = true, .search_path = true,
                .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
        BUSTER_TEST(arguments, spawned.handle != 0);
        if (spawned.handle)
        {
            ProcessWaitResult compiled = os_process_wait_sync(arena, spawned);
            if (compiled.result != PROCESS_RESULT_SUCCESS)
            {
                ByteSlice errors = compiled.streams[STANDARD_STREAM_ERROR];
                arguments->show(arguments, S8("LLVM Win64 object consumer rejected variadic bitcode: {S8}\n"),
                                (String8){.pointer = (char8*)errors.pointer, .length = errors.length});
            }
            BUSTER_TEST(arguments, compiled.result == PROCESS_RESULT_SUCCESS);
        }
    }
    scratch_end(temporary);
#endif
    return result;
}

// Construct the four list operations without passing through the C frontend.
// The C fixture below separately checks that frontend lowering and an LLVM
// consumer agree about the public ABI.
BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_canonical_variadics(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    IrTypeId parameters[1] = {{.value = 1}};
    IrType types[5] = {
        {.id = {.value = 0}, .kind = IR_TYPE_VOID, .layout = {.resolved = true}},
        {.id = {.value = 1}, .kind = IR_TYPE_INTEGER, .layout = {.size = 4, .alignment = 4, .resolved = true},
         .bit_width = 32, .is_signed = true},
        {.id = {.value = 2}, .kind = IR_TYPE_VA_LIST, .layout = {.size = 24, .alignment = 8, .resolved = true}},
        {.id = {.value = 3}, .kind = IR_TYPE_POINTER, .element_type = {.value = 2},
         .layout = {.size = 8, .alignment = 8, .resolved = true}},
        {.id = {.value = 4}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 1},
         .parameter_types = parameters, .parameter_count = 1, .is_variadic = true,
         .calling_convention = IR_CALLING_CONVENTION_C, .layout = {.resolved = true}},
    };
    IrSymbol symbols[1] = {{.name = S8("direct_variadic"), .link_name = S8("direct_variadic"), .type = {.value = 4},
                            .kind = IR_SYMBOL_FUNCTION, .linkage = IR_LINKAGE_EXTERNAL, .is_definition = true}};
    IrValueId store_original[2] = {{.value = 0}, {.value = 2}};
    IrValueId original_place[1] = {{.value = 0}};
    IrValueId original_cursor[1] = {{.value = 3}};
    IrValueId store_copy[2] = {{.value = 1}, {.value = 4}};
    IrValueId copy_place[1] = {{.value = 1}};
    IrValueId copied_cursor[1] = {{.value = 5}};
    IrValueId returned[1] = {{.value = 6}};
    u64 parameter_index[1] = {0};
    IrInstruction instructions[13] = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(instructions); index += 1)
    {
        instructions[index].next = index + 1 < BUSTER_ARRAY_LENGTH(instructions) ? (IrInstructionId){.value = index + 1} :
                              IR_INSTRUCTION_ID_INVALID;
        instructions[index].result = IR_VALUE_ID_INVALID;
        instructions[index].symbol = IR_SYMBOL_ID_INVALID;
        instructions[index].canonical_local = IR_LOCAL_ID_INVALID;
        instructions[index].conversion_operation = IR_CONVERSION_COUNT;
        instructions[index].unary_operation = IR_UNARY_COUNT;
        instructions[index].binary_operation = IR_BINARY_COUNT;
    }
    instructions[0].opcode = IR_OPCODE_LOCAL;
    instructions[0].canonical_type.value = 2;
    instructions[0].canonical_local.value = 0;
    instructions[0].result.value = 0;
    instructions[1].opcode = IR_OPCODE_LOCAL;
    instructions[1].canonical_type.value = 2;
    instructions[1].canonical_local.value = 1;
    instructions[1].result.value = 1;
    instructions[2].opcode = IR_OPCODE_VA_START;
    instructions[2].canonical_type.value = 2;
    instructions[2].result.value = 2;
    instructions[3].opcode = IR_OPCODE_STORE;
    instructions[3].canonical_type.value = 0;
    instructions[3].operands = store_original;
    instructions[3].operand_count = 2;
    instructions[4].opcode = IR_OPCODE_ADDRESS_OF;
    instructions[4].canonical_type.value = 3;
    instructions[4].operands = original_place;
    instructions[4].operand_count = 1;
    instructions[4].result.value = 3;
    instructions[5].opcode = IR_OPCODE_VA_COPY;
    instructions[5].canonical_type.value = 2;
    instructions[5].operands = original_cursor;
    instructions[5].operand_count = 1;
    instructions[5].result.value = 4;
    instructions[6].opcode = IR_OPCODE_STORE;
    instructions[6].canonical_type.value = 0;
    instructions[6].operands = store_copy;
    instructions[6].operand_count = 2;
    instructions[7].opcode = IR_OPCODE_ADDRESS_OF;
    instructions[7].canonical_type.value = 3;
    instructions[7].operands = copy_place;
    instructions[7].operand_count = 1;
    instructions[7].result.value = 5;
    instructions[8].opcode = IR_OPCODE_VA_ARG;
    instructions[8].canonical_type.value = 1;
    instructions[8].operands = copied_cursor;
    instructions[8].operand_count = 1;
    instructions[8].result.value = 6;
    instructions[9].opcode = IR_OPCODE_VA_END;
    instructions[9].canonical_type.value = 0;
    instructions[9].operands = copied_cursor;
    instructions[9].operand_count = 1;
    instructions[10].opcode = IR_OPCODE_VA_END;
    instructions[10].canonical_type.value = 0;
    instructions[10].operands = original_cursor;
    instructions[10].operand_count = 1;
    instructions[11].opcode = IR_OPCODE_ARGUMENT;
    instructions[11].canonical_type.value = 1;
    instructions[11].immediates = parameter_index;
    instructions[11].immediate_count = 1;
    instructions[11].result.value = 7;
    instructions[12].opcode = IR_OPCODE_RETURN;
    instructions[12].canonical_type.value = 0;
    instructions[12].operands = returned;
    instructions[12].operand_count = 1;

    IrValue values[8] = {0};
    u32 definitions[8] = {0, 1, 2, 4, 5, 7, 8, 11};
    u32 value_types[8] = {2, 2, 2, 3, 2, 3, 1, 1};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(values); index += 1)
    {
        values[index].definition.value = definitions[index];
        values[index].canonical_type.value = value_types[index];
        values[index].category = index < 2 ? IR_VALUE_PLACE : IR_VALUE_VALUE;
    }
    IrBlock blocks[1] = {{.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = 12},
                          .terminated = true, .sealed = true}};
    IrValueId local_places[2] = {{.value = 0}, {.value = 1}};
    IrFunction functions[1] = {{.name = S8("direct_variadic"), .symbol = {.value = 0}, .canonical_type = {.value = 4},
                                .entry = {.value = 0}, .blocks = blocks, .instructions = instructions, .values = values,
                                .local_places = local_places, .block_count = 1, .instruction_count = 13, .value_count = 8,
                                .local_count = 2, .state = IR_FUNCTION_LOWERED}};
    IrModule modules[1] = {{.name = S8("canonical_variadic"), .functions = functions, .function_count = 1,
                            .lowered_function_count = 1}};
    IrProgram program = {.arena = arena, .modules = modules, .module_count = 1, .lowered_function_count = 1,
                         .types = {.types = types, .count = 5}, .symbols = {.symbols = symbols, .count = 1},
                         .disable_local_promotion = true};
    LlvmBitcodeOptions options = LLVM_BITCODE_OPTIONS_DEFAULT;
    options.target_triple = S8("x86_64-unknown-linux-gnu");
    options.data_layout = S8("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
    options.source_filename = S8("canonical_variadic.c");
    options.validate_ir = true;
    LlvmBitcodeArtifact first = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    LlvmBitcodeArtifact second = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    if (!llvm_bitcode_artifact_is_valid(first))
    {
        arguments->show(arguments, S8("canonical variadic: {S8} {S8} instruction={u32}\n"),
                        llvm_bitcode_error_code_name(first.error.code), first.error.message, first.error.instruction.value);
    }
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(first));
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(second));
    BUSTER_TEST(arguments, first.bytes.length == second.bytes.length &&
                          !memcmp(first.bytes.pointer, second.bytes.pointer, first.bytes.length));
    BUSTER_TEST(arguments, first.stats.defined_function_count == 1 && first.stats.function_count == 4);
    BUSTER_TEST(arguments, first.stats.instruction_count == 13);

    // Reuse the same canonical instructions with the Win64 public list layout.
    types[2].layout.size = 8;
    options.target_triple = S8("x86_64-pc-windows-msvc");
    options.data_layout = S8("e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
    first = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    second = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    if (!llvm_bitcode_artifact_is_valid(first))
    {
        arguments->show(arguments, S8("canonical Win64 variadic: {S8} {S8} instruction={u32}\n"),
                        llvm_bitcode_error_code_name(first.error.code), first.error.message, first.error.instruction.value);
    }
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(first));
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(second));
    BUSTER_TEST(arguments, first.bytes.length == second.bytes.length &&
                          !memcmp(first.bytes.pointer, second.bytes.pointer, first.bytes.length));
    BUSTER_TEST(arguments, first.stats.defined_function_count == 1 && first.stats.function_count == 4);
    BUSTER_TEST(arguments, first.stats.instruction_count == 13);
    return result;
}

/* One module holding a three-byte record and an `_Atomic` copy of it whose
   promoted size is `atomic_size`, emitted for its type table alone: the walk in
   llvm_bc_build_types visits every type in the program, so no global has to
   name them for the records to be written.

   `atomic_size` 4 is what the frontend builds -- an atomic type is padded up to
   the next power of two (#731) -- and 3 is the atomic-scalar shape, where the
   operand's size already covers the object. The two answer differently: the
   padded one needs a record of its own, the operand followed by a byte array
   (#767), and the unpadded one is its operand's type exactly. */
BUSTER_GLOBAL_LOCAL LlvmBitcodeArtifact llvm_bitcode_test_atomic_record(Arena* arena, u64 atomic_size, bool include_atomic)
{
    IrField* fields = arena_allocate(arena, IrField, 3);
    for (u32 index = 0; index < 3; index += 1)
    {
        fields[index] = (IrField){.type = {.value = 1}, .offset = index};
    }
    IrType* types = arena_allocate(arena, IrType, 4);
    types[0] = (IrType){
        .kind = IR_TYPE_VOID,
        .layout = {.resolved = true},
    };
    types[1] = (IrType){
        .id = {.value = 1},
        .kind = IR_TYPE_INTEGER,
        .layout = {.size = 1, .alignment = 1, .resolved = true},
        .bit_width = 8,
        .is_signed = true,
    };
    types[2] = (IrType){
        .id = {.value = 2},
        .kind = IR_TYPE_STRUCT,
        .layout = {.size = 3, .alignment = 1, .resolved = true},
        .fields = fields,
        .field_count = 3,
    };
    types[3] = (IrType){
        .id = {.value = 3},
        .unqualified_type = {.value = 2},
        .kind = IR_TYPE_STRUCT,
        .layout = {.size = atomic_size, .alignment = (u32)atomic_size, .resolved = true},
        .fields = fields,
        .field_count = 3,
        .is_atomic = true,
    };
    IrModule modules[1] = {
        {
            .name = S8("bitcode_atomic_test"),
        },
    };
    IrProgram program = {
        .arena = arena,
        .modules = modules,
        .types = {.types = types, .count = include_atomic ? 4 : 3},
        .module_count = 1,
    };
    LlvmBitcodeOptions options = LLVM_BITCODE_OPTIONS_DEFAULT;
    options.target_triple = S8("x86_64-unknown-linux-gnu");
    options.data_layout = S8("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
    options.source_filename = S8("bitcode_atomic_test.c");
    options.validate_ir = false;

    return llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
}

BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_stack_records(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    IrTypeId parameter_types[] = {{.value = 1}};
    IrType types[] = {
        {.kind = IR_TYPE_VOID, .layout = {.resolved = true}},
        {.id = {.value = 1}, .kind = IR_TYPE_INTEGER, .bit_width = 64, .layout = {.size = 8, .alignment = 8, .resolved = true}},
        {.id = {.value = 2}, .kind = IR_TYPE_POINTER, .element_type = {.value = 0},
         .layout = {.size = 8, .alignment = 8, .resolved = true}},
        {.id = {.value = 3}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 1},
         .parameter_types = parameter_types, .parameter_count = 1, .layout = {.resolved = true}},
    };
    IrSymbol symbols[] = {{.name = S8("stack_records"), .link_name = S8("stack_records"), .type = {.value = 3},
                           .kind = IR_SYMBOL_FUNCTION, .linkage = IR_LINKAGE_EXTERNAL, .is_definition = true}};
    IrValueId size_operand[] = {{.value = 0}};
    IrValueId first_checkpoint[] = {{.value = 3}};
    IrValueId second_checkpoint[] = {{.value = 4}};
    IrBlockId continuation[] = {{.value = 1}};
    u64 argument_index[] = {0};
    u64 alignment[] = {16};
    IrInstruction instructions[] = {
        {.opcode = IR_OPCODE_ARGUMENT, .canonical_type = {.value = 1}, .result = {.value = 0},
         .immediates = argument_index, .immediate_count = 1},
        {.opcode = IR_OPCODE_STACK_SAVE, .canonical_type = {.value = 2}, .result = {.value = 1}},
        {.opcode = IR_OPCODE_STACK_ALLOCATE, .canonical_type = {.value = 2}, .result = {.value = 2},
         .operands = size_operand, .operand_count = 1, .immediates = alignment, .immediate_count = 1},
        {.opcode = IR_OPCODE_BRANCH, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
         .targets = continuation, .target_count = 1},
        {.opcode = IR_OPCODE_STACK_RESTORE, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
         .operands = first_checkpoint, .operand_count = 1},
        {.opcode = IR_OPCODE_STACK_SAVE, .canonical_type = {.value = 2}, .result = {.value = 4}},
        {.opcode = IR_OPCODE_STACK_ALLOCATE, .canonical_type = {.value = 2}, .result = {.value = 5},
         .operands = size_operand, .operand_count = 1, .immediates = alignment, .immediate_count = 1},
        {.opcode = IR_OPCODE_STACK_RESTORE, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
         .operands = second_checkpoint, .operand_count = 1},
        {.opcode = IR_OPCODE_RETURN, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
         .operands = size_operand, .operand_count = 1},
    };
    IrValue values[6] = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(values); index += 1)
    {
        values[index] = (IrValue){.canonical_type = {.value = index == 0 ? 1 : 2}, .definition = IR_INSTRUCTION_ID_INVALID,
                                  .category = IR_VALUE_VALUE};
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(instructions); index += 1)
    {
        if (instructions[index].result.value < BUSTER_ARRAY_LENGTH(values))
        {
            values[instructions[index].result.value].definition = (IrInstructionId){.value = index};
        }
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(instructions); index += 1)
    {
        instructions[index].next = index != 3 && index + 1 < BUSTER_ARRAY_LENGTH(instructions) ?
                                   (IrInstructionId){.value = index + 1} : IR_INSTRUCTION_ID_INVALID;
    }
    IrIncoming incoming = {.predecessor = {.value = 0}, .value = {.value = 1}};
    IrBlockParameter parameter = {.first_incoming = &incoming, .last_incoming = &incoming, .canonical_type = {.value = 2},
                                  .value = {.value = 3}, .incoming_count = 1};
    IrPredecessor predecessor = {.block = {.value = 0}};
    IrBlock blocks[] = {
        {.first_instruction = {.value = 0}, .last_instruction = {.value = 3}, .terminated = true, .sealed = true},
        {.id = {.value = 1}, .first_instruction = {.value = 4}, .last_instruction = {.value = 8}, .terminated = true, .sealed = true,
         .first_predecessor = &predecessor, .last_predecessor = &predecessor, .predecessor_count = 1,
         .first_parameter = &parameter, .last_parameter = &parameter, .parameter_count = 1},
    };
    IrFunction functions[] = {{.name = S8("stack_records"), .symbol = {.value = 0}, .canonical_type = {.value = 3},
                               .entry = {.value = 0}, .blocks = blocks, .instructions = instructions, .values = values,
                               .block_count = BUSTER_ARRAY_LENGTH(blocks), .instruction_count = BUSTER_ARRAY_LENGTH(instructions),
                               .value_count = BUSTER_ARRAY_LENGTH(values), .state = IR_FUNCTION_LOWERED}};
    IrModule modules[] = {{.name = S8("stack_records"), .functions = functions, .function_count = 1, .lowered_function_count = 1}};
    IrProgram program = {.arena = arena, .modules = modules, .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)},
                         .symbols = {.symbols = symbols, .count = 1}, .module_count = 1, .lowered_function_count = 1};
    LlvmBitcodeOptions options = LLVM_BITCODE_OPTIONS_DEFAULT;
    options.target_triple = S8("x86_64-unknown-linux-gnu");
    options.data_layout = S8("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
    LlvmBitcodeArtifact first = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    LlvmBitcodeArtifact second = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    if (!llvm_bitcode_artifact_is_valid(first))
    {
        arguments->show(arguments, S8("LLVM stack records rejected: {S8} block={u32} instruction={u32}: {S8}\n"),
                        llvm_bitcode_error_code_name(first.error.code), first.error.block.value, first.error.instruction.value,
                        first.error.message);
    }
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(first));
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(second));
    BUSTER_TEST(arguments, first.stats.function_count == 3 && first.stats.defined_function_count == 1);
    BUSTER_TEST(arguments, first.bytes.length == second.bytes.length && first.bytes.length &&
                           !memcmp(first.bytes.pointer, second.bytes.pointer, first.bytes.length));

    instructions[7].operand_count = 0;
    LlvmBitcodeArtifact malformed = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    BUSTER_TEST(arguments, !llvm_bitcode_artifact_is_valid(malformed) && !malformed.bytes.length);
    BUSTER_TEST(arguments, malformed.error.code == LLVM_BITCODE_ERROR_IR_VALIDATION && malformed.error.instruction.value == 7);
    instructions[7].operand_count = 1;
    symbols[0].link_name = S8("llvm.stacksave");
    LlvmBitcodeArtifact collision = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    BUSTER_TEST(arguments, !llvm_bitcode_artifact_is_valid(collision) && !collision.bytes.length);
    BUSTER_TEST(arguments, collision.error.code == LLVM_BITCODE_ERROR_DUPLICATE_SYMBOL);
    return result;
}

// Independent, bounded reader for the unabbreviated records this writer emits.
// Decode the serialized value instead of duplicating the encoder's sign mapping.
typedef struct LlvmBitcodeTestReader
{
    ByteSlice bytes;
    u64 bit;
    bool failed;
} LlvmBitcodeTestReader;

BUSTER_GLOBAL_LOCAL u64 llvm_bitcode_test_bits(LlvmBitcodeTestReader* reader, u32 count)
{
    u64 result = 0;
    if (count > 64 || reader->bit > reader->bytes.length * 8 || count > reader->bytes.length * 8 - reader->bit)
    {
        reader->failed = true;
    }
    else
    {
        for (u32 index = 0; index < count; index += 1)
        {
            result |= (u64)((reader->bytes.pointer[reader->bit >> 3] >> (reader->bit & 7)) & 1) << index;
            reader->bit += 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 llvm_bitcode_test_vbr(LlvmBitcodeTestReader* reader, u32 width)
{
    u64 result = 0;
    u32 shift = 0;
    bool more = true;
    while (more && !reader->failed)
    {
        u64 word = llvm_bitcode_test_bits(reader, width);
        u64 payload = word & ((UINT64_C(1) << (width - 1)) - 1);
        more = (word >> (width - 1)) != 0;
        if (shift >= 64 || payload > (UINT64_MAX >> shift))
        {
            reader->failed = true;
        }
        else
        {
            result |= payload << shift;
            shift += width - 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool llvm_bitcode_test_integer(ByteSlice bytes, u64 expected, u32 width)
{
    LlvmBitcodeTestReader reader = {.bytes = bytes, .bit = 32};
    u32 code_widths[3] = {2};
    u64 blocks[3] = {0};
    u32 depth = 0;
    u64 integers[8] = {0};
    u32 integer_count = 0;
    u32 functions = 0;
    u32 returns = 0;
    bool matched = false;
    while (!reader.failed && reader.bit < bytes.length * 8)
    {
        u64 code = llvm_bitcode_test_bits(&reader, code_widths[depth]);
        if (code == 1) // ENTER_SUBBLOCK
        {
            u64 block = llvm_bitcode_test_vbr(&reader, 8);
            u64 code_width = llvm_bitcode_test_vbr(&reader, 4);
            reader.bit = (reader.bit + 31) & ~UINT64_C(31);
            u64 words = llvm_bitcode_test_bits(&reader, 32);
            if ((block == 8 || block == 11 || block == 12) && depth < 2 && code_width > 0 && code_width <= 32)
            {
                depth += 1;
                blocks[depth] = block;
                code_widths[depth] = (u32)code_width;
            }
            else if (reader.bit <= bytes.length * 8 && words <= (bytes.length * 8 - reader.bit) / 32)
            {
                reader.bit += words * 32;
            }
            else
            {
                reader.failed = true;
            }
        }
        else if (code == 0 && depth) // END_BLOCK
        {
            reader.bit = (reader.bit + 31) & ~UINT64_C(31);
            depth -= 1;
        }
        else if (code == 3) // UNABBREV_RECORD
        {
            u64 record = llvm_bitcode_test_vbr(&reader, 6);
            u64 count = llvm_bitcode_test_vbr(&reader, 6);
            if (blocks[depth] == 8 && record == 8) // MODULE_CODE_FUNCTION
            {
                functions += 1;
            }
            for (u64 index = 0; index < count && !reader.failed; index += 1)
            {
                u64 operand = llvm_bitcode_test_vbr(&reader, 6);
                if (blocks[depth] == 11 && record == 4 && count == 1) // CST_CODE_INTEGER
                {
                    if (integer_count < BUSTER_ARRAY_LENGTH(integers))
                    {
                        integers[integer_count++] = operand == 1 ? UINT64_C(1) << 63 : (operand & 1) ? 0 - (operand >> 1) : operand >> 1;
                    }
                    else
                    {
                        reader.failed = true;
                    }
                }
                else if (blocks[depth] == 11 && record != 1) // Only integer constants occur in this fixture.
                {
                    reader.failed = true;
                }
                else if (blocks[depth] == 12 && record == 10 && count == 1) // FUNC_CODE_INST_RET
                {
                    // The fixture has no parameters or value-producing function
                    // records. Follow its relative return reference so an ABI
                    // allocation-count constant cannot satisfy the comparison.
                    returns += 1;
                    if (operand > 0 && operand <= integer_count)
                    {
                        u64 decoded = integers[integer_count - (u32)operand];
                        u64 mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
                        matched = (decoded & mask) == (expected & mask);
                    }
                    else
                    {
                        reader.failed = true;
                    }
                }
                else if (blocks[depth] == 12 && (record != 1 || count != 1 || operand != 1)) // DECLAREBLOCKS
                {
                    reader.failed = true;
                }
            }
        }
        else
        {
            reader.failed = true;
        }
    }
    return !reader.failed && functions == 1 && returns == 1 && matched;
}


enum
{
    LLVM_SCALAR_ABI_S_EXT = 24,
    LLVM_SCALAR_ABI_Z_EXT = 34,
    LLVM_SCALAR_ABI_MAX_GROUPS = 96,
    LLVM_SCALAR_ABI_MAX_LISTS = 16,
    LLVM_SCALAR_ABI_MAX_FUNCTIONS = 16,
    LLVM_SCALAR_ABI_MAX_TYPES = 64,
    LLVM_SCALAR_ABI_MAX_OPERANDS = 24,
    LLVM_SCALAR_ABI_BUILD_TIMEOUT_US = 60000000,
    LLVM_SCALAR_ABI_RUN_TIMEOUT_US = 5000000,
};

typedef struct LlvmScalarAbiRecord LlvmScalarAbiRecord;
struct LlvmScalarAbiRecord
{
    u64 operands[LLVM_SCALAR_ABI_MAX_OPERANDS];
    u32 code;
    u32 count;
};

typedef struct LlvmScalarAbiGroup LlvmScalarAbiGroup;
struct LlvmScalarAbiGroup
{
    u64 mask;
    u64 alignment;
    u32 parameter;
};

typedef struct LlvmScalarAbiWire LlvmScalarAbiWire;
struct LlvmScalarAbiWire
{
    LlvmScalarAbiGroup groups[LLVM_SCALAR_ABI_MAX_GROUPS];
    LlvmScalarAbiRecord lists[LLVM_SCALAR_ABI_MAX_LISTS];
    LlvmScalarAbiRecord functions[LLVM_SCALAR_ABI_MAX_FUNCTIONS];
    LlvmScalarAbiRecord types[LLVM_SCALAR_ABI_MAX_TYPES];
    LlvmScalarAbiRecord calls[2];
    String8 names[LLVM_SCALAR_ABI_MAX_FUNCTIONS];
    u32 group_count;
    u32 list_count;
    u32 function_count;
    u32 type_count;
    u32 call_count;
};

// The independent reader follows LLVM's unabbreviated record framing. It does
// not call writer helpers or inspect a private emitter context.
BUSTER_GLOBAL_LOCAL bool llvm_bitcode_test_scalar_read(Arena* arena, ByteSlice bytes, LlvmScalarAbiWire* wire)
{
    LlvmBitcodeTestReader reader = {.bytes = bytes, .bit = 32};
    u32 widths[4] = {2};
    u32 blocks[4] = {0};
    u32 depth = 0;
    bool magic = bytes.length >= 4 && bytes.pointer && bytes.pointer[0] == 'B' && bytes.pointer[1] == 'C' &&
                 bytes.pointer[2] == 0xc0 && bytes.pointer[3] == 0xde;
    reader.failed = !magic;
    while (!reader.failed && reader.bit < bytes.length * 8)
    {
        u64 code = llvm_bitcode_test_bits(&reader, widths[depth]);
        if (code == 1)
        {
            u64 block = llvm_bitcode_test_vbr(&reader, 8);
            u64 width = llvm_bitcode_test_vbr(&reader, 4);
            reader.bit = (reader.bit + 31) & ~UINT64_C(31);
            u64 words = llvm_bitcode_test_bits(&reader, 32);
            bool selected = block == 8 || block == 9 || block == 10 || block == 12 || block == 14 || block == 17;
            if (reader.bit > bytes.length * 8 || words > (bytes.length * 8 - reader.bit) / 32 || !width || width > 32)
            {
                reader.failed = true;
            }
            else if (selected && depth + 1 < BUSTER_ARRAY_LENGTH(widths))
            {
                depth += 1;
                widths[depth] = (u32)width;
                blocks[depth] = (u32)block;
            }
            else if (selected)
            {
                reader.failed = true;
            }
            else
            {
                reader.bit += words * 32;
            }
        }
        else if (code == 0 && depth)
        {
            reader.bit = (reader.bit + 31) & ~UINT64_C(31);
            depth -= 1;
        }
        else if (code == 3)
        {
            LlvmScalarAbiRecord record = {.code = (u32)llvm_bitcode_test_vbr(&reader, 6)};
            u64 count = llvm_bitcode_test_vbr(&reader, 6);
            bool selected = blocks[depth] == 9 || blocks[depth] == 10 || blocks[depth] == 17 ||
                            (blocks[depth] == 8 && record.code == 8) || (blocks[depth] == 12 && record.code == 34) ||
                            (blocks[depth] == 14 && depth == 2 && record.code == 1);
            if (selected && count > BUSTER_ARRAY_LENGTH(record.operands))
            {
                reader.failed = true;
            }
            for (u64 index = 0; index < count && !reader.failed; index += 1)
            {
                u64 operand = llvm_bitcode_test_vbr(&reader, 6);
                if (index < BUSTER_ARRAY_LENGTH(record.operands)) record.operands[index] = operand;
            }
            record.count = (u32)count;
            if (!reader.failed && blocks[depth] == 10 && record.code == 3)
            {
                u64 id = record.operands[0];
                if (count < 4 || !id || id >= BUSTER_ARRAY_LENGTH(wire->groups) || wire->groups[id].mask)
                {
                    reader.failed = true;
                }
                else
                {
                    LlvmScalarAbiGroup* group = wire->groups + id;
                    group->parameter = (u32)record.operands[1];
                    u32 cursor = 2;
                    while (cursor < count && !reader.failed)
                    {
                        u64 tag = record.operands[cursor++];
                        u64 kind = cursor < count ? record.operands[cursor++] : UINT64_MAX;
                        if (kind >= 64 || (group->mask & (UINT64_C(1) << kind)))
                        {
                            reader.failed = true;
                        }
                        else
                        {
                            group->mask |= UINT64_C(1) << kind;
                            if (tag == 1 || tag == 6)
                            {
                                if (cursor >= count) reader.failed = true;
                                else if (tag == 1 && kind == 1) group->alignment = record.operands[cursor++];
                                else if (tag == 6 && (kind == 3 || kind == 29)) cursor += 1;
                                else reader.failed = true;
                            }
                            else if (tag != 0 || (kind != LLVM_SCALAR_ABI_S_EXT && kind != LLVM_SCALAR_ABI_Z_EXT))
                            {
                                reader.failed = true;
                            }
                        }
                    }
                    wire->group_count += 1;
                }
            }
            else if (!reader.failed && blocks[depth] == 9 && record.code == 2)
            {
                if (wire->list_count + 1 >= BUSTER_ARRAY_LENGTH(wire->lists)) reader.failed = true;
                else wire->lists[++wire->list_count] = record;
            }
            else if (!reader.failed && blocks[depth] == 8 && record.code == 8)
            {
                if (count < 5 || wire->function_count >= BUSTER_ARRAY_LENGTH(wire->functions)) reader.failed = true;
                else wire->functions[wire->function_count++] = record;
            }
            else if (!reader.failed && blocks[depth] == 17 && record.code != 1)
            {
                if (wire->type_count >= BUSTER_ARRAY_LENGTH(wire->types)) reader.failed = true;
                else wire->types[wire->type_count++] = record;
            }
            else if (!reader.failed && blocks[depth] == 12 && record.code == 34)
            {
                if (wire->call_count >= BUSTER_ARRAY_LENGTH(wire->calls)) reader.failed = true;
                else wire->calls[wire->call_count++] = record;
            }
            else if (!reader.failed && blocks[depth] == 14 && depth == 2 && record.code == 1)
            {
                u64 id = record.operands[0];
                if (count < 2 || id >= wire->function_count || wire->names[id].length) reader.failed = true;
                else
                {
                    char8* name = arena_allocate(arena, char8, count - 1);
                    for (u32 index = 1; index < count; index += 1) name[index - 1] = (char8)record.operands[index];
                    wire->names[id] = (String8){.pointer = name, .length = count - 1};
                }
            }
        }
        else
        {
            reader.failed = true;
        }
    }
    return !reader.failed && !depth;
}

BUSTER_GLOBAL_LOCAL u64 llvm_bitcode_test_scalar_mask(LlvmScalarAbiWire* wire, u64 list, u32 parameter)
{
    u64 result = 0;
    if (list <= wire->list_count)
    {
        LlvmScalarAbiRecord* record = wire->lists + list;
        for (u32 index = 0; index < record->count; index += 1)
        {
            u64 group = record->operands[index];
            if (!group || group >= BUSTER_ARRAY_LENGTH(wire->groups) || !wire->groups[group].mask)
            {
                result = UINT64_MAX;
                break;
            }
            if (wire->groups[group].parameter == parameter) result |= wire->groups[group].mask;
        }
    }
    else
    {
        result = UINT64_MAX;
    }
    return result;
}

typedef struct LlvmScalarAbiTarget LlvmScalarAbiTarget;
struct LlvmScalarAbiTarget
{
    String8 triple;
    Target target;
    IrCallingConvention calling_convention;
    u32 wire_calling_convention;
    bool narrow_extension;
    bool bool_extension;
};

// Fixed C scalar types, not coerced aggregate carriers, decide extension.
// LLVM 21.1.8: Targets/X86.cpp and Targets/AArch64.cpp; wire enum IDs are from
// llvm/include/llvm/Bitcode/LLVMBitCodes.h at llvmorg-21.1.8.
BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_scalar_abi_wire(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    LlvmScalarAbiTarget targets[] = {
        {S8("x86_64-unknown-linux-gnu"), {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX}, IR_CALLING_CONVENTION_C, 0, true, true},
        {S8("x86_64-pc-windows-msvc"), {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS}, IR_CALLING_CONVENTION_C, 0, false, true},
        {S8("aarch64-unknown-linux-gnu"), {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX}, IR_CALLING_CONVENTION_C, 0, false, false},
        {S8("arm64-apple-macosx"), {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS}, IR_CALLING_CONVENTION_C, 0, true, true},
        {S8("aarch64-pc-windows-msvc"), {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_WINDOWS}, IR_CALLING_CONVENTION_C, 0, false, false},
        {S8("x86_64-pc-windows-msvc"), {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS}, IR_CALLING_CONVENTION_SYSTEMV, 78, true, true},
        {S8("x86_64-unknown-linux-gnu"), {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX}, IR_CALLING_CONVENTION_WIN64, 79, false, true},
    };
    String8 names[] = {S8("wire_s8"), S8("wire_s16"), S8("wire_u8"), S8("wire_u16"), S8("wire_bool"), S8("wire_i32"), S8("wire_i64"),
                       S8("wire_probe"), S8("wire_bits1"), S8("wire_hidden"), S8("wire_coerced")};
    u32 widths[] = {8, 16, 8, 16, 1, 32, 64};
    for (u32 row = 0; row < BUSTER_ARRAY_LENGTH(targets); row += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        LlvmScalarAbiTarget target = targets[row];
        bool aggregates = target.target.cpu_arch == CPU_ARCH_X86_64;
        u32 function_count = aggregates ? 11 : 9;
        IrTypeId parameters[] = {{.value = 1}, {.value = 2}, {.value = 3}, {.value = 4}, {.value = 5}, {.value = 6}, {.value = 7}};
        IrTypeId probe_parameters[] = {{.value = 15}, {.value = 1}, {.value = 2}, {.value = 3}, {.value = 4}, {.value = 5}, {.value = 6}, {.value = 7}};
        IrTypeId bits_parameters[] = {{.value = 17}};
        IrTypeId hidden_parameters[] = {{.value = 1}, {.value = 19}};
        IrTypeId coerced_parameters[] = {{.value = 21}, {.value = 6}, {.value = 7}};
        IrField fields[] = {{.name = S8("a"), .type = {.value = 7}}, {.name = S8("b"), .type = {.value = 7}, .offset = 8},
                            {.name = S8("c"), .type = {.value = 7}, .offset = 16}};
        IrField small_field = {.name = S8("signed_byte"), .type = {.value = 1}};
        IrType types[23] = {0};
        types[0] = (IrType){.kind = IR_TYPE_VOID, .layout = {.resolved = true}};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(widths); index += 1)
        {
            u32 size = widths[index] == 1 ? 1 : widths[index] / 8;
            types[index + 1] = (IrType){.id = {.value = index + 1}, .kind = index == 4 ? IR_TYPE_BOOLEAN : IR_TYPE_INTEGER,
                .bit_width = widths[index], .is_signed = index == 0 || index == 1 || index == 5 || index == 6,
                .layout = {.size = size, .alignment = size, .resolved = true}};
            types[index + 8] = (IrType){.id = {.value = index + 8}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = index + 1},
                .parameter_types = parameters, .parameter_count = BUSTER_ARRAY_LENGTH(parameters), .is_variadic = true,
                .calling_convention = target.calling_convention, .layout = {.resolved = true}};
        }
        types[15] = (IrType){.id = {.value = 15}, .kind = IR_TYPE_POINTER, .element_type = {.value = 8},
                            .layout = {.size = 8, .alignment = 8, .resolved = true}};
        types[16] = (IrType){.id = {.value = 16}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 1},
            .parameter_types = probe_parameters, .parameter_count = BUSTER_ARRAY_LENGTH(probe_parameters),
            .calling_convention = target.calling_convention, .layout = {.resolved = true}};
        types[17] = (IrType){.id = {.value = 17}, .kind = IR_TYPE_INTEGER, .bit_width = 1,
                            .layout = {.size = 1, .alignment = 1, .resolved = true}};
        types[18] = (IrType){.id = {.value = 18}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 17},
            .parameter_types = bits_parameters, .parameter_count = 1,
            .calling_convention = target.calling_convention, .layout = {.resolved = true}};
        types[19] = (IrType){.id = {.value = 19}, .kind = IR_TYPE_STRUCT, .fields = fields, .field_count = BUSTER_ARRAY_LENGTH(fields),
                            .layout = {.size = 24, .alignment = 8, .resolved = true}};
        types[20] = (IrType){.id = {.value = 20}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 19},
            .parameter_types = hidden_parameters, .parameter_count = BUSTER_ARRAY_LENGTH(hidden_parameters),
            .calling_convention = target.calling_convention, .layout = {.resolved = true}};
        types[21] = (IrType){.id = {.value = 21}, .kind = IR_TYPE_STRUCT, .fields = &small_field, .field_count = 1,
                            .layout = {.size = 1, .alignment = 1, .resolved = true}};
        types[22] = (IrType){.id = {.value = 22}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 21},
            .parameter_types = coerced_parameters, .parameter_count = BUSTER_ARRAY_LENGTH(coerced_parameters),
            .calling_convention = target.calling_convention, .layout = {.resolved = true}};
        IrSymbol symbols[11] = {0};
        IrFunction functions[11] = {0};
        for (u32 index = 0; index < function_count; index += 1)
        {
            u32 type = index < 7 ? index + 8 : index == 7 ? 16 : index == 8 ? 18 : index == 9 ? 20 : 22;
            symbols[index] = (IrSymbol){.id = {.value = index}, .name = names[index], .link_name = names[index], .type = {.value = type},
                .kind = IR_SYMBOL_FUNCTION, .linkage = IR_LINKAGE_EXTERNAL, .is_definition = index == 7};
            functions[index] = (IrFunction){.id = {.value = index}, .name = names[index], .symbol = {.value = index},
                .canonical_type = {.value = type}, .state = index == 7 ? IR_FUNCTION_LOWERED : IR_FUNCTION_DECLARATION};
        }
        u64 parameter_indices[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        IrValueId direct[] = {{.value = 0}, {.value = 2}, {.value = 3}, {.value = 4}, {.value = 5}, {.value = 6},
                              {.value = 7}, {.value = 8}, {.value = 7}, {.value = 7}};
        IrValueId indirect[] = {{.value = 1}, {.value = 2}, {.value = 3}, {.value = 4}, {.value = 5}, {.value = 6},
                                {.value = 7}, {.value = 8}, {.value = 7}, {.value = 7}};
        IrValueId returned = {.value = 10};
        IrInstruction instructions[12] = {0};
        IrValue values[11] = {0};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(instructions); index += 1)
        {
            instructions[index].next = index + 1 < BUSTER_ARRAY_LENGTH(instructions) ? (IrInstructionId){.value = index + 1} : IR_INSTRUCTION_ID_INVALID;
            instructions[index].result = IR_VALUE_ID_INVALID;
            instructions[index].symbol = IR_SYMBOL_ID_INVALID;
            instructions[index].canonical_local = IR_LOCAL_ID_INVALID;
            instructions[index].conversion_operation = IR_CONVERSION_COUNT;
            instructions[index].unary_operation = IR_UNARY_COUNT;
            instructions[index].binary_operation = IR_BINARY_COUNT;
        }
        instructions[0].opcode = IR_OPCODE_FUNCTION;
        instructions[0].canonical_type.value = 8;
        instructions[0].symbol.value = 0;
        instructions[0].result.value = 0;
        values[0] = (IrValue){.canonical_type = {.value = 8}, .definition = {.value = 0}, .category = IR_VALUE_VALUE};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(probe_parameters); index += 1)
        {
            u32 instruction = index + 1;
            instructions[instruction].opcode = IR_OPCODE_ARGUMENT;
            instructions[instruction].canonical_type = probe_parameters[index];
            instructions[instruction].immediates = parameter_indices + index;
            instructions[instruction].immediate_count = 1;
            instructions[instruction].result.value = instruction;
            values[instruction] = (IrValue){.id = {.value = instruction}, .canonical_type = probe_parameters[index],
                .definition = {.value = instruction}, .category = IR_VALUE_VALUE};
        }
        for (u32 index = 9; index <= 10; index += 1)
        {
            instructions[index].opcode = IR_OPCODE_CALL;
            instructions[index].canonical_type.value = 1;
            instructions[index].operands = index == 9 ? direct : indirect;
            instructions[index].operand_count = BUSTER_ARRAY_LENGTH(direct);
            instructions[index].result.value = index;
            values[index] = (IrValue){.id = {.value = index}, .canonical_type = {.value = 1}, .definition = {.value = index}, .category = IR_VALUE_VALUE};
        }
        instructions[11].opcode = IR_OPCODE_RETURN;
        instructions[11].operands = &returned;
        instructions[11].operand_count = 1;
        IrBlock block = {.first_instruction = {.value = 0}, .last_instruction = {.value = 11}, .terminated = true, .sealed = true};
        functions[7].blocks = &block;
        functions[7].instructions = instructions;
        functions[7].values = values;
        functions[7].block_count = 1;
        functions[7].instruction_count = BUSTER_ARRAY_LENGTH(instructions);
        functions[7].value_count = BUSTER_ARRAY_LENGTH(values);
        IrModule module = {.name = S8("scalar_abi"), .functions = functions, .function_count = function_count, .lowered_function_count = 1};
        IrProgram program = {.arena = arena, .data_layout = target_data_layout(target.target), .modules = &module, .module_count = 1,
            .types = {.types = types, .count = aggregates ? BUSTER_ARRAY_LENGTH(types) : 19},
            .symbols = {.symbols = symbols, .count = function_count}, .lowered_function_count = 1};
        LlvmBitcodeOptions options = LLVM_BITCODE_OPTIONS_DEFAULT;
        options.target_triple = target.triple;
        LlvmBitcodeArtifact first = llvm_bitcode_emit_with_options(arena, &program, &module, 1, options);
        LlvmBitcodeArtifact second = llvm_bitcode_emit_with_options(arena, &program, &module, 1, options);
        String8 context = string_format(arena, S8("scalar ABI target={S8} cc={u32} error={S8}: {S8}"), target.triple,
            target.wire_calling_convention, llvm_bitcode_error_code_name(first.error.code), first.error.message);
        BUSTER_TEST_RAW(arguments, llvm_bitcode_artifact_is_valid(first) && llvm_bitcode_artifact_is_valid(second), context);
        BUSTER_TEST_RAW(arguments, first.bytes.length && first.bytes.length == second.bytes.length &&
            !memcmp(first.bytes.pointer, second.bytes.pointer, first.bytes.length), context);
        LlvmScalarAbiWire* wire = arena_allocate(arena, LlvmScalarAbiWire, 1);
        *wire = (LlvmScalarAbiWire){0};
        bool decoded = llvm_bitcode_artifact_is_valid(first) && llvm_bitcode_test_scalar_read(arena, first.bytes, wire);
        BUSTER_TEST_RAW(arguments, decoded && wire->function_count == function_count && wire->call_count == 2, context);
        u32 callee = UINT32_MAX;
        u32 seen = 0;
        if (decoded)
        {
            for (u32 function = 0; function < wire->function_count; function += 1)
            {
                u32 named = UINT32_MAX;
                for (u32 index = 0; index < function_count; index += 1)
                {
                    if (string_equal(wire->names[function], names[index])) named = index;
                }
                BUSTER_TEST_RAW(arguments, named < function_count, context);
                if (named < function_count)
                {
                    BUSTER_TEST_RAW(arguments, !(seen & ((u32)1 << named)), context);
                    seen |= (u32)1 << named;
                    LlvmScalarAbiRecord record = wire->functions[function];
                    BUSTER_TEST_RAW(arguments, record.operands[1] == target.wire_calling_convention && record.operands[2] == (named != 7), context);
                    u64 list = record.operands[4];
                    BUSTER_TEST_RAW(arguments, list <= wire->list_count, context);
                    if (list <= wire->list_count)
                    {
                        u32 maximum_parameter = named == 7 ? 8 : named <= 6 ? 7 : named == 8 ? 1 : 3;
                        for (u32 entry = 0; entry < wire->lists[list].count; entry += 1)
                        {
                            u64 group = wire->lists[list].operands[entry];
                            BUSTER_TEST_RAW(arguments, group && group < BUSTER_ARRAY_LENGTH(wire->groups) &&
                                wire->groups[group].parameter <= maximum_parameter, context);
                        }
                    }
                    if (named == 0) callee = function;
                    if (named <= 7)
                    {
                        for (u32 parameter = 0; parameter <= 9; parameter += 1)
                        {
                            u32 scalar = parameter == 0 ? (named < 7 ? named : 0) :
                                named == 7 ? (parameter >= 2 && parameter <= 8 ? parameter - 2 : 7) :
                                (parameter <= 7 ? parameter - 1 : 7);
                            u64 expected = scalar == 4 && target.bool_extension ? UINT64_C(1) << LLVM_SCALAR_ABI_Z_EXT :
                                scalar < 4 && target.narrow_extension ? UINT64_C(1) << (scalar < 2 ? LLVM_SCALAR_ABI_S_EXT : LLVM_SCALAR_ABI_Z_EXT) : 0;
                            String8 detail = string_format(arena, S8("{S8} function={S8} parameter={u32} expected={u64} actual={u64}"),
                                context, names[named], parameter, expected, llvm_bitcode_test_scalar_mask(wire, list, parameter));
                            BUSTER_TEST_RAW(arguments, llvm_bitcode_test_scalar_mask(wire, list, parameter) == expected, detail);
                        }
                        u64 type_id = record.operands[0];
                        bool scalar_type = type_id < wire->type_count && wire->types[type_id].code == 21;
                        BUSTER_TEST_RAW(arguments, scalar_type, context);
                        if (scalar_type)
                        {
                            LlvmScalarAbiRecord signature = wire->types[type_id];
                            u64 result_type = signature.operands[1];
                            u32 expected_width = widths[named < 7 ? named : 0];
                            BUSTER_TEST_RAW(arguments, result_type < wire->type_count && wire->types[result_type].code == 7 &&
                                wire->types[result_type].operands[0] == expected_width, context);
                            u32 first_scalar = named == 7 ? 3 : 2;
                            BUSTER_TEST_RAW(arguments, signature.count == first_scalar + BUSTER_ARRAY_LENGTH(widths) &&
                                signature.operands[0] == (named != 7), context);
                            for (u32 scalar = 0; scalar < BUSTER_ARRAY_LENGTH(widths); scalar += 1)
                            {
                                u64 scalar_type_id = signature.operands[first_scalar + scalar];
                                BUSTER_TEST_RAW(arguments, scalar_type_id < wire->type_count && wire->types[scalar_type_id].code == 7 &&
                                    wire->types[scalar_type_id].operands[0] == widths[scalar], context);
                            }
                        }
                    }
                    else if (named == 8)
                    {
                        // Win64 extends BOOLEAN i1, but not an ordinary INTEGER i1.
                        for (u32 parameter = 0; parameter <= 2; parameter += 1)
                        {
                            u64 expected = parameter < 2 && target.narrow_extension ? UINT64_C(1) << LLVM_SCALAR_ABI_Z_EXT : 0;
                            BUSTER_TEST_RAW(arguments, llvm_bitcode_test_scalar_mask(wire, list, parameter) == expected, context);
                        }
                    }
                    else
                    {
                        bool sysv = target.narrow_extension;
                        for (u32 parameter = 0; parameter <= 4; parameter += 1)
                        {
                            u64 expected = named == 9 && parameter == 1 ? (UINT64_C(1) << 29) | (UINT64_C(1) << 1) :
                                named == 9 && parameter == 2 && sysv ? UINT64_C(1) << LLVM_SCALAR_ABI_S_EXT :
                                named == 9 && parameter == 3 && sysv ? (UINT64_C(1) << 3) | (UINT64_C(1) << 1) : 0;
                            BUSTER_TEST_RAW(arguments, llvm_bitcode_test_scalar_mask(wire, list, parameter) == expected,
                                string_format(arena, S8("{S8} aggregate={S8} parameter={u32}"), context, names[named], parameter));
                        }
                        if (named == 9 && list <= wire->list_count)
                        {
                            for (u32 entry = 0; entry < wire->lists[list].count; entry += 1)
                            {
                                u64 group = wire->lists[list].operands[entry];
                                if (group && group < BUSTER_ARRAY_LENGTH(wire->groups) &&
                                    (wire->groups[group].parameter == 1 || (sysv && wire->groups[group].parameter == 3)))
                                {
                                    BUSTER_TEST_RAW(arguments, wire->groups[group].alignment == 8, context);
                                }
                            }
                        }
                    }
                }
            }
            BUSTER_TEST_RAW(arguments, seen == ((u32)1 << function_count) - 1, context);
            BUSTER_TEST_RAW(arguments, callee < wire->function_count, context);
            if (callee < wire->function_count)
            {
                BUSTER_TEST_RAW(arguments, wire->calls[0].operands[3] != wire->calls[1].operands[3], context);
                for (u32 call = 0; call < wire->call_count; call += 1)
                {
                    LlvmScalarAbiRecord record = wire->calls[call];
                    BUSTER_TEST_RAW(arguments, record.count == 13 && record.operands[0] == wire->functions[callee].operands[4] &&
                        record.operands[1] == (((u64)target.wire_calling_convention << 1) | UINT64_C(32768)) &&
                        record.operands[2] == wire->functions[callee].operands[0], context);
                    // Both anonymous arguments reuse the fixed i32 argument.
                    BUSTER_TEST_RAW(arguments, record.operands[11] == record.operands[9] && record.operands[12] == record.operands[9], context);
                }
            }
        }
        scratch_end(temporary);
    }
    return result;
}

#if BUSTER_LINUX && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
BUSTER_GLOBAL_LOCAL bool llvm_bitcode_test_scalar_process(UnitTestArguments* arguments, Arena* arena, SliceString8 command, String8 phase, u64 deadline, bool* admission)
{
    ProcessSpawnResult spawn = os_process_spawn(command, (SliceString8){0}, (SliceString8){0},
        (ProcessSpawnOptions){.use_process_environment = true, .search_path = true, .new_process_group = true,
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .capture_limits = {.per_stream = {0, BUSTER_KB(64), BUSTER_KB(64)}, .total = BUSTER_KB(128)},
            .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL});
    bool result = false;
    if (spawn.handle)
    {
        ProcessWaitResult waited = os_process_wait_deadline(arena, spawn, deadline);
        if (waited.process_tree_cleanup_failed || waited.process_group_reservation_retained || waited.process_group_ownership_lost)
        {
            *admission = false;
        }
        result = spawn.process_group && !spawn.error.v && spawn.failure == PROCESS_SPAWN_FAILURE_NONE &&
                 waited.result == PROCESS_RESULT_SUCCESS && !waited.platform_status && !waited.timed_out &&
                 !waited.termination_requested && !waited.forcibly_terminated && !waited.capture_limit_exceeded &&
                 !waited.output_truncated && !waited.capture_failed && !waited.process_tree_cleanup_failed &&
                 !waited.process_group_reservation_retained && !waited.process_group_ownership_lost &&
                 !waited.dropped_total && !waited.streamed_total && waited.captured_total == waited.observed_total;
        arguments->show(arguments,
            S8("LLVM_SCALAR_ABI_PROCESS phase={S8} argv0={S8} deadline_us={u64} spawn_error={u32} spawn_stage={u32} group={u32} "
               "result={u32} native={u32} timeout={u32} requested={u32} forced={u32} capture_limit={u32} truncated={u32} "
               "capture_failed={u32} cleanup_failed={u32} reservation_retained={u32} ownership_lost={u32} "
               "observed={u64} captured={u64} streamed={u64} dropped={u64}\n"),
            phase, command.pointer[0], deadline, spawn.error.v, (u32)spawn.failure, (u32)spawn.process_group,
            (u32)waited.result, waited.platform_status, (u32)waited.timed_out, (u32)waited.termination_requested,
            (u32)waited.forcibly_terminated, (u32)waited.capture_limit_exceeded, (u32)waited.output_truncated,
            (u32)waited.capture_failed, (u32)waited.process_tree_cleanup_failed, (u32)waited.process_group_reservation_retained,
            (u32)waited.process_group_ownership_lost, waited.observed_total, waited.captured_total, waited.streamed_total, waited.dropped_total);
        if (!result)
        {
            arguments->show(arguments, S8("LLVM_SCALAR_ABI_OUTPUT phase={S8} stdout={S8} stderr={S8}\n"), phase,
                (String8){.pointer = (char8*)waited.streams[STANDARD_STREAM_OUTPUT].pointer, .length = waited.streams[STANDARD_STREAM_OUTPUT].length},
                (String8){.pointer = (char8*)waited.streams[STANDARD_STREAM_ERROR].pointer, .length = waited.streams[STANDARD_STREAM_ERROR].length});
        }
    }
    else
    {
        *admission = false;
        arguments->show(arguments, S8("LLVM_SCALAR_ABI_SPAWN phase={S8} argv0={S8} error={u32} stage={u32}\n"),
            phase, command.pointer[0], spawn.error.v, (u32)spawn.failure);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool llvm_bitcode_test_scalar_readback(UnitTestArguments* arguments, Arena* arena, String8 path, ByteSlice expected, String8 phase)
{
    FileMapRead mapped = file_map_read(arena, path, (FileReadOptions){0});
    bool result = mapped.bytes.pointer && mapped.bytes.length == expected.length &&
                  (!expected.length || !memcmp(mapped.bytes.pointer, expected.pointer, expected.length));
    Sha256 hash;
    char8 source_hash[SHA256_HEX_CAPACITY];
    char8 disk_hash[SHA256_HEX_CAPACITY];
    sha256_init(&hash);
    sha256_add(&hash, expected.pointer, expected.length);
    sha256_finish_hex(&hash, source_hash);
    sha256_init(&hash);
    sha256_add(&hash, mapped.bytes.pointer, mapped.bytes.length);
    sha256_finish_hex(&hash, disk_hash);
    arguments->show(arguments, S8("LLVM_SCALAR_ABI_BYTES phase={S8} path={S8} expected_bytes={u64} disk_bytes={u64} original_sha256={S8} disk_sha256={S8}\n"),
        phase, path, expected.length, mapped.bytes.length,
        (String8){.pointer = source_hash, .length = SHA256_HEX_CAPACITY - 1}, (String8){.pointer = disk_hash, .length = SHA256_HEX_CAPACITY - 1});
    file_map_unmap(mapped);
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_scalar_abi_runtime(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_LINUX && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    Arena* arena = arguments->arena;
    String8 compilers[] = {executable_resolve_in_path(arena, S8("gcc")), executable_resolve_in_path(arena, S8("clang"))};
    String8 optimization[] = {S8("-O0"), S8("-O2")};
    String8 frontend[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
    BUSTER_TEST_RAW(arguments, compilers[0].length && compilers[1].length,
        S8("Linux scalar ABI oracle requires both GCC and Clang on PATH"));
    String8 root = buster_test_temporary_path(arena, S8("buster-llvm-scalar-abi"), S8(""));
    OsDirectoryCreateResult directory = {0};
    if (root.length) directory = os_make_directory_exclusive(root);
    BUSTER_TEST(arguments, directory.created && !directory.error.v);
    if (directory.created && compilers[0].length && compilers[1].length)
    {
        String8 declarations = S8(
        "int scalar_read_sc(signed char);\n"
        "int scalar_read_ss(short);\n"
        "int scalar_read_uc(unsigned char);\n"
        "int scalar_read_us(unsigned short);\n"
        "int scalar_read_bool(_Bool);\n"
        "signed char scalar_return_sc(int);\n"
        "short scalar_return_ss(int);\n"
        "unsigned char scalar_return_uc(unsigned);\n"
        "unsigned short scalar_return_us(unsigned);\n"
        "_Bool scalar_return_bool(int);\n"
        "int scalar_read_i32(int);\n"
        "long long scalar_read_i64(long long);\n"
        "int scalar_stack(int, int, int, int, int, int, int, int, signed char, short, unsigned char, unsigned short, _Bool);\n"
        "int scalar_promoted(signed char, int, ...);\n"
        "int scalar_indirect(int (*)(signed char), signed char);\n"
        "int scalar_probe(void);\n"
        "int scalar_call_sc(int);\n"
        "int scalar_call_ss(int);\n"
        "int scalar_call_uc(unsigned);\n"
        "int scalar_call_us(unsigned);\n"
        "int scalar_call_bool(int);\n"
        "#if LLVM_SCALAR_ABI_AGGREGATES\n"
        "struct scalar_large { long long first; long long second; long long third; };\n"
        "struct scalar_large scalar_hidden(signed char, unsigned short, _Bool);\n"
        "int scalar_byval(struct scalar_large, signed char, unsigned short);\n"
        "#endif\n");
        String8 common = string_format(arena, S8("#define LLVM_SCALAR_ABI_AGGREGATES {u32}\n{S8}"),
            (u32)BUSTER_CPU_ARCH_X86_64, declarations);
        String8 bodies[] = {
            S8(
                "\n"
                "int scalar_read_sc(signed char value) { return value; }\n"
                "int scalar_read_ss(short value) { return value; }\n"
                "int scalar_read_uc(unsigned char value) { return value; }\n"
                "int scalar_read_us(unsigned short value) { return value; }\n"
                "int scalar_read_bool(_Bool value) { return value; }\n"
                "signed char scalar_return_sc(int value) { return (signed char)value; }\n"
                "short scalar_return_ss(int value) { return (short)value; }\n"
                "unsigned char scalar_return_uc(unsigned value) { return (unsigned char)value; }\n"
                "unsigned short scalar_return_us(unsigned value) { return (unsigned short)value; }\n"
                "_Bool scalar_return_bool(int value) { return (_Bool)value; }\n"
                "int scalar_read_i32(int value) { return value; }\n"
                "long long scalar_read_i64(long long value) { return value; }\n"
                "int scalar_stack(int a, int b, int c, int d, int e, int f, int g, int h,\n"
                "                 signed char sc, short ss, unsigned char uc, unsigned short us, _Bool truth)\n"
                "{\n"
                "    return a + b + c + d + e + f + g + h + 3 * sc + 5 * ss + 7 * uc + 11 * us + 13 * truth;\n"
                "}\n"
                "#if LLVM_SCALAR_ABI_AGGREGATES\n"
                "struct scalar_large scalar_hidden(signed char sc, unsigned short us, _Bool truth)\n"
                "{\n"
                "    struct scalar_large value = {sc, us, truth};\n"
                "    return value;\n"
                "}\n"
                "int scalar_byval(struct scalar_large value, signed char sc, unsigned short us)\n"
                "{\n"
                "    return (int)(value.first + value.second + value.third) + 3 * sc + 5 * us;\n"
                "}\n"
                "#endif\n"),
            S8(
                "\n"
                "int scalar_call_sc(int value) { return scalar_read_sc((signed char)value); }\n"
                "int scalar_call_ss(int value) { return scalar_read_ss((short)value); }\n"
                "int scalar_call_uc(unsigned value) { return scalar_read_uc((unsigned char)value); }\n"
                "int scalar_call_us(unsigned value) { return scalar_read_us((unsigned short)value); }\n"
                "int scalar_call_bool(int value) { return scalar_read_bool((_Bool)value); }\n"
                "\n"
                "int scalar_indirect(int (*read)(signed char), signed char value)\n"
                "{\n"
                "    return read(value);\n"
                "}\n"
                "int scalar_probe(void)\n"
                "{\n"
                "    int failures = 0;\n"
                "    int signed_bytes[4] = {-128, -1, 0, 127};\n"
                "    int signed_shorts[4] = {-32768, -1, 0, 32767};\n"
                "    unsigned unsigned_bytes[4] = {0, 127, 128, 255};\n"
                "    unsigned unsigned_shorts[4] = {0, 32767, 32768, 65535};\n"
                "    int truth_inputs[4] = {-7, 0, 1, 55};\n"
                "    for (int index = 0; index < 4; index += 1)\n"
                "    {\n"
                "        int sc = signed_bytes[index];\n"
                "        int ss = signed_shorts[index];\n"
                "        unsigned uc = unsigned_bytes[index];\n"
                "        unsigned us = unsigned_shorts[index];\n"
                "        int truth = truth_inputs[index] != 0;\n"
                "        failures += scalar_read_sc((signed char)sc) != sc;\n"
                "        failures += scalar_read_ss((short)ss) != ss;\n"
                "        failures += scalar_read_uc((unsigned char)uc) != (int)uc;\n"
                "        failures += scalar_read_us((unsigned short)us) != (int)us;\n"
                "        failures += scalar_read_bool((_Bool)truth) != truth;\n"
                "        failures += scalar_indirect(scalar_read_sc, (signed char)sc) != sc;\n"
                "        failures += (int)scalar_return_sc(sc) != sc;\n"
                "        failures += (int)scalar_return_ss(ss) != ss;\n"
                "        failures += (unsigned)scalar_return_uc(uc) != uc;\n"
                "        failures += (unsigned)scalar_return_us(us) != us;\n"
                "        failures += (int)scalar_return_bool(truth_inputs[index]) != truth;\n"
                "        int expected = 36 + 3 * sc + 5 * ss + 7 * (int)uc + 11 * (int)us + 13 * truth;\n"
                "        failures += scalar_stack(1, 2, 3, 4, 5, 6, 7, 8, (signed char)sc, (short)ss,\n"
                "                                 (unsigned char)uc, (unsigned short)us, (_Bool)truth) != expected;\n"
                "        failures += scalar_promoted((signed char)sc, 23, (signed char)sc, (short)ss,\n"
                "                                    (unsigned char)uc, (unsigned short)us, (_Bool)truth) !=\n"
                "                    23 + 17 * sc + 3 * sc + 5 * ss + 7 * (int)uc + 11 * (int)us + 13 * truth;\n"
                "#if LLVM_SCALAR_ABI_AGGREGATES\n"
                "        struct scalar_large value = scalar_hidden((signed char)sc, (unsigned short)us, (_Bool)truth);\n"
                "        failures += value.first != sc || value.second != us || value.third != truth;\n"
                "        failures += scalar_byval(value, (signed char)sc, (unsigned short)us) != sc + (int)us + truth + 3 * sc + 5 * (int)us;\n"
                "#endif\n"
                "    }\n"
                "    failures += scalar_read_i32(-2147483647 - 1) != -2147483647 - 1;\n"
                "    failures += scalar_read_i64(-9223372036854775807LL - 1) != -9223372036854775807LL - 1;\n"
                "    return failures;\n"
                "}\n"),
            S8(
                "\n"
                "#include <stdio.h>\n"
                "#include <stdarg.h>\n"
                "int scalar_promoted(signed char fixed, int anchor, ...)\n"
                "{\n"
                "    va_list list;\n"
                "    va_start(list, anchor);\n"
                "    int sc = va_arg(list, int);\n"
                "    int ss = va_arg(list, int);\n"
                "    int uc = va_arg(list, int);\n"
                "    int us = va_arg(list, int);\n"
                "    int truth = va_arg(list, int);\n"
                "    va_end(list);\n"
                "    return anchor + 17 * fixed + 3 * sc + 5 * ss + 7 * uc + 11 * us + 13 * truth;\n"
                "}\n"
                "#define SCALAR_CHECK(label, expression, expected) do { \\\n"
                "    long long actual = (long long)(expression); long long wanted = (long long)(expected); \\\n"
                "    if (actual != wanted) { printf(\"LLVM_SCALAR_ABI_MISMATCH case=%d value=%s actual=%lld expected=%lld\\n\", \\\n"
                "        index, label, actual, wanted); failures += 1; } \\\n"
                "} while (0)\n"
                "int main(void)\n"
                "{\n"
                "    int failures = 0;\n"
                "    int signed_bytes[4] = {-128, -1, 0, 127};\n"
                "    int signed_shorts[4] = {-32768, -1, 0, 32767};\n"
                "    unsigned unsigned_bytes[4] = {0, 127, 128, 255};\n"
                "    unsigned unsigned_shorts[4] = {0, 32767, 32768, 65535};\n"
                "    int truth_inputs[4] = {-7, 0, 1, 55};\n"
                "    for (int index = 0; index < 4; index += 1)\n"
                "    {\n"
                "        int sc = signed_bytes[index];\n"
                "        int ss = signed_shorts[index];\n"
                "        unsigned uc = unsigned_bytes[index];\n"
                "        unsigned us = unsigned_shorts[index];\n"
                "        int truth = truth_inputs[index] != 0;\n"
                "        SCALAR_CHECK(\"signed-char\", scalar_read_sc((signed char)sc), sc);\n"
                "        SCALAR_CHECK(\"outgoing-signed-char\", scalar_call_sc(sc), sc);\n"
                "        SCALAR_CHECK(\"outgoing-short\", scalar_call_ss(ss), ss);\n"
                "        SCALAR_CHECK(\"outgoing-unsigned-char\", scalar_call_uc(uc), uc);\n"
                "        SCALAR_CHECK(\"outgoing-unsigned-short\", scalar_call_us(us), us);\n"
                "        SCALAR_CHECK(\"outgoing-bool\", scalar_call_bool(truth_inputs[index]), truth);\n"
                "        SCALAR_CHECK(\"short\", scalar_read_ss((short)ss), ss);\n"
                "        SCALAR_CHECK(\"unsigned-char\", scalar_read_uc((unsigned char)uc), uc);\n"
                "        SCALAR_CHECK(\"unsigned-short\", scalar_read_us((unsigned short)us), us);\n"
                "        SCALAR_CHECK(\"bool\", scalar_read_bool((_Bool)truth), truth);\n"
                "        SCALAR_CHECK(\"indirect\", scalar_indirect(scalar_read_sc, (signed char)sc), sc);\n"
                "        SCALAR_CHECK(\"return-signed-char\", scalar_return_sc(sc), sc);\n"
                "        SCALAR_CHECK(\"return-short\", scalar_return_ss(ss), ss);\n"
                "        SCALAR_CHECK(\"return-unsigned-char\", scalar_return_uc(uc), uc);\n"
                "        SCALAR_CHECK(\"return-unsigned-short\", scalar_return_us(us), us);\n"
                "        SCALAR_CHECK(\"return-bool\", scalar_return_bool(truth_inputs[index]), truth);\n"
                "        SCALAR_CHECK(\"stack\", scalar_stack(1, 2, 3, 4, 5, 6, 7, 8, (signed char)sc, (short)ss,\n"
                "            (unsigned char)uc, (unsigned short)us, (_Bool)truth),\n"
                "            36 + 3 * sc + 5 * ss + 7 * (int)uc + 11 * (int)us + 13 * truth);\n"
                "#if LLVM_SCALAR_ABI_AGGREGATES\n"
                "        struct scalar_large value = scalar_hidden((signed char)sc, (unsigned short)us, (_Bool)truth);\n"
                "        SCALAR_CHECK(\"hidden-first\", value.first, sc);\n"
                "        SCALAR_CHECK(\"hidden-second\", value.second, us);\n"
                "        SCALAR_CHECK(\"hidden-third\", value.third, truth);\n"
                "        SCALAR_CHECK(\"byval\", scalar_byval(value, (signed char)sc, (unsigned short)us),\n"
                "            sc + (int)us + truth + 3 * sc + 5 * (int)us);\n"
                "#endif\n"
                "    }\n"
                "    int index = 4;\n"
                "    SCALAR_CHECK(\"i32\", scalar_read_i32(-2147483647 - 1), -2147483647 - 1);\n"
                "    SCALAR_CHECK(\"i64\", scalar_read_i64(-9223372036854775807LL - 1), -9223372036854775807LL - 1);\n"
                "    SCALAR_CHECK(\"buster-caller\", scalar_probe(), 0);\n"
                "    return failures != 0;\n"
                "}\n"),
        };
        String8 units[] = {S8("callee"), S8("caller"), S8("checker")};
        String8 paths[3] = {{0}};
        String8 originals[3] = {{0}};
        String8 host_objects[3] = {{0}};
        bool sources_ready = true;
        bool admission = true;
        for (u32 unit = 0; unit < BUSTER_ARRAY_LENGTH(units); unit += 1)
        {
            paths[unit] = string_format(arena, S8("{S8}/{S8}.c"), root, units[unit]);
            host_objects[unit] = string_format(arena, S8("{S8}/host-{S8}.o"), root, units[unit]);
            originals[unit] = string_format(arena, S8("{S8}{S8}"), common, bodies[unit]);
            bool written = file_write(paths[unit], BUSTER_SLICE_TO_BYTE_SLICE(originals[unit]));
            BUSTER_TEST(arguments, written);
            bool retained = written && llvm_bitcode_test_scalar_readback(arguments, arena, paths[unit],
                BUSTER_SLICE_TO_BYTE_SLICE(originals[unit]), S8("original-source"));
            BUSTER_TEST(arguments, retained);
            sources_ready &= retained;
        }
        bool references_ready = sources_ready;
        for (u32 compiler = 0; compiler < BUSTER_ARRAY_LENGTH(compilers); compiler += 1)
        {
            for (u32 option = 0; option < BUSTER_ARRAY_LENGTH(optimization); option += 1)
            {
                String8 phase = string_format(arena, S8("original-reference compiler={u32} optimization={S8}"), compiler, optimization[option]);
                String8 executable = string_format(arena, S8("{S8}/reference-{u32}-{u32}"), root, compiler, option);
                String8 command[] = {compilers[compiler], S8("-std=gnu17"), S8("-g0"), S8("-fwrapv"), S8("-fno-strict-aliasing"),
                    S8("-funsigned-char"), S8("-fno-pie"), S8("-no-pie"), optimization[option],
                    paths[0], paths[1], paths[2], S8("-o"), executable};
                bool compiled = sources_ready && admission && llvm_bitcode_test_scalar_process(arguments, arena,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(command), phase, LLVM_SCALAR_ABI_BUILD_TIMEOUT_US, &admission);
                BUSTER_TEST_RAW(arguments, compiled, phase);
                String8 run[] = {executable};
                bool passed = compiled && admission && llvm_bitcode_test_scalar_process(arguments, arena,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(run), phase, LLVM_SCALAR_ABI_RUN_TIMEOUT_US, &admission);
                BUSTER_TEST_RAW(arguments, passed, phase);
                references_ready &= passed;
            }
        }
        bool host_ready = references_ready;
        for (u32 unit = 0; unit < BUSTER_ARRAY_LENGTH(units); unit += 1)
        {
            String8 command[] = {compilers[1], S8("-std=gnu17"), S8("-O2"), S8("-g0"), S8("-fwrapv"),
                S8("-fno-strict-aliasing"), S8("-funsigned-char"), S8("-fno-pie"), S8("-c"), paths[unit], S8("-o"), host_objects[unit]};
            String8 phase = string_format(arena, S8("independent-clang-O2 unit={S8}"), units[unit]);
            bool compiled = references_ready && admission && llvm_bitcode_test_scalar_process(arguments, arena,
                (SliceString8)BUSTER_ARRAY_TO_SLICE(command), phase, LLVM_SCALAR_ABI_BUILD_TIMEOUT_US, &admission);
            BUSTER_TEST_RAW(arguments, compiled, phase);
            host_ready &= compiled;
        }
        for (u32 unit = 0; unit < 2; unit += 1)
        {
            for (u32 form = 0; form < BUSTER_ARRAY_LENGTH(frontend); form += 1)
            {
                TemporalArena temporary = scratch_begin(&arena, 1);
                Arena* scratch = temporary.arena;
                String8 output = string_format(scratch, S8("{S8}/{S8}-{u32}.bc"), root, units[unit], form);
                String8 repeat = string_format(scratch, S8("{S8}/{S8}-{u32}-repeat.bc"), root, units[unit], form);
                String8 target =
#if BUSTER_CPU_ARCH_X86_64
                    S8("--target=x86_64-linux");
#else
                    S8("--target=aarch64-linux");
#endif
                String8 command[] = {S8("-emit-llvm"), S8("-std=gnu17"), S8("-g0"), S8("-fwrapv"), S8("-fno-strict-aliasing"),
                    S8("-funsigned-char"), S8("-fno-pie"), target, frontend[form], S8("-o"), output, paths[unit]};
                CompilerDriverResult emitted = {0};
                CompilerDriverResult repeated = {0};
                bool valid = false;
                if (host_ready && admission)
                {
                    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(scratch, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                    BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.target.os == OPERATING_SYSTEM_LINUX &&
                        invocation.target.cpu_arch == target_native.cpu_arch);
                    emitted = compiler_driver_execute_invocation(scratch, invocation);
                    command[10] = repeat;
                    repeated = compiler_driver_execute_invocation(scratch,
                        compiler_driver_parse_arguments(scratch, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                    valid = emitted.error == COMPILER_DRIVER_ERROR_NONE && repeated.error == COMPILER_DRIVER_ERROR_NONE &&
                        emitted.has_llvm_bitcode && repeated.has_llvm_bitcode &&
                        llvm_bitcode_artifact_is_valid(emitted.llvm_bitcode) && llvm_bitcode_artifact_is_valid(repeated.llvm_bitcode);
                }
                String8 phase = string_format(scratch, S8("buster unit={S8} frontend={S8} error={S8}"), units[unit], frontend[form], emitted.diagnostic);
                BUSTER_TEST_RAW(arguments, valid, phase);
                if (valid)
                {
                    BUSTER_TEST_RAW(arguments, emitted.llvm_bitcode.bytes.length == repeated.llvm_bitcode.bytes.length &&
                        !memcmp(emitted.llvm_bitcode.bytes.pointer, repeated.llvm_bitcode.bytes.pointer, emitted.llvm_bitcode.bytes.length), phase);
                    bool first_retained = llvm_bitcode_test_scalar_readback(arguments, scratch, output, emitted.llvm_bitcode.bytes, phase);
                    bool second_retained = llvm_bitcode_test_scalar_readback(arguments, scratch, repeat, repeated.llvm_bitcode.bytes, phase);
                    BUSTER_TEST_RAW(arguments, first_retained && second_retained, phase);
                    for (u32 option = 0; option < BUSTER_ARRAY_LENGTH(optimization); option += 1)
                    {
                        String8 object = string_format(scratch, S8("{S8}/{S8}-{u32}-{u32}.o"), root, units[unit], form, option);
                        String8 executable = string_format(scratch, S8("{S8}/mixed-{S8}-{u32}-{u32}"), root, units[unit], form, option);
                        String8 detail = string_format(scratch, S8("{S8} consumer={S8}"), phase, optimization[option]);
                        // C transport semantics are already encoded in the IR.
                        // The consumer selects optimization and non-PIE object code.
                        String8 consume[] = {compilers[1], optimization[option], S8("-g0"), S8("-fno-pie"), S8("-c"), output, S8("-o"), object};
                        bool consumed = first_retained && second_retained && admission && llvm_bitcode_test_scalar_process(arguments, scratch,
                            (SliceString8)BUSTER_ARRAY_TO_SLICE(consume), detail, LLVM_SCALAR_ABI_BUILD_TIMEOUT_US, &admission);
                        BUSTER_TEST_RAW(arguments, consumed, detail);
                        String8 link[] = {compilers[1], S8("-no-pie"), object, host_objects[1 - unit], host_objects[2], S8("-o"), executable};
                        bool linked = consumed && admission && llvm_bitcode_test_scalar_process(arguments, scratch,
                            (SliceString8)BUSTER_ARRAY_TO_SLICE(link), detail, LLVM_SCALAR_ABI_BUILD_TIMEOUT_US, &admission);
                        BUSTER_TEST_RAW(arguments, linked, detail);
                        String8 run[] = {executable};
                        bool passed = linked && admission && llvm_bitcode_test_scalar_process(arguments, scratch,
                            (SliceString8)BUSTER_ARRAY_TO_SLICE(run), detail, LLVM_SCALAR_ABI_RUN_TIMEOUT_US, &admission);
                        BUSTER_TEST_RAW(arguments, passed, detail);
                    }
                }
                scratch_end(temporary);
            }
        }
        for (u32 unit = 0; unit < BUSTER_ARRAY_LENGTH(units); unit += 1)
        {
            BUSTER_TEST(arguments, llvm_bitcode_test_scalar_readback(arguments, arena, paths[unit],
                BUSTER_SLICE_TO_BYTE_SLICE(originals[unit]), S8("source-after-consumers")));
        }
    }
    if (directory.created)
    {
        BUSTER_TEST(arguments, os_directory_delete(root));
    }
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_relocated_globals(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    IrType types[5] = {0};
    types[0] = (IrType){.kind = IR_TYPE_VOID, .layout = {.resolved = true}};
    types[1] = (IrType){.id = {.value = 1}, .kind = IR_TYPE_INTEGER, .bit_width = 8,
                        .layout = {.size = 1, .alignment = 1, .resolved = true}};
    types[2] = (IrType){.id = {.value = 2}, .kind = IR_TYPE_INTEGER, .bit_width = 32,
                        .layout = {.size = 4, .alignment = 4, .resolved = true}};
    types[3] = (IrType){.id = {.value = 3}, .kind = IR_TYPE_POINTER, .element_type = {.value = 2},
                        .layout = {.size = 8, .alignment = 8, .resolved = true}};
    types[4] = (IrType){.id = {.value = 4}, .kind = IR_TYPE_ARRAY, .element_type = {.value = 1}, .element_count = 26,
                        .layout = {.size = 26, .alignment = 1, .resolved = true}};
    IrSymbol symbols[4] = {
        {.id = {.value = 0}, .name = S8("payload"), .type = {.value = 4}, .kind = IR_SYMBOL_DATA,
         .linkage = IR_LINKAGE_EXTERNAL, .is_definition = true},
        {.id = {.value = 1}, .name = S8("word"), .type = {.value = 2}, .kind = IR_SYMBOL_DATA,
         .linkage = IR_LINKAGE_EXTERNAL, .is_definition = true},
        {.id = {.value = 2}, .name = S8("before_word"), .type = {.value = 3}, .kind = IR_SYMBOL_DATA,
         .linkage = IR_LINKAGE_EXTERNAL, .is_definition = true},
        {.id = {.value = 3}, .name = S8("imported"), .type = {.value = 2}, .kind = IR_SYMBOL_DATA,
         .linkage = IR_LINKAGE_IMPORT},
    };
    u8 bytes[26] = {3, 0, 0, 0, 0, 0, 0, 0, 0, 7, 8, 9, 10, 11, 12, 13, 0, 0, 0, 0, 0, 0, 0, 0, 17, 18};
    // Reversed canonical order must still produce the physical order 1, 16.
    IrGlobalRelocation relocations[2] = {
        {.symbol = {.value = 3}, .offset = 16},
        {.symbol = {.value = 1}, .addend = -4, .offset = 1},
    };
    IrGlobal globals[3] = {
        {.symbol = {.value = 0}, .type = {.value = 4}, .bytes = {.pointer = bytes, .length = sizeof(bytes)},
         .initializer_kind = IR_GLOBAL_INITIALIZER_BYTES, .relocations = relocations, .relocation_count = 2, .alignment = 1},
        {.symbol = {.value = 1}, .type = {.value = 2}, .initializer_kind = IR_GLOBAL_INITIALIZER_INTEGER,
         .initializer_bits = 31, .alignment = 4},
        {.symbol = {.value = 2}, .type = {.value = 3}, .initializer_kind = IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS,
         .initializer_symbol = {.value = 1}, .initializer_addend = -4, .alignment = 8},
    };
    IrModule modules[1] = {{.name = S8("relocated_global_test"), .globals = globals, .global_count = 3}};
    IrProgram program = {.arena = arena, .modules = modules, .module_count = 1,
                         .types = {.types = types, .count = 5}, .symbols = {.symbols = symbols, .count = 4}};
    program.data_layout.pointer.size = 8;
    LlvmBitcodeOptions options = LLVM_BITCODE_OPTIONS_DEFAULT;
    options.target_triple = S8("x86_64-unknown-linux-gnu");
    options.data_layout = S8("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
    options.validate_ir = false;
    LlvmBitcodeArtifact first = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    LlvmBitcodeArtifact second = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(first));
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(second));
    BUSTER_TEST(arguments, first.bytes.pointer && second.bytes.pointer && first.bytes.length == second.bytes.length &&
                           !memcmp(first.bytes.pointer, second.bytes.pointer, first.bytes.length));
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    // The negative canonical addend is not a C source fixture. Have an
    // independent LLVM consumer parse and lower this exact module as well.
    String8 compiler = executable_resolve_in_path(arena, S8("clang"));
    if (compiler.length && first.success)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        String8 bitcode = buster_test_temporary_path(temporary.arena, S8("buster-llvm-negative-addend"), S8(".bc"));
        String8 object = buster_test_temporary_path(temporary.arena, S8("buster-llvm-negative-addend"), S8(".o"));
        bool written = file_write(bitcode, first.bytes);
        BUSTER_TEST(arguments, written);
        if (written)
        {
            String8 command[] = {compiler, S8("-c"), bitcode, S8("-o"), object};
            ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
                (ProcessSpawnOptions){.use_process_environment = true, .search_path = true,
                    .capture = ((u64)1 << STANDARD_STREAM_ERROR)});
            BUSTER_TEST(arguments, spawned.handle != 0);
            if (spawned.handle)
            {
                ProcessWaitResult compiled = os_process_wait_sync(temporary.arena, spawned);
                if (compiled.result != PROCESS_RESULT_SUCCESS)
                {
                    ByteSlice errors = compiled.streams[STANDARD_STREAM_ERROR];
                    arguments->show(arguments, S8("LLVM rejected negative addend: {S8}\n"),
                                    (String8){.pointer = (char8*)errors.pointer, .length = errors.length});
                }
                BUSTER_TEST(arguments, compiled.result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(temporary);
    }
#endif

    relocations[0].offset = 23;
    LlvmBitcodeArtifact range = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    BUSTER_TEST(arguments, range.error.code == LLVM_BITCODE_ERROR_IR_VALIDATION && !range.bytes.length);
    relocations[0].offset = 4;
    LlvmBitcodeArtifact overlap = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    BUSTER_TEST(arguments, overlap.error.code == LLVM_BITCODE_ERROR_IR_VALIDATION && !overlap.bytes.length);
    relocations[0].offset = 16;
    relocations[0].is_label_address = true;
    LlvmBitcodeArtifact label = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    BUSTER_TEST(arguments, label.error.code == LLVM_BITCODE_ERROR_UNSUPPORTED_GLOBAL_INITIALIZER && !label.bytes.length);
    relocations[0].is_label_address = false;
    symbols[3].is_thread_local = true;
    LlvmBitcodeArtifact tls = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    BUSTER_TEST(arguments, tls.error.code == LLVM_BITCODE_ERROR_UNSUPPORTED_GLOBAL_INITIALIZER && !tls.bytes.length);
    symbols[3].is_thread_local = false;
    globals[1].is_thread_local = true;
    LlvmBitcodeArtifact global_tls = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    BUSTER_TEST(arguments, global_tls.error.code == LLVM_BITCODE_ERROR_UNSUPPORTED_GLOBAL_INITIALIZER && !global_tls.bytes.length);

    return result;
}

// Enough globals and initializer constants to grow the writer's hashed name
// and constant indexes several times, with repeated initializers that must
// share one pool entry and a late link name that must collide with an early
// one (#1499).
BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_indexed_lookups(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    enum
    {
        GLOBAL_COUNT = 300,
        DISTINCT_INITIALIZERS = 200,
        NAME_LENGTH = 4,
    };
    IrType types[2] = {0};
    types[0] = (IrType){.kind = IR_TYPE_VOID, .layout = {.resolved = true}};
    types[1] = (IrType){.id = {.value = 1}, .kind = IR_TYPE_INTEGER, .bit_width = 32,
                        .layout = {.size = 4, .alignment = 4, .resolved = true}};
    IrSymbol* symbols = arena_allocate(arena, IrSymbol, GLOBAL_COUNT);
    IrGlobal* globals = arena_allocate(arena, IrGlobal, GLOBAL_COUNT);
    char8* names = arena_allocate(arena, char8, GLOBAL_COUNT * NAME_LENGTH);
    for (u32 index = 0; index < GLOBAL_COUNT; index += 1)
    {
        char8* name = names + index * NAME_LENGTH;
        name[0] = 'g';
        name[1] = (char8)('0' + index / 100);
        name[2] = (char8)('0' + index / 10 % 10);
        name[3] = (char8)('0' + index % 10);
        symbols[index] = (IrSymbol){.id = {.value = index}, .name = {.pointer = name, .length = NAME_LENGTH}, .type = {.value = 1},
                                    .kind = IR_SYMBOL_DATA, .linkage = IR_LINKAGE_EXTERNAL, .is_definition = true};
        globals[index] = (IrGlobal){.symbol = {.value = index}, .type = {.value = 1}, .initializer_kind = IR_GLOBAL_INITIALIZER_INTEGER,
                                    .initializer_bits = index % DISTINCT_INITIALIZERS, .alignment = 4};
    }
    IrModule modules[1] = {{.name = S8("indexed_lookups"), .globals = globals, .global_count = GLOBAL_COUNT}};
    IrProgram program = {.arena = arena, .modules = modules, .module_count = 1,
                         .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}, .symbols = {.symbols = symbols, .count = GLOBAL_COUNT}};
    program.data_layout.pointer.size = 8;
    LlvmBitcodeOptions options = LLVM_BITCODE_OPTIONS_DEFAULT;
    options.target_triple = S8("x86_64-unknown-linux-gnu");
    options.data_layout = S8("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
    options.validate_ir = false;
    LlvmBitcodeArtifact first = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    LlvmBitcodeArtifact second = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(first));
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(second));
    BUSTER_TEST(arguments, first.stats.global_count == GLOBAL_COUNT && first.stats.constant_count == DISTINCT_INITIALIZERS);
    BUSTER_TEST(arguments, first.bytes.pointer && second.bytes.pointer && first.bytes.length == second.bytes.length &&
                           !memcmp(first.bytes.pointer, second.bytes.pointer, first.bytes.length));

    symbols[GLOBAL_COUNT - 1].link_name = symbols[1].name;
    LlvmBitcodeArtifact collision = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    BUSTER_TEST(arguments, !llvm_bitcode_artifact_is_valid(collision) && !collision.bytes.length);
    BUSTER_TEST(arguments, collision.error.code == LLVM_BITCODE_ERROR_DUPLICATE_SYMBOL &&
                           collision.error.symbol.value == GLOBAL_COUNT - 1);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_integer_counts(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    IrType types[3] = {
        {.kind = IR_TYPE_VOID, .layout = {.resolved = true}},
        {.id = {.value = 1}, .kind = IR_TYPE_INTEGER, .bit_width = 32, .layout = {.size = 4, .alignment = 4, .resolved = true}},
        {.id = {.value = 2}, .kind = IR_TYPE_FUNCTION, .return_type = {.value = 1},
         .calling_convention = IR_CALLING_CONVENTION_C, .layout = {.resolved = true}},
    };
    IrSymbol symbol = {.name = S8("canonical_counts"), .link_name = S8("canonical_counts"),
                       .type = {.value = 2}, .kind = IR_SYMBOL_FUNCTION, .linkage = IR_LINKAGE_EXTERNAL, .is_definition = true};
    u64 constant_immediate = 1;
    IrValueId operands[4] = {{.value = 0}, {.value = 1}, {.value = 2}, {.value = 3}};
    IrValueId returned = {.value = 4};
    IrUnaryOperation operations[4] = {
        IR_UNARY_INTEGER_COUNT_LEADING_ZEROS, IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS,
        IR_UNARY_INTEGER_POPULATION_COUNT, IR_UNARY_INTEGER_POPULATION_COUNT,
    };
    IrInstruction instructions[6] = {0};
    instructions[0] = (IrInstruction){
        .opcode = IR_OPCODE_CONSTANT_INTEGER, .canonical_type = {.value = 1}, .result = {.value = 0},
        .immediates = &constant_immediate, .immediate_count = 1, .next = {.value = 1},
        .conversion_operation = IR_CONVERSION_COUNT, .unary_operation = IR_UNARY_COUNT, .binary_operation = IR_BINARY_COUNT,
    };
    for (u32 index = 0; index < 4; index += 1)
    {
        instructions[index + 1] = (IrInstruction){
            .opcode = IR_OPCODE_UNARY, .canonical_type = {.value = 1}, .result = {.value = index + 1},
            .operands = operands + index, .operand_count = 1, .unary_operation = (u8)operations[index],
            .next = {.value = index + 2},
            .conversion_operation = IR_CONVERSION_COUNT, .binary_operation = IR_BINARY_COUNT,
        };
    }
    instructions[5] = (IrInstruction){
        .opcode = IR_OPCODE_RETURN, .canonical_type = {.value = 0}, .result = IR_VALUE_ID_INVALID,
        .operands = &returned, .operand_count = 1, .next = IR_INSTRUCTION_ID_INVALID,
        .conversion_operation = IR_CONVERSION_COUNT, .unary_operation = IR_UNARY_COUNT, .binary_operation = IR_BINARY_COUNT,
    };
    IrValue values[5] = {0};
    for (u32 index = 0; index < 5; index += 1)
    {
        values[index] = (IrValue){.canonical_type = {.value = 1}, .definition = {.value = index}, .category = IR_VALUE_VALUE};
    }
    IrBlock block = {.first_instruction = {.value = 0}, .last_instruction = {.value = 5}, .terminated = true, .sealed = true};
    IrFunction function = {
        .name = S8("canonical_counts"), .symbol = {.value = 0}, .canonical_type = {.value = 2}, .entry = {.value = 0},
        .blocks = &block, .instructions = instructions, .values = values, .block_count = 1,
        .instruction_count = 6, .value_count = 5, .state = IR_FUNCTION_LOWERED,
    };
    IrModule module = {.name = S8("integer_counts"), .functions = &function, .function_count = 1, .lowered_function_count = 1};
    IrProgram program = {.arena = arena, .modules = &module, .module_count = 1,
                         .types = {.types = types, .count = 3}, .symbols = {.symbols = &symbol, .count = 1}, .lowered_function_count = 1};
    LlvmBitcodeOptions options = LLVM_BITCODE_OPTIONS_DEFAULT;
    options.target_triple = S8("x86_64-unknown-linux-gnu");
    options.validate_ir = false;
    String8 compiler = executable_resolve_in_path(arena, S8("clang"));
    for (u32 width = 1; width <= 128; width = width == 64 ? 128 : width + 1)
    {
        types[1].bit_width = width;
        types[1].layout.size = (width + 7) / 8;
        types[1].layout.alignment = width == 64 ? 8 : width == 32 ? 4 : width == 16 ? 2 : 1;
        constant_immediate = width & 1 ? 0 : 1;
        LlvmBitcodeArtifact first = llvm_bitcode_emit_with_options(arena, &program, &module, 1, options);
        LlvmBitcodeArtifact second = llvm_bitcode_emit_with_options(arena, &program, &module, 1, options);
        if (width > 64)
        {
            BUSTER_TEST(arguments, first.error.code == LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION &&
                                  string_first_sequence(first.error.message, S8("width from 1 to 64")) != BUSTER_STRING_NO_MATCH);
            BUSTER_TEST(arguments, !first.success && !first.bytes.length && !second.success && !second.bytes.length);
        }
        else
        {
            BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(first) && llvm_bitcode_artifact_is_valid(second));
            BUSTER_TEST(arguments, first.bytes.length == second.bytes.length &&
                                  !memcmp(first.bytes.pointer, second.bytes.pointer, first.bytes.length));
            BUSTER_TEST(arguments, first.stats.function_count == 4 && first.stats.defined_function_count == 1);
            BUSTER_TEST(arguments, first.stats.instruction_count == 6);
            if (compiler.length && first.success && (width == 1 || width == 8 || width == 16 || width == 32 || width == 64))
            {
                TemporalArena temporary = scratch_begin(&arguments->arena, 1);
                Arena* scratch = temporary.arena;
                String8 bitcode = buster_test_temporary_path(scratch, S8("buster-count-canonical"), S8(".bc"));
                String8 object = buster_test_temporary_path(scratch, S8("buster-count-canonical"), S8(".o"));
                BUSTER_TEST(arguments, file_write(bitcode, first.bytes));
                for (u32 optimization = 0; optimization < 2; optimization += 1)
                {
                    String8 command[] = {compiler, optimization ? S8("-O2") : S8("-O0"), S8("-c"), bitcode, S8("-o"), object};
                    ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
                        (ProcessSpawnOptions){.use_process_environment = true, .search_path = true,
                            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
                    BUSTER_TEST(arguments, spawned.handle != 0);
                    if (spawned.handle)
                    {
                        ProcessWaitResult compiled = os_process_wait_sync(scratch, spawned);
                        if (compiled.result != PROCESS_RESULT_SUCCESS)
                        {
                            ByteSlice errors = compiled.streams[STANDARD_STREAM_ERROR];
                            arguments->show(arguments, S8("LLVM count i{u32} -O{u32}: {S8}\n"), width, optimization ? 2u : 0u,
                                            (String8){.pointer = (char8*)errors.pointer, .length = errors.length});
                        }
                        BUSTER_TEST(arguments, compiled.result == PROCESS_RESULT_SUCCESS);
                    }
                }
                scratch_end(temporary);
            }
        }
    }
    // Combine two widths in one module so the declarations, constants, and
    // relative call operands also work when their value IDs are interleaved.
    types[1].bit_width = 8;
    types[1].layout.size = 1;
    types[1].layout.alignment = 1;
    IrType mixed_types[5] = {types[0], types[1], types[2], types[1], types[2]};
    mixed_types[3].id.value = 3;
    mixed_types[3].bit_width = 64;
    mixed_types[3].layout.size = 8;
    mixed_types[3].layout.alignment = 8;
    mixed_types[4].id.value = 4;
    mixed_types[4].return_type.value = 3;
    IrInstruction wide_instructions[6];
    memcpy(wide_instructions, instructions, sizeof(instructions));
    u64 wide_immediate = 1;
    wide_instructions[0].immediates = &wide_immediate;
    for (u32 index = 0; index < 6; index += 1)
    {
        if (wide_instructions[index].canonical_type.value == 1)
        {
            wide_instructions[index].canonical_type.value = 3;
        }
    }
    IrValue wide_values[5];
    memcpy(wide_values, values, sizeof(values));
    for (u32 index = 0; index < 5; index += 1)
    {
        wide_values[index].canonical_type.value = 3;
    }
    IrSymbol mixed_symbols[2] = {symbol, symbol};
    mixed_symbols[1].id.value = 1;
    mixed_symbols[1].name = S8("canonical_counts64");
    mixed_symbols[1].link_name = S8("canonical_counts64");
    mixed_symbols[1].type.value = 4;
    IrFunction mixed_functions[2] = {function, function};
    mixed_functions[1].name = S8("canonical_counts64");
    mixed_functions[1].symbol.value = 1;
    mixed_functions[1].canonical_type.value = 4;
    mixed_functions[1].instructions = wide_instructions;
    mixed_functions[1].values = wide_values;
    IrModule mixed_module = module;
    mixed_module.functions = mixed_functions;
    mixed_module.function_count = 2;
    mixed_module.lowered_function_count = 2;
    IrProgram mixed_program = program;
    mixed_program.modules = &mixed_module;
    mixed_program.types.types = mixed_types;
    mixed_program.types.count = BUSTER_ARRAY_LENGTH(mixed_types);
    mixed_program.symbols.symbols = mixed_symbols;
    mixed_program.symbols.count = BUSTER_ARRAY_LENGTH(mixed_symbols);
    mixed_program.lowered_function_count = 2;
    LlvmBitcodeArtifact mixed = llvm_bitcode_emit_with_options(arena, &mixed_program, &mixed_module, 1, options);
    LlvmBitcodeArtifact repeated = llvm_bitcode_emit_with_options(arena, &mixed_program, &mixed_module, 1, options);
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(mixed) && llvm_bitcode_artifact_is_valid(repeated));
    BUSTER_TEST(arguments, mixed.bytes.length == repeated.bytes.length &&
                          !memcmp(mixed.bytes.pointer, repeated.bytes.pointer, mixed.bytes.length));
    BUSTER_TEST(arguments, mixed.stats.function_count == 8 && mixed.stats.defined_function_count == 2);
    if (compiler.length && mixed.success)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* scratch = temporary.arena;
        String8 bitcode = buster_test_temporary_path(scratch, S8("buster-count-mixed"), S8(".bc"));
        String8 object = buster_test_temporary_path(scratch, S8("buster-count-mixed"), S8(".o"));
        BUSTER_TEST(arguments, file_write(bitcode, mixed.bytes));
        for (u32 optimization = 0; optimization < 2; optimization += 1)
        {
            String8 command[] = {compiler, optimization ? S8("-O2") : S8("-O0"), S8("-c"), bitcode, S8("-o"), object};
            ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
                (ProcessSpawnOptions){.use_process_environment = true, .search_path = true,
                    .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
            BUSTER_TEST(arguments, spawned.handle != 0);
            if (spawned.handle)
            {
                ProcessWaitResult compiled = os_process_wait_sync(scratch, spawned);
                if (compiled.result != PROCESS_RESULT_SUCCESS)
                {
                    ByteSlice errors = compiled.streams[STANDARD_STREAM_ERROR];
                    arguments->show(arguments, S8("LLVM mixed count -O{u32}: {S8}\n"), optimization ? 2u : 0u,
                                    (String8){.pointer = (char8*)errors.pointer, .length = errors.length});
                }
                BUSTER_TEST(arguments, compiled.result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(temporary);
    }
    types[1].bit_width = 32;
    types[1].layout.size = 4;
    types[1].layout.alignment = 4;
    instructions[1].unary_operation = IR_UNARY_COUNT;
    LlvmBitcodeArtifact malformed = llvm_bitcode_emit_with_options(arena, &program, &module, 1, options);
    BUSTER_TEST(arguments, !malformed.success && !malformed.bytes.length && malformed.error.code != LLVM_BITCODE_ERROR_NONE);
    return result;
}

// Collection records each constant instruction's pool value id and value
// numbering reads it, so searches of the locked pool come from the module's
// auxiliary constants alone, not from its constant rows. Two modules that
// differ only in 60 more constant rows must search the locked pool equally
// often, while their pools differ by exactly those 60 constants.
BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_constant_handoff(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 constant_counts[] = {4, 64};
    LlvmBitcodeStats stats[BUSTER_ARRAY_LENGTH(constant_counts)] = {0};
    bool emitted_all = true;
    for (u32 variant = 0; variant < BUSTER_ARRAY_LENGTH(constant_counts); variant += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        String8 source = S8("typedef unsigned long long u64;\nu64 chain(u64 a)\n{\n    u64 r = a;\n");
        for (u32 index = 0; index < constant_counts[variant]; index += 1)
        {
            source = string_format(arena, S8("{S8}    r = r * a + {u32}u;\n"), source, 1001 + index);
        }
        source = string_format(arena, S8("{S8}    return r;\n}}\n"), source);
        String8 input = buster_test_temporary_path(arena, S8("buster-llvm-constant-handoff"), S8(".c"));
        String8 output = buster_test_temporary_path(arena, S8("buster-llvm-constant-handoff"), S8(".bc"));
        BUSTER_TEST(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(source)));
        String8 command[] = {S8("-emit-llvm"), S8("-o"), output, input};
        CompilerDriverResult emitted = compiler_driver_execute_invocation(
            arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
        bool success = emitted.error == COMPILER_DRIVER_ERROR_NONE && emitted.has_llvm_bitcode && emitted.llvm_bitcode.success;
        BUSTER_TEST_RAW(arguments, success, emitted.diagnostic);
        emitted_all &= success;
        stats[variant] = emitted.llvm_bitcode.stats;
        scratch_end(temporary);
    }
    if (emitted_all)
    {
        BUSTER_TEST(arguments, stats[1].constant_count - stats[0].constant_count == constant_counts[1] - constant_counts[0]);
        BUSTER_TEST(arguments, stats[1].locked_constant_searches == stats[0].locked_constant_searches);
        BUSTER_TEST(arguments, stats[0].constant_searches > stats[0].locked_constant_searches);
    }
    return result;
}

UnitTestResult llvm_bitcode_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;

    BUSTER_TEST_FIXTURE(arguments, llvm_bitcode_test_integer_encoding);
    BUSTER_TEST_FIXTURE(arguments, llvm_bitcode_test_scalar_abi_wire);
    BUSTER_TEST_FIXTURE(arguments, llvm_bitcode_test_scalar_abi_runtime);

    IrType types[3] = {0};
    types[0] = (IrType){
        .kind = IR_TYPE_VOID,
        .layout = {.resolved = true},
    };
    types[1] = (IrType){
        .id = {.value = 1},
        .kind = IR_TYPE_INTEGER,
        .layout = {.size = 4, .alignment = 4, .resolved = true},
        .bit_width = 32,
        .is_signed = true,
    };
    types[2] = (IrType){
        .id = {.value = 2},
        .return_type = {.value = 1},
        .kind = IR_TYPE_FUNCTION,
        .calling_convention = IR_CALLING_CONVENTION_C,
        .layout = {.resolved = true},
    };

    IrSymbol symbols[1] = {
        {
            .name = S8("main"),
            .link_name = S8("main"),
            .type = {.value = 2},
            .kind = IR_SYMBOL_FUNCTION,
            .linkage = IR_LINKAGE_EXTERNAL,
            .is_definition = true,
        },
    };
    u64 constant_immediates[1] = {42};
    IrValueId return_operands[1] = {{.value = 0}};
    IrInstruction instructions[2] = {0};
    instructions[0] = (IrInstruction){
        .immediates = constant_immediates,
        .canonical_type = {.value = 1},
        .next = {.value = 1},
        .result = {.value = 0},
        .opcode = IR_OPCODE_CONSTANT_INTEGER,
        .conversion_operation = IR_CONVERSION_COUNT,
        .unary_operation = IR_UNARY_COUNT,
        .binary_operation = IR_BINARY_COUNT,
        .immediate_count = 1,
    };
    instructions[1] = (IrInstruction){
        .operands = return_operands,
        .canonical_type = {.value = 0},
        .next = IR_INSTRUCTION_ID_INVALID,
        .result = IR_VALUE_ID_INVALID,
        .opcode = IR_OPCODE_RETURN,
        .conversion_operation = IR_CONVERSION_COUNT,
        .unary_operation = IR_UNARY_COUNT,
        .binary_operation = IR_BINARY_COUNT,
        .operand_count = 1,
    };
    IrValue values[1] = {
        {
            .canonical_type = {.value = 1},
            .definition = {.value = 0},
            .category = IR_VALUE_VALUE,
        },
    };
    IrBlock blocks[1] = {
        {
            .first_instruction = {.value = 0},
            .last_instruction = {.value = 1},
            .terminated = true,
            .sealed = true,
        },
    };
    IrFunction functions[1] = {
        {
            .name = S8("main"),
            .symbol = {.value = 0},
            .canonical_type = {.value = 2},
            .entry = {.value = 0},
            .blocks = blocks,
            .instructions = instructions,
            .values = values,
            .block_count = 1,
            .instruction_count = 2,
            .value_count = 1,
            .state = IR_FUNCTION_LOWERED,
        },
    };
    IrModule modules[1] = {
        {
            .name = S8("bitcode_test"),
            .functions = functions,
            .function_count = 1,
            .lowered_function_count = 1,
        },
    };
    IrProgram program = {
        .arena = arena,
        .modules = modules,
        .types = {.types = types, .count = 3},
        .symbols = {.symbols = symbols, .count = 1},
        .module_count = 1,
        .lowered_function_count = 1,
    };
    LlvmBitcodeOptions options = LLVM_BITCODE_OPTIONS_DEFAULT;
    options.target_triple = S8("x86_64-unknown-linux-gnu");
    options.data_layout = S8("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
    options.source_filename = S8("bitcode_test.c");
    options.validate_ir = false;

    LlvmBitcodeArtifact first = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    LlvmBitcodeArtifact second = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(first));
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(second));
    BUSTER_TEST(arguments, first.bytes.length >= 4);
    BUSTER_TEST(arguments, first.bytes.length == second.bytes.length);
    BUSTER_TEST(arguments, first.bytes.length && !memcmp(first.bytes.pointer, second.bytes.pointer, first.bytes.length));
    BUSTER_TEST(arguments, first.bytes.pointer[0] == 'B' && first.bytes.pointer[1] == 'C' && first.bytes.pointer[2] == 0xc0 &&
                               first.bytes.pointer[3] == 0xde);
    BUSTER_TEST(arguments, first.stats.deterministic);
    BUSTER_TEST(arguments, first.stats.module_count == 1);
    BUSTER_TEST(arguments, first.stats.function_count == 1 && first.stats.defined_function_count == 1);
    BUSTER_TEST(arguments, first.stats.instruction_count == 2);
    BUSTER_TEST(arguments, first.stats.binary_bytes == first.bytes.length);
    BUSTER_TEST(arguments, string_equal(llvm_bitcode_error_code_name(LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION),
                                        S8("unsupported_instruction")));

    u32 widths[] = {1, 8, 16, 32, 64};
    for (u32 width_index = 0; width_index < BUSTER_ARRAY_LENGTH(widths); width_index += 1)
    {
        u32 width = widths[width_index];
        u64 sign = UINT64_C(1) << (width - 1);
        u64 patterns[] = {0, 1, sign - 1, sign, sign + 1, UINT64_MAX};
        types[1].kind = width == 1 ? IR_TYPE_BOOLEAN : IR_TYPE_INTEGER;
        types[1].bit_width = width;
        types[1].is_signed = width != 1;
        types[1].layout.size = (width + 7) / 8;
        types[1].layout.alignment = (width + 7) / 8;
        for (u32 pattern_index = 0; pattern_index < BUSTER_ARRAY_LENGTH(patterns); pattern_index += 1)
        {
            constant_immediates[0] = patterns[pattern_index];
            LlvmBitcodeArtifact encoded = llvm_bitcode_emit_with_options(arena, &program, modules, 1, options);
            BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(encoded));
            BUSTER_TEST(arguments, llvm_bitcode_test_integer(encoded.bytes, constant_immediates[0], width));
        }
    }

    LlvmBitcodeArtifact invalid = llvm_bitcode_emit_with_options(0, &program, modules, 1, options);
    BUSTER_TEST(arguments, !llvm_bitcode_artifact_is_valid(invalid));
    BUSTER_TEST(arguments, invalid.error.code == LLVM_BITCODE_ERROR_INVALID_ARGUMENT);

    // An atomic aggregate is wider than its operand, so it needs a record of
    // its own -- the operand plus a `[1 x i8]` padding array, two records --
    // where an atomic type the operand's own size is that operand's type and
    // adds none. Clang writes the padded one as `{ %struct.three, [1 x i8] }`;
    // this pins that a record is built at all and that the unpadded case still
    // aliases, which is the half every atomic scalar depends on (#767).
    LlvmBitcodeArtifact without_atomic = llvm_bitcode_test_atomic_record(arena, 3, false);
    LlvmBitcodeArtifact atomic_alias = llvm_bitcode_test_atomic_record(arena, 3, true);
    LlvmBitcodeArtifact atomic_padded = llvm_bitcode_test_atomic_record(arena, 4, true);
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(without_atomic));
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(atomic_alias));
    BUSTER_TEST(arguments, llvm_bitcode_artifact_is_valid(atomic_padded));
    BUSTER_TEST(arguments, atomic_alias.stats.type_count == without_atomic.stats.type_count);
    BUSTER_TEST(arguments, atomic_padded.stats.type_count == without_atomic.stats.type_count + 2);
    UnitTestResult uefi_boundary = llvm_bitcode_test_uefi_boundary(arguments);
    result.test_count += uefi_boundary.test_count;
    result.succeeded_test_count += uefi_boundary.succeeded_test_count;
    UnitTestResult consumers = llvm_bitcode_test_consumers(arguments);
    result.test_count += consumers.test_count;
    result.succeeded_test_count += consumers.succeeded_test_count;
    UnitTestResult stack_records = llvm_bitcode_test_stack_records(arguments);
    result.test_count += stack_records.test_count;
    result.succeeded_test_count += stack_records.succeeded_test_count;
    UnitTestResult stack_scopes = llvm_bitcode_test_stack_scopes(arguments);
    result.test_count += stack_scopes.test_count;
    result.succeeded_test_count += stack_scopes.succeeded_test_count;
    UnitTestResult switches = llvm_bitcode_test_switches(arguments);
    result.test_count += switches.test_count;
    result.succeeded_test_count += switches.succeeded_test_count;
    UnitTestResult diagnostics = llvm_bitcode_test_abi_diagnostics(arguments);
    result.test_count += diagnostics.test_count;
    result.succeeded_test_count += diagnostics.succeeded_test_count;
    UnitTestResult relocations = llvm_bitcode_test_relocated_globals(arguments);
    result.test_count += relocations.test_count;
    result.succeeded_test_count += relocations.succeeded_test_count;
    UnitTestResult variadic_diagnostics = llvm_bitcode_test_variadic_diagnostics(arguments);
    result.test_count += variadic_diagnostics.test_count;
    result.succeeded_test_count += variadic_diagnostics.succeeded_test_count;
    UnitTestResult canonical_variadics = llvm_bitcode_test_canonical_variadics(arguments);
    result.test_count += canonical_variadics.test_count;
    result.succeeded_test_count += canonical_variadics.succeeded_test_count;
    UnitTestResult win64_object = llvm_bitcode_test_variadic_win64_object(arguments);
    result.test_count += win64_object.test_count;
    result.succeeded_test_count += win64_object.succeeded_test_count;
    UnitTestResult integer_counts = llvm_bitcode_test_integer_counts(arguments);
    result.test_count += integer_counts.test_count;
    result.succeeded_test_count += integer_counts.succeeded_test_count;
    UnitTestResult constant_handoff = llvm_bitcode_test_constant_handoff(arguments);
    result.test_count += constant_handoff.test_count;
    result.succeeded_test_count += constant_handoff.succeeded_test_count;
    UnitTestResult indexed_lookups = llvm_bitcode_test_indexed_lookups(arguments);
    result.test_count += indexed_lookups.test_count;
    result.succeeded_test_count += indexed_lookups.succeeded_test_count;
    return result;
}
#endif
