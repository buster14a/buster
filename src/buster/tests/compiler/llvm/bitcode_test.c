#include <buster/tests/compiler/llvm/bitcode_test.h>
#if BUSTER_INCLUDE_TESTS

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
    return result;
}
#endif
