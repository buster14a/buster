#include <buster/tests/compiler/llvm/bitcode_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/os.h>
#include <buster/lib/file.h>

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
    } LlvmBitcodeConsumerFixture;
    LlvmBitcodeConsumerFixture fixtures[] = {
        {.source = S8("tests/basic_c_llvm_scalars.c")},
        {.source = S8("tests/basic_c_llvm_layout.c")},
        {.source = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_pointer_addend.c"),
         .caller = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_pointer_addend_caller.c")},
        {.source = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_pointer_table.c"),
         .caller = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_pointer_table_caller.c")},
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
        {.source = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_unaligned_pointer.c"),
         .caller = S8("src/buster/tests/compiler/llvm/fixtures/basic_c_llvm_unaligned_pointer_caller.c")},
#endif
#if BUSTER_CPU_ARCH_X86_64
        {.source = S8("tests/basic_c_llvm_aggregate_abi_callee.c"), .caller = S8("tests/basic_c_llvm_aggregate_abi_caller.c")},
        {.source = S8("tests/basic_c_llvm_aggregate_abi_caller.c"), .caller = S8("tests/basic_c_llvm_aggregate_abi_callee.c")},
        {.source = S8("tests/basic_c_llvm_vector_abi.c"), .caller = S8("tests/basic_c_llvm_vector_abi_main.c")},
#endif
        {.source = S8("tests/basic_c_llvm_integer_boundary_values.c"), .caller = S8("tests/basic_c_llvm_integer_boundary_check.c")},
    };
    for (u32 fixture = 0; fixture < BUSTER_ARRAY_LENGTH(fixtures); fixture += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        String8 output = buster_test_temporary_path(arena, S8("buster-llvm-consumer"), S8(".bc"));
        String8 command[] = {S8("-emit-llvm"), S8("-o"), output, fixtures[fixture].source};
        CompilerDriverResult emitted = compiler_driver_execute_invocation(
            arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
        if (emitted.error != COMPILER_DRIVER_ERROR_NONE)
        {
            arguments->show(arguments, S8("LLVM fixture {S8}: {S8}\n"), fixtures[fixture].source, emitted.diagnostic);
        }
        BUSTER_TEST(arguments, emitted.error == COMPILER_DRIVER_ERROR_NONE && emitted.has_llvm_bitcode && emitted.llvm_bitcode.success);
        if (compiler.length && emitted.error == COMPILER_DRIVER_ERROR_NONE)
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
            compile[compile_count++] = S8("-O2");
            compile[compile_count++] = output;
            if (fixtures[fixture].caller.length)
            {
                compile[compile_count++] = fixtures[fixture].caller;
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
                    arguments->show(arguments, S8("LLVM consumer rejected {S8}: {S8}\n"), fixtures[fixture].source,
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
                        BUSTER_TEST(arguments, os_process_wait_sync(arena, child).result == PROCESS_RESULT_SUCCESS);
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
                BUSTER_TEST(arguments, emitted.error == COMPILER_DRIVER_ERROR_LLVM_BITCODE);
                BUSTER_TEST(arguments, !emitted.llvm_bitcode.success && emitted.llvm_bitcode.bytes.length == 0);
                String8 diagnostic = mode == 4 ? S8("aggregate variadic arguments") :
                                     target == 3 && mode == 6 ? S8("SysV aggregate register layout") : S8("aggregate function ABI");
                BUSTER_TEST(arguments, string_first_sequence(emitted.diagnostic, diagnostic) != BUSTER_STRING_NO_MATCH);
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

    return result;
}

UnitTestResult llvm_bitcode_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;

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
    UnitTestResult switches = llvm_bitcode_test_switches(arguments);
    result.test_count += switches.test_count;
    result.succeeded_test_count += switches.succeeded_test_count;
    UnitTestResult diagnostics = llvm_bitcode_test_abi_diagnostics(arguments);
    result.test_count += diagnostics.test_count;
    result.succeeded_test_count += diagnostics.succeeded_test_count;
    UnitTestResult relocations = llvm_bitcode_test_relocated_globals(arguments);
    result.test_count += relocations.test_count;
    result.succeeded_test_count += relocations.succeeded_test_count;
    return result;
}
#endif
