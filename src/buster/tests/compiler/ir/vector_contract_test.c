// Canonical vector contracts: enumerate the exact vocabulary, inspect the C
// integer/predicate boundary, and exercise native and non-native consumers.
#include <buster/tests/compiler/ir/vector_contract_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/wasm/wasm.h>
#include <buster/lib/compiler/ebpf/ebpf.h>
#include <buster/lib/compiler/llvm/bitcode.h>
#include <buster/lib/string.h>

typedef struct VectorContractCase VectorContractCase;
struct VectorContractCase
{
    String8 statement;
    u8 predicate_operands;
    u8 predicate_lanes;
    bool predicate_result;
    u8 extension; // 0: F/BW; 1: also VBMI; 2: also VBMI2.
};

BUSTER_GLOBAL_LOCAL VectorContractCase const vector_contract_cases[IR_SIMD_COUNT] = {
    [IR_SIMD_LOAD] = {.statement = S8_INITIALIZER("*out = __builtin_buster_simd_load(input);")},
    [IR_SIMD_LOAD_MASKED] = {.statement = S8_INITIALIZER("*out = __builtin_buster_simd_load_masked(input, mask);"), .predicate_operands = 2, .predicate_lanes = 64},
    [IR_SIMD_STORE] = {.statement = S8_INITIALIZER("__builtin_buster_simd_store(out, *input);")},
    [IR_SIMD_STORE_MASKED] = {.statement = S8_INITIALIZER("__builtin_buster_simd_store_masked(out, mask, *input);"), .predicate_operands = 2, .predicate_lanes = 64},
    [IR_SIMD_SPLAT_BYTE] = {.statement = S8_INITIALIZER("*out = __builtin_buster_simd_splat_byte(7);")},
    [IR_SIMD_COMPARE_EQUAL_BYTE] = {.statement = S8_INITIALIZER("*bits = __builtin_buster_simd_equal_byte(*input, *other);"), .predicate_lanes = 64, .predicate_result = true},
    [IR_SIMD_COMPARE_LESS_BYTE] = {.statement = S8_INITIALIZER("*bits = __builtin_buster_simd_less_byte(*input, *other);"), .predicate_lanes = 64, .predicate_result = true},
    [IR_SIMD_SIGN_MASK_BYTE] = {.statement = S8_INITIALIZER("*bits = __builtin_buster_simd_sign_byte(*input);"), .predicate_lanes = 64, .predicate_result = true},
    [IR_SIMD_TEST_MASK_BYTE] = {.statement = S8_INITIALIZER("*bits = __builtin_buster_simd_test_byte(*input, *other);"), .predicate_lanes = 64, .predicate_result = true},
    [IR_SIMD_PERMUTE2_BYTE] = {.statement = S8_INITIALIZER("*out = __builtin_buster_simd_permute2_byte(mask, *input, *other, *input);"), .predicate_operands = 1, .predicate_lanes = 64, .extension = 1},
    [IR_SIMD_COMPRESS_BYTE] = {.statement = S8_INITIALIZER("*out = __builtin_buster_simd_compress_byte(mask, *input);"), .predicate_operands = 1, .predicate_lanes = 64, .extension = 2},
    [IR_SIMD_COMPRESS_STORE_BYTE] = {.statement = S8_INITIALIZER("__builtin_buster_simd_compress_store_byte(out, mask, *input);"), .predicate_operands = 2, .predicate_lanes = 64, .extension = 2},
    [IR_SIMD_WIDEN_BYTE_TO_WORD] = {.statement = S8_INITIALIZER("*out = __builtin_buster_simd_widen_byte(*input, 3);")},
    [IR_SIMD_SHIFT_LEFT_WORD] = {.statement = S8_INITIALIZER("*out = __builtin_buster_simd_shift_left_word(*input, 31);")},
    [IR_SIMD_TERNARY_WORD] = {.statement = S8_INITIALIZER("*out = __builtin_buster_simd_ternary_word(*input, *other, *input, 0x96);")},
    [IR_SIMD_COMPARE_EQUAL_WORD] = {.statement = S8_INITIALIZER("*bits = __builtin_buster_simd_equal_word(*input, *other);"), .predicate_lanes = 16, .predicate_result = true},
    [IR_SIMD_SPLAT_WORD] = {.statement = S8_INITIALIZER("*out = __builtin_buster_simd_splat_word(0x80000000u);")},
    [IR_SIMD_COMPARE_LESS_WORD] = {.statement = S8_INITIALIZER("*bits = __builtin_buster_simd_less_word(*input, *other);"), .predicate_lanes = 16, .predicate_result = true},
    [IR_SIMD_COMPRESS_WORD] = {.statement = S8_INITIALIZER("*out = __builtin_buster_simd_compress_word(mask, *input);"), .predicate_operands = 1, .predicate_lanes = 16},
};

