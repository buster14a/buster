#include <buster/tests/compiler/llvm/bitcode_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/llvm/bitcode_internal.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/os.h>

BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_integer_encoding(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // LLVM reserves the encoded value 1 for INT64_MIN, not for the minimum
    // of the IR integer type. These are wire values, independent of the writer.
    u32 widths[] = {1, 8, 16, 32, 64};
    u64 expected[] = {3, 257, 65537, UINT64_C(4294967297), 1};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(widths); index += 1)
    {
        u64 sign = UINT64_C(1) << (widths[index] - 1);
        BUSTER_TEST(arguments, llvm_bitcode_test_encode_integer_bits(sign, widths[index]) == expected[index]);
    }
    for (u32 width = 1; width <= 64; width += 1)
    {
        u64 sign = UINT64_C(1) << (width - 1);
        u64 mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
        // Exhaust every pattern through i16; then check each larger width's
        // zero, one, sign boundary, all-ones, and discarded high input bits.
        u64 boundaries[] = {0, 1, sign - 1, sign, sign + 1, mask, UINT64_MAX};
        u64 count = width <= 16 ? (UINT64_C(1) << width) : BUSTER_ARRAY_LENGTH(boundaries);
        for (u64 index = 0; index < count; index += 1)
        {
            u64 bits = width <= 16 ? index : boundaries[index];
            u64 encoded = llvm_bitcode_test_encode_integer_bits(bits, width);
            u64 decoded = encoded >> 1;
            if (encoded == 1)
            {
                decoded = UINT64_C(1) << 63;
            }
            else if (encoded & 1)
            {
                decoded = 0 - decoded;
            }
            BUSTER_TEST(arguments, (decoded & mask) == (bits & mask));
        }
    }
    return result;
}

#if !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL bool llvm_bitcode_test_run(UnitTestArguments* arguments, SliceString8 command)
{
    bool success = false;
    ProcessSpawnResult spawn = os_process_spawn(command, (SliceString8){0}, (SliceString8){0},
                                                (ProcessSpawnOptions){.use_process_environment = true,
                                                                      .capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR)});
    if (spawn.handle)
    {
        ProcessWaitResult wait = os_process_wait_sync(arguments->arena, spawn);
        success = wait.result == PROCESS_RESULT_SUCCESS;
        if (!success)
        {
            ByteSlice errors = wait.streams[STANDARD_STREAM_ERROR];
            arguments->show(arguments, S8("LLVM integer consumer failed: {S8}\n"),
                            (String8){.pointer = (char8*)errors.pointer, .length = errors.length});
        }
    }
    return success;
}

BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_integer_consumer(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 probe_command[] = {S8("clang"), S8("--version")};
    bool clang_available = llvm_bitcode_test_run(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(probe_command));
    if (clang_available)
    {
        String8 modes[] = {S8("none"), S8("mir-stack"), S8("fast"), S8("quality")};
        String8 optimizations[] = {S8("-O0"), S8("-O2")};
        for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
        {
            TemporalArena temporary = arena_begin_temporal(arguments->arena);
            Arena* arena = temporary.arena;
            String8 bitcode_path = buster_test_temporary_path(arena, S8("buster-llvm-integer"), S8(".bc"));
            String8 executable_path = buster_test_temporary_path(arena, S8("buster-llvm-integer"), S8(".exe"));
            String8 allocator = string_format(arena, S8("-fregister-allocator={S8}"), modes[mode]);
            String8 emit_command[] = {S8("-emit-llvm"), S8("-c"), S8("-nostdinc"), allocator, S8("-o"), bitcode_path,
                                      S8("tests/basic_c_llvm_integer_constants.c")};
            CompilerDriverResult emitted = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(emit_command)));
            BUSTER_TEST(arguments, emitted.error == COMPILER_DRIVER_ERROR_NONE && emitted.has_llvm_bitcode && emitted.llvm_bitcode.success);
            if (emitted.error == COMPILER_DRIVER_ERROR_NONE && emitted.has_llvm_bitcode && emitted.llvm_bitcode.success)
            {
                for (u32 optimization = 0; optimization < BUSTER_ARRAY_LENGTH(optimizations); optimization += 1)
                {
                    // The checker is compiled independently: comparing constants
                    // produced by the same broken writer can hide this regression.
                    String8 consume_command[] = {S8("clang"), S8("--driver-mode=gcc"), S8("-Wno-override-module"), optimizations[optimization],
                                                  bitcode_path, S8("tests/basic_c_llvm_integer_constants_check.c"), S8("-o"), executable_path};
                    bool compiled = llvm_bitcode_test_run(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(consume_command));
                    BUSTER_TEST(arguments, compiled);
                    if (compiled)
                    {
                        String8 run_command[] = {executable_path};
                        BUSTER_TEST(arguments, llvm_bitcode_test_run(arguments, (SliceString8)BUSTER_ARRAY_TO_SLICE(run_command)));
                    }
                }
            }
            else
            {
                arguments->show(arguments, S8("LLVM integer producer failed: {S8}\n"), emitted.diagnostic);
            }
            scratch_end(temporary);
        }
    }
    else
    {
        arguments->show(arguments, S8("LLVM integer consumer skipped: clang unavailable; wire-encoding tests still run.\n"));
    }
    return result;
}


#endif

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
    UnitTestResult integers = llvm_bitcode_test_integer_encoding(arguments);
    result.succeeded_test_count += integers.succeeded_test_count;
    result.test_count += integers.test_count;
#if !BUSTER_ANDROID && !BUSTER_IOS
    UnitTestResult consumer = llvm_bitcode_test_integer_consumer(arguments);
    result.succeeded_test_count += consumer.succeeded_test_count;
    result.test_count += consumer.test_count;
#endif
    return result;
}
#endif
