#include <buster/tests/compiler/llvm/bitcode_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/os.h>

BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_consumers(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 compiler = executable_resolve_in_path(arguments->arena, S8("clang"));
    String8 fixtures[] = {
        S8("tests/basic_c_llvm_scalars.c"), S8("tests/basic_c_llvm_layout.c"),
#if BUSTER_CPU_ARCH_X86_64
        S8("tests/basic_c_llvm_aggregate_abi_callee.c"), S8("tests/basic_c_llvm_aggregate_abi_caller.c"),
        S8("tests/basic_c_llvm_vector_abi.c"),
#endif
    };
    for (u32 fixture = 0; fixture < BUSTER_ARRAY_LENGTH(fixtures); fixture += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        String8 output = buster_test_temporary_path(arena, S8("buster-llvm-consumer"), S8(".bc"));
        String8 command[] = {S8("-emit-llvm"), S8("-o"), output, fixtures[fixture]};
        CompilerDriverResult emitted = compiler_driver_execute_invocation(
            arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
        if (emitted.error != COMPILER_DRIVER_ERROR_NONE)
        {
            arguments->show(arguments, S8("LLVM fixture {S8}: {S8}\n"), fixtures[fixture], emitted.diagnostic);
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
            if (fixture >= 2)
            {
                compile[compile_count++] = fixture == 4 ? S8("tests/basic_c_llvm_vector_abi_main.c") : fixtures[fixture ^ 1u];
            }
            compile[compile_count++] = S8("-o");
            compile[compile_count++] = executable;
            ProcessSpawnResult spawned = os_process_spawn((SliceString8){.pointer = compile, .length = compile_count}, (SliceString8){0}, (SliceString8){0},
                (ProcessSpawnOptions){.use_process_environment = true,
                    .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
            BUSTER_TEST(arguments, spawned.handle != 0);
            if (spawned.handle)
            {
                ProcessWaitResult compiled = os_process_wait_sync(arena, spawned);
                if (compiled.result != PROCESS_RESULT_SUCCESS)
                {
                    ByteSlice errors = compiled.streams[STANDARD_STREAM_ERROR];
                    arguments->show(arguments, S8("LLVM consumer rejected {S8}: {S8}\n"), fixtures[fixture],
                                    (String8){.pointer = (char8*)errors.pointer, .length = errors.length});
                }
                BUSTER_TEST(arguments, compiled.result == PROCESS_RESULT_SUCCESS);
                if (compiled.result == PROCESS_RESULT_SUCCESS)
                {
                    String8 run[] = {executable};
                    ProcessSpawnResult child = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                        (ProcessSpawnOptions){.use_process_environment = true});
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
    u32 integers = 0;
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
            if ((block == 8 || block == 11) && depth < 2 && code_width > 0 && code_width <= 32)
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
            for (u64 index = 0; index < count && !reader.failed; index += 1)
            {
                u64 operand = llvm_bitcode_test_vbr(&reader, 6);
                if (blocks[depth] == 11 && record == 4 && count == 1) // CST_CODE_INTEGER
                {
                    u64 decoded = operand == 1 ? UINT64_C(1) << 63 : (operand & 1) ? 0 - (operand >> 1) : operand >> 1;
                    u64 mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
                    matched = (decoded & mask) == (expected & mask);
                    integers += 1;
                }
            }
        }
        else
        {
            reader.failed = true;
        }
    }
    return !reader.failed && integers == 1 && matched;
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
    UnitTestResult consumers = llvm_bitcode_test_consumers(arguments);
    result.test_count += consumers.test_count;
    result.succeeded_test_count += consumers.succeeded_test_count;
    UnitTestResult diagnostics = llvm_bitcode_test_abi_diagnostics(arguments);
    result.test_count += diagnostics.test_count;
    result.succeeded_test_count += diagnostics.succeeded_test_count;
    return result;
}
#endif