BUSTER_GLOBAL_LOCAL UnitTestResult vector_contract_test_metadata(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 operation = 0; operation <= IR_UNARY_COUNT; operation += 1)
    {
        bool vector = operation == IR_UNARY_VECTOR_INTEGER_NEGATE || operation == IR_UNARY_VECTOR_FLOAT_NEGATE ||
                      operation == IR_UNARY_VECTOR_INTEGER_BITWISE_NOT;
        BUSTER_TEST(arguments, ir_vector_operation_semantics(IR_OPCODE_UNARY, operation) ==
                    (vector ? IR_VECTOR_SEMANTICS_GENERIC : IR_VECTOR_SEMANTICS_NONE));
    }
    for (u32 operation = IR_BINARY_VECTOR_INTEGER_ADD; operation < IR_BINARY_COUNT; operation += 1)
    {
        BUSTER_TEST(arguments, ir_vector_operation_semantics(IR_OPCODE_BINARY, operation) == IR_VECTOR_SEMANTICS_GENERIC);
    }
    BUSTER_TEST(arguments, ir_vector_operation_semantics(IR_OPCODE_BINARY, IR_BINARY_INTEGER_ADD) == IR_VECTOR_SEMANTICS_NONE);
    BUSTER_TEST(arguments, ir_vector_operation_semantics(IR_OPCODE_BINARY, IR_BINARY_COUNT) == IR_VECTOR_SEMANTICS_NONE);
    BUSTER_TEST(arguments, ir_vector_operation_semantics(IR_OPCODE_SIMD, IR_SIMD_COUNT) == IR_VECTOR_SEMANTICS_NONE);
    BUSTER_TEST(arguments, ir_vector_operation_semantics(IR_OPCODE_SIMD, UINT32_MAX) == IR_VECTOR_SEMANTICS_NONE);
    TargetCpuFeature const features[] = {TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX512BW,
        TARGET_CPU_FEATURE_X86_AVX512VBMI, TARGET_CPU_FEATURE_X86_AVX512VBMI2};
    for (u32 operation = 0; operation <= IR_SIMD_COUNT; operation += 1)
    {
        IrSimdShape shape = ir_simd_operation_shape((IrSimdOperation)operation);
        bool valid = operation < IR_SIMD_COUNT;
        if (valid)
        {
            VectorContractCase const* expected = vector_contract_cases + operation;
            BUSTER_TEST(arguments, expected->statement.length != 0);
            BUSTER_TEST(arguments, shape.operand_count && shape.semantic_class == IR_VECTOR_SEMANTICS_EXACT_X86_512);
            BUSTER_TEST(arguments, shape.predicate_operand_mask == expected->predicate_operands);
            BUSTER_TEST(arguments, shape.predicate_lane_count == expected->predicate_lanes);
            BUSTER_TEST(arguments, shape.predicate_result == expected->predicate_result);
        }
        else
        {
            BUSTER_TEST(arguments, shape.operand_count == 0 && shape.semantic_class == IR_VECTOR_SEMANTICS_NONE);
        }
        // Includes deliberately inconsistent masks: missing F/BW must refuse
        // even if VBMI/VBMI2 is set, and non-x86 targets cannot inherit x86 bits.
        for (u32 architecture = 0; architecture < CPU_ARCH_COUNT; architecture += 1)
        {
            for (u32 feature_mask = 0; feature_mask < 16; feature_mask += 1)
            {
                Target target = {.cpu_arch = (CpuArch)architecture, .os = OPERATING_SYSTEM_LINUX, .cpu_features_explicit = true};
                for (u32 feature = 0; feature < BUSTER_ARRAY_LENGTH(features); feature += 1)
                {
                    if (feature_mask & (1u << feature))
                    {
                        target.cpu_features = target_cpu_features_add(target.cpu_features, features[feature]);
                    }
                }
                u32 extension = valid ? vector_contract_cases[operation].extension : 0;
                bool supported = valid && architecture == CPU_ARCH_X86_64 && (feature_mask & 3) == 3 &&
                    (!extension || (feature_mask & (extension == 1 ? 4u : 8u)));
                BUSTER_TEST(arguments, ir_simd_operation_supported(target, (IrSimdOperation)operation) == supported);
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult vector_contract_test_emit(UnitTestArguments* arguments, IrProgram* program, Target target, u32 operation)
{
    UnitTestResult result = {0};
    IrModule* module = program->modules;
    bool exact = operation < IR_SIMD_COUNT;
    if (target.cpu_arch == CPU_ARCH_X86_64 || target.cpu_arch == CPU_ARCH_AARCH64)
    {
        bool supported = !exact || (target.cpu_arch == CPU_ARCH_X86_64 &&
            (target.cpu_model == CPU_MODEL_AMD_ZEN_5 || (target.cpu_model == CPU_MODEL_INTEL_SKYLAKE_AVX512 && !vector_contract_cases[operation].extension)));
        CodegenRegisterAllocatorMode modes[] = {CODEGEN_REGISTER_ALLOCATOR_NONE, CODEGEN_REGISTER_ALLOCATOR_MIR_STACK,
            CODEGEN_REGISTER_ALLOCATOR_FAST, CODEGEN_REGISTER_ALLOCATOR_QUALITY};
        for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
        {
            CodegenModule artifact = codegen_generate_canonical_module(arguments->arena, program, module, target,
                (CodegenModuleOptions){.register_allocator = (u8)modes[mode], .verify_invariants = true});
            if ((artifact.error == CODEGEN_ERROR_NONE) != supported)
            {
                arguments->show(arguments, S8("vector contract target {u32} mode {u32}, error {u32}, opcode {u32}\n"),
                    (u32)target.cpu_arch, (u32)modes[mode], (u32)artifact.error, (u32)artifact.failed_opcode);
            }
            BUSTER_TEST(arguments, (artifact.error == CODEGEN_ERROR_NONE) == supported);
            if (supported)
            {
                BUSTER_TEST(arguments, artifact.statistics.simd_operation_count == (u32)exact);
            }
            else
            {
                BUSTER_TEST(arguments, artifact.error == CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION);
                BUSTER_TEST(arguments, artifact.failed_opcode == IR_OPCODE_SIMD && artifact.failure_reason.length != 0);
            }
        }
        if (target.cpu_arch == CPU_ARCH_X86_64 && target.cpu_model == CPU_MODEL_AMD_ZEN_5)
        {
            LlvmBitcodeArtifact artifact = llvm_bitcode_emit(arguments->arena, program, module, 1);
            if (artifact.success == exact)
            {
                arguments->show(arguments, S8("vector contract LLVM operation {u32} error {u32}: {S8}\n"),
                    operation, (u32)artifact.error.code, artifact.error.message);
            }
            BUSTER_TEST(arguments, artifact.success != exact);
            if (exact)
            {
                BUSTER_TEST(arguments, artifact.error.code == LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION && artifact.error.opcode == IR_OPCODE_SIMD);
                BUSTER_TEST(arguments, artifact.bytes.length == 0 && artifact.error.message.length != 0);
            }
        }
    }
    else if (target.cpu_arch == CPU_ARCH_WASM64)
    {
        Wasm64Artifact artifact = wasm64_emit_program(arguments->arena, program);
        BUSTER_TEST(arguments, !artifact.success && artifact.error.code != WASM64_ERROR_NONE);
        BUSTER_TEST(arguments, artifact.bytes.length == 0 && artifact.error.message.length != 0);
    }
    else
    {
        EbpfArtifact artifact = ebpf_emit_program(arguments->arena, program);
        BUSTER_TEST(arguments, !artifact.success && artifact.error.code != EBPF_ERROR_NONE);
        BUSTER_TEST(arguments, artifact.bytes.length == 0 && artifact.error.message.length != 0);
    }
    return result;
}

UnitTestResult vector_contract_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = vector_contract_test_metadata(arguments);
    Target targets[] = {
        {.cpu_arch = CPU_ARCH_X86_64, .cpu_model = CPU_MODEL_AMD_ZEN_5, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_X86_64, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_X86_64, .cpu_model = CPU_MODEL_INTEL_SKYLAKE_AVX512, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_AARCH64, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_WASM64, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_FREESTANDING},
        {.cpu_arch = CPU_ARCH_BPFEL, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX},
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(targets); target_index += 1)
    {
        for (u32 operation = 0; operation <= IR_SIMD_COUNT; operation += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            UnitTestArguments local = *arguments;
            local.arena = temporary.arena;
            bool exact = operation < IR_SIMD_COUNT;
            String8 statement = exact ? vector_contract_cases[operation].statement : S8("*out = (*input + *other) ^ (*input == *other);");
            String8 element = exact ? S8("unsigned char") : S8("signed char");
            String8 source = string_format(local.arena, S8("typedef {S8} V __attribute__((vector_size(64)));\n"
                "void probe(V* out, V const* input, V const* other, unsigned long long mask, unsigned long long* bits) {{ {S8} }\n"), element, statement);
            Target target = targets[target_index];
            CPreprocessResult preprocess = c_preprocess(local.arena, source,
                (CPreprocessOptions){.target = target, .data_layout = target_data_layout(target)});
            CAnalysisResult analysis = c_parse(local.arena, preprocess);
            CIRLowerResult lowered = {0};
            if (!preprocess.error_count && !analysis.diagnostic_count)
            {
                lowered = c_lower_to_ir(local.arena, S8("vector-contract.c"), preprocess, analysis, target);
            }
            if (preprocess.error_count || analysis.diagnostic_count || lowered.diagnostic_count || !lowered.program)
            {
                arguments->show(arguments, S8("vector contract frontend target {u32} operation {u32} errors {u32}/{u32}/{u32}\n"),
                    target_index, operation, preprocess.error_count, analysis.diagnostic_count, lowered.diagnostic_count);
            }
            BUSTER_TEST(arguments, !preprocess.error_count && !analysis.diagnostic_count && !lowered.diagnostic_count && lowered.program);
            if (lowered.program && !lowered.diagnostic_count)
            {
                IrProgram* program = lowered.program;
                IrModule* module = program->modules;
                BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
                u32 exact_count = 0;
                IrType* mask_type = 0;
                for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
                {
                    IrFunction* function = module->functions + function_index;
                    for (u32 index = 0; index < function->instruction_count; index += 1)
                    {
                        IrInstruction* instruction = function->instructions + index;
                        if (instruction->opcode == IR_OPCODE_SIMD)
                        {
                            exact_count += 1;
                            BUSTER_TEST(arguments, instruction->simd_operation == operation);
                            IrSimdShape shape = ir_simd_operation_shape((IrSimdOperation)operation);
                            for (u32 operand = 0; operand < shape.operand_count; operand += 1)
                            {
                                if (shape.predicate_operand_mask & (1u << operand))
                                {
                                    IrType* type = ir_type_from_id(&program->types, function->values[instruction->operands[operand].value].canonical_type);
                                    BUSTER_TEST(arguments, type && type->kind == IR_TYPE_INTEGER && type->bit_width == 64);
                                    mask_type = type;
                                }
                            }
                            if (shape.predicate_result)
                            {
                                IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                                BUSTER_TEST(arguments, type && type->kind == IR_TYPE_INTEGER && type->bit_width == 64 && !type->is_signed);
                                mask_type = type;
                            }
                        }
                    }
                }
                BUSTER_TEST(arguments, exact_count == (u32)exact);
                if (mask_type)
                {
                    // A smaller C mask requires an explicit integer conversion;
                    // the exact canonical boundary always owns all 64 bits.
                    mask_type->bit_width = 32;
                    BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_OPERATION);
                    mask_type->bit_width = 64;
                    BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
                }
                UnitTestResult emitted = vector_contract_test_emit(&local, program, target, operation);
                result.test_count += emitted.test_count;
                result.succeeded_test_count += emitted.succeeded_test_count;
            }
            scratch_end(temporary);
        }
    }
    return result;
}
#endif
