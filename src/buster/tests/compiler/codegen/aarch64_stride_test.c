#include <buster/tests/compiler/codegen/aarch64_stride_test.h>
#if BUSTER_INCLUDE_TESTS

// A variable index scales by the element stride, and the stride is an
// arbitrary 64-bit constant rather than a movz immediate. The canonical
// AArch64 backend used to reject any element of 64 KiB or more outright,
// which made an unrelated struct growing past 2^16 a compiler failure with
// no aarch64 code involved. These cases pin the movz/movk materialization
// at and above that boundary, and keep the sub-boundary shape a single
// movz so nothing that already compiled changed a byte.

typedef struct Aarch64StrideCase Aarch64StrideCase;
struct Aarch64StrideCase
{
    u64 stride;
    // Words the stride costs to materialize: one movz plus a movk per
    // further halfword that carries a bit.
    u32 materialization_words;
};

// Tracks the movz/movk chain per general-purpose register so a scan can
// answer what a register holds at any later word.
typedef struct Aarch64StrideRegisters Aarch64StrideRegisters;
struct Aarch64StrideRegisters
{
    u64 values[32];
    u32 written[32];
};

BUSTER_GLOBAL_LOCAL IrFunction* aarch64_stride_function_find(IrModule* module, String8 name)
{
    for (u32 index = 0; index < module->function_count; index += 1)
    {
        if (string_equal(module->functions[index].name, name))
        {
            return module->functions + index;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL CodegenFunctionDescriptor* aarch64_stride_descriptor_find(CodegenModule* module, IrSymbolId symbol)
{
    for (u32 index = 0; index < module->function_count; index += 1)
    {
        if (module->functions[index].symbol.value == symbol.value)
        {
            return module->functions + index;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool aarch64_stride_move_immediate(u32 word)
{
    return (word & 0xff800000) == 0xd2800000 || (word & 0xff800000) == 0x92800000 || (word & 0xff800000) == 0xf2800000;
}

// Applies one 64-bit movz/movn/movk word and counts how many words that
// register has taken since its last movz/movn, so a caller can check the
// sequence length as well as the value it builds. Only move-immediate words
// reach here: nothing else writes the registers these scans follow.
BUSTER_GLOBAL_LOCAL void aarch64_stride_apply_move_immediate(Aarch64StrideRegisters* registers, u32 word)
{
    u32 target = word & 31;
    u32 shift = ((word >> 21) & 3) * 16;
    u64 halfword = (word >> 5) & 0xffff;
    if ((word & 0xff800000) == 0xd2800000)
    {
        registers->values[target] = halfword << shift;
        registers->written[target] = 1;
    }
    else if ((word & 0xff800000) == 0x92800000)
    {
        registers->values[target] = ~(halfword << shift);
        registers->written[target] = 1;
    }
    else
    {
        registers->values[target] = (registers->values[target] & ~(UINT64_C(0xffff) << shift)) | (halfword << shift);
        registers->written[target] += 1;
    }
}

typedef struct Aarch64StrideScan Aarch64StrideScan;
struct Aarch64StrideScan
{
    // Scaling multiplies found in the function, and how many of them scaled
    // by exactly the expected stride with the expected word count.
    u32 scale_count;
    u32 matching_scale_count;
    bool add_follows_every_scale;
};

// Walks the canonical backend's scaling idiom: the stride lands in x11,
// `mul x10, x10, x11` scales the index, and `add x9, x9, x10` reaches the
// element. Both fixed words are the canonical emission's own spelling.
BUSTER_GLOBAL_LOCAL Aarch64StrideScan aarch64_stride_scan_canonical(u8 const* code, u64 size, Aarch64StrideCase expected)
{
    Aarch64StrideScan scan = {.add_follows_every_scale = true};
    Aarch64StrideRegisters registers = {0};
    for (u64 offset = 0; offset + 4 <= size; offset += 4)
    {
        u32 word = (u32)code[offset] | ((u32)code[offset + 1] << 8) | ((u32)code[offset + 2] << 16) | ((u32)code[offset + 3] << 24);
        if (word == 0x9b0b7d4a)
        {
            scan.scale_count += 1;
            scan.matching_scale_count +=
                registers.written[11] == expected.materialization_words && registers.values[11] == expected.stride;
            u64 add_offset = offset + 4;
            u32 add_word = 0;
            if (add_offset + 4 <= size)
            {
                add_word = (u32)code[add_offset] | ((u32)code[add_offset + 1] << 8) | ((u32)code[add_offset + 2] << 16) |
                           ((u32)code[add_offset + 3] << 24);
            }
            scan.add_follows_every_scale = scan.add_follows_every_scale && add_word == 0x8b0a0129;
            continue;
        }
        if (aarch64_stride_move_immediate(word))
        {
            aarch64_stride_apply_move_immediate(&registers, word);
        }
    }
    return scan;
}

// The machine-IR selector allocates its own registers and schedules its own
// rows, so neither the register holding the stride nor the position of the
// multiply is fixed. Assert only what is: the stride reaches some register.
BUSTER_GLOBAL_LOCAL bool aarch64_stride_machine_materializes(u8 const* code, u32 size, u64 stride)
{
    Aarch64StrideRegisters registers = {0};
    for (u32 offset = 0; offset + 4 <= size; offset += 4)
    {
        u32 word = (u32)code[offset] | ((u32)code[offset + 1] << 8) | ((u32)code[offset + 2] << 16) | ((u32)code[offset + 3] << 24);
        if (!aarch64_stride_move_immediate(word))
        {
            continue;
        }
        aarch64_stride_apply_move_immediate(&registers, word);
        u32 target = word & 31;
        if (registers.written[target] && registers.values[target] == stride)
        {
            return true;
        }
    }
    return false;
}

UnitTestResult aarch64_stride_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Target aarch64_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_LINUX,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_singleton(TARGET_CPU_FEATURE_AARCH64_NEON),
    };
    Aarch64StrideCase stride_cases[] = {
        // Below the old 2^16 ceiling: still exactly one movz.
        {.stride = 8, .materialization_words = 1},
        {.stride = 32768, .materialization_words = 1},
        {.stride = 65520, .materialization_words = 1},
        // At and above it, where the backend used to fail outright.
        {.stride = 65536, .materialization_words = 1},
        {.stride = 65544, .materialization_words = 2},
        {.stride = BUSTER_MB(1), .materialization_words = 1},
        {.stride = BUSTER_MB(1) + 24, .materialization_words = 2},
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(stride_cases); case_index += 1)
    {
        Aarch64StrideCase stride_case = stride_cases[case_index];
        // Doubled braces are the formatter's escape, not C.
        String8 source = string_format(arguments->arena,
                                       S8("typedef struct StrideElement {{ unsigned char bytes[{u64}]; }} StrideElement;\n"
                                          "StrideElement* stride_index(StrideElement* base, unsigned long long index)\n"
                                          "{{\n"
                                          "    return base + index;\n"
                                          "}}\n"),
                                       stride_case.stride);
        CPreprocessResult tokens = c_preprocess(arguments->arena, source, (CPreprocessOptions){0});
        CParseResult parse = c_parse(arguments->arena, tokens);
        CIRLowerResult lowered = c_lower_to_ir(arguments->arena, S8("aarch64-stride.c"), tokens, parse, aarch64_target);
        BUSTER_TEST(arguments, tokens.error_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.diagnostic_count == 0);
        if (!lowered.program)
        {
            continue;
        }
        IrModule* module = lowered.program->modules;
        IrFunction* stride_index = aarch64_stride_function_find(module, S8("stride_index"));
        BUSTER_TEST(arguments, stride_index != 0);
        if (!stride_index)
        {
            continue;
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
        CodegenModule generated =
            codegen_generate_canonical_module(arguments->arena, lowered.program, module, aarch64_target, (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, generated.error == CODEGEN_ERROR_NONE);
        CodegenFunctionDescriptor* descriptor =
            generated.error == CODEGEN_ERROR_NONE ? aarch64_stride_descriptor_find(&generated, stride_index->symbol) : 0;
        BUSTER_TEST(arguments, descriptor != 0);
        if (descriptor && (u64)descriptor->code_offset + descriptor->code_size <= generated.code.length)
        {
            Aarch64StrideScan scan =
                aarch64_stride_scan_canonical(generated.code.pointer + descriptor->code_offset, descriptor->code_size, stride_case);
            BUSTER_TEST(arguments, scan.scale_count >= 1);
            BUSTER_TEST(arguments, scan.matching_scale_count == scan.scale_count);
            BUSTER_TEST(arguments, scan.add_follows_every_scale);
        }
        // The machine-IR selector reaches the same stride through its own
        // immediate materialization; keep both AArch64 paths covered.
        MachineSelectResult selected = machine_select_canonical_function(arguments->arena, lowered.program, stride_index, aarch64_target);
        BUSTER_TEST(arguments, selected.supported);
        if (selected.supported && machine_verify_function(&selected.function).error == MACHINE_VERIFY_NONE)
        {
            MachineStackPlacement placement = machine_stack_placement_build(arguments->arena, &selected.function);
            BUSTER_TEST(arguments, placement.valid);
            if (placement.valid)
            {
                MachineEncodeResult encoded = machine_encode_aarch64(arguments->arena, &selected.function, &placement);
                BUSTER_TEST(arguments, encoded.valid);
                BUSTER_TEST(arguments, encoded.valid && aarch64_stride_machine_materializes(encoded.bytes, encoded.byte_count, stride_case.stride));
            }
        }
    }
    return result;
}

#endif
