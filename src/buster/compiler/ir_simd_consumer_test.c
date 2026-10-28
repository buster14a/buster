// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Explicit refusal boundaries for scalar-only rewriting/execution. Include after optimizers and Tier 1 selection.

static uint64_t ir_simd_native_boundary(void)
{
    return 732;
}

static int run_ir_simd_tests(int argc, char** argv)
{
    (void)argv;
    Arena input = arena_reserve(UINT64_C(1) << 24);
    Arena output = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 24);
    IrTestTally tally = { .seed = 732 };
    IrFunction function = { 0 };
    const IrSizes sizes = { .instruction_count = 2, .block_count = 1, .vector_constant_count = 1 };
    bool ready = argc == 0 && input.base != NULL && output.base != NULL && scratch.base != NULL && ir_create(&input, &sizes, &function) == IR_OK;
    ir_test_expect(&tally, ready, "SIMD consumer boundary storage and arguments");
    if (ready)
    {
        function.result_type = IR_V4I32;
        function.symbol_count = 2;
        function.block_offsets[1] = 2;
        ir_test_instruction(&function, 0, IR_VCONST, IR_V4I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&function, 1, IR_RET, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            IrFunction rewritten = { .instruction_count = 123 };
            IrCheck checked = scalar ? ssa_ir_promote_scalar(&function, &output, &scratch, &rewritten) : ssa_ir_promote(&function, &output, &scratch, &rewritten);
            ir_test_expect(&tally, checked.error == IR_ERROR_UNSUPPORTED && rewritten.instruction_count == 123 && output.length == 0 && scratch.length == 0,
                           "SSA promotion refuses SIMD transactionally until sparse-payload rewriting is supported");
            IrOptimizeResult optimized = { .function.instruction_count = 123 };
            IrOptimizeOptions options = ir_optimize_options();
            options.scalar = scalar != 0;
            checked = ir_optimize(&function, &output, &scratch, options, &optimized);
            ir_test_expect(&tally, checked.error == IR_ERROR_UNSUPPORTED && optimized.function.instruction_count == 123 && output.length == 0 && scratch.length == 0,
                           "SCCP/DCE/CFG refuses SIMD before scalar folding or payload reconstruction");
            IrGvnResult gvn = { .function.instruction_count = 123 };
            IrGvnOptions gvn_options = ir_gvn_options();
            gvn_options.scalar = scalar != 0;
            checked = ir_optimize_gvn(&function, &output, &scratch, gvn_options, &gvn);
            ir_test_expect(&tally, checked.error == IR_ERROR_UNSUPPORTED && gvn.function.instruction_count == 123 && output.length == 0 && scratch.length == 0,
                           "GVN refuses unsupported SIMD without merging distinct semantic shapes");
            IrLicmResult licm = { .function.instruction_count = 123 };
            IrLicmOptions licm_options = ir_licm_options();
            licm_options.scalar = scalar != 0;
            checked = ir_optimize_licm(&function, &output, &scratch, licm_options, &licm);
            ir_test_expect(&tally, checked.error == IR_ERROR_UNSUPPORTED && licm.function.instruction_count == 123 && output.length == 0 && scratch.length == 0,
                           "LICM refuses unsupported SIMD before changing memory/effect order");
            IrSroaResult sroa = { .function.instruction_count = 123 };
            const IrSroaOptions sroa_options = { .scalar = scalar != 0 };
            checked = ir_optimize_sroa(&function, &output, &scratch, sroa_options, &sroa);
            ir_test_expect(&tally, checked.error == IR_ERROR_UNSUPPORTED && sroa.function.instruction_count == 123 && output.length == 0 && scratch.length == 0,
                           "SROA refuses unsupported SIMD rather than splitting wide values as unrelated scalars");
            const uint32_t symbols[1] = { 1 };
            const uint8_t flags[1] = { BUSTER_IR_INLINE_INTERNAL };
            const IrInlineUnit unit = { .functions = &function, .symbols = symbols, .flags = flags, .count = 1, .symbol_count = 2 };
            IrInlineResult inlined = { .function_count = 123 };
            IrInlineOptions inline_options = ir_inline_options();
            inline_options.scalar = scalar != 0;
            checked = ir_optimize_inline(&unit, &output, &scratch, inline_options, &inlined, NULL);
            ir_test_expect(&tally, checked.error == IR_ERROR_UNSUPPORTED && inlined.function_count == 123 && output.length == 0 && scratch.length == 0,
                           "inlining refuses unsupported SIMD metadata before cloning a unit");
            IrSimdValue* original_payload = function.vector_constants;
            IrSimdValue* borrowed = (IrSimdValue*)output.base;
            *borrowed = (IrSimdValue){ .words = { UINT64_C(0xfedcba9876543210) } };
            const IrSimdValue saved_payload = *borrowed;
            function.vector_constants = borrowed;
            checked = ir_optimize_inline(&unit, &output, &scratch, inline_options, &inlined, NULL);
            ir_test_expect(&tally, checked.error == IR_ERROR_STRUCTURE && memcmp(borrowed, &saved_payload, sizeof(saved_payload)) == 0 &&
                           inlined.function_count == 123 && output.length == 0 && scratch.length == 0,
                           "inliner preflight preserves a vector payload borrowed from the unused output tail before any audit allocation");
            function.vector_constants = original_payload;
            const uint32_t original_count = function.vector_constant_count;
            function.vector_constant_count = BUSTER_IR_INLINE_MAX_ITEMS + 1;
            checked = ir_optimize_inline(&unit, &output, &scratch, inline_options, &inlined, NULL);
            ir_test_expect(&tally, checked.error == IR_ERROR_LIMIT && inlined.function_count == 123 && output.length == 0 && scratch.length == 0,
                           "inliner checks the sparse vector payload bound before reading a declared oversized borrowed span");
            function.vector_constant_count = original_count;
            Tier1SelectPlan selected;
            memset(&selected, 0xa5, sizeof(selected));
            const Tier1SelectPlan before = selected;
            uint32_t failed = BUSTER_IR_NONE;
            const Tier1SelectError selection = tier1_select_plan(&function, NULL, &output, &scratch, scalar == 0, &selected, &failed);
            ir_test_expect(&tally, selection == TIER1_SELECT_ERROR_UNSUPPORTED && memcmp(&selected, &before, sizeof(selected)) == 0 && output.length == 0 && scratch.length == 0,
                           "native Tier 1 refuses SIMD without using the host ISA to authorize output instructions");
        }
        const IrInterpreterFunction code = { .symbol = 1, .function = &function };
        const IrInterpreterProgram program = { .functions = &code, .function_count = 1 };
        const InterpreterResult interpreted = ir_interpret(&program, 1, NULL, 0, NULL, &output);
        ir_test_expect(&tally, interpreted.status == INTERPRETER_UNSUPPORTED && output.length == 0,
                       "ordinary two-word interpreter explicitly refuses wide vector values and restores its arena");
        const InterpreterNativeBinding native = { .address = (void*)ir_simd_native_boundary };
        const InterpreterValue argument = { .low = 42 };
        for (uint16_t type = IR_V4I32; type < IR_TYPE_COUNT; type += 1)
        {
            for (uint32_t parameter = 0; parameter < 2; parameter += 1)
            {
                const InterpreterNativeRequest request = {
                    .binding = &native, .types = &type, .arguments = &argument,
                    .argument_count = parameter, .named_count = parameter, .result_type = parameter != 0 ? IR_I64 : type,
                };
                InterpreterValue native_result = { .low = 1234, .high = 5678 };
                const bool invoked = interpreter_native_invoke(&request, &native_result);
                ir_test_expect(&tally, !invoked && native_result.low == 1234 && native_result.high == 5678,
                               "the explicit native ABI boundary refuses vector/mask arguments and results before invoking scalar host code");
            }
        }
    }
    fprintf(stderr, "IR SIMD consumers: %" PRIu64 " checks, %" PRIu64 " failures\n", tally.checks, tally.failures);
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
    return tally.failures != 0;
}
