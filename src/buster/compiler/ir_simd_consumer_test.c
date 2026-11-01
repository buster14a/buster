// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Explicit refusal boundaries for scalar-only rewriting/execution. Include after optimizers and Tier 1 selection.

static uint64_t ir_simd_native_boundary(void)
{
    return 732;
}

// ir_function_has_simd's vector path against its scalar twin and the expected answer: an operation and a type column of
// every length to 200, each ending at a PROT_NONE page, with no vector or mask in them, then with one at each place (a
// vector type, and each vector operation on a scalar type, which the scan must take too).
static void ir_simd_scan_test(IrTestTally* tally)
{
    const long page_result = sysconf(_SC_PAGESIZE);
    const uint64_t page = page_result > 0 ? (uint64_t)page_result : 4096;
    uint8_t* columns[2] = { MAP_FAILED, MAP_FAILED };
    bool ready = true;
    for (uint32_t k = 0; k < 2; k += 1)
    {
        columns[k] = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = ready && columns[k] != MAP_FAILED && mprotect(columns[k] + page, page, PROT_NONE) == 0;
    }
    ir_test_expect(tally, ready, "guarded SIMD scan columns");
    static const uint8_t markers[4][2] = {
        { IR_ADD, IR_V8I32 },
        { IR_VCONST, IR_I32 },
        { IR_VCMP, IR_I32 },
        { IR_VSELECT, IR_I32 },
    };
    for (uint32_t count = 0; ready && count <= 200; count += 1)
    {
        uint8_t* op = columns[0] + page - count;
        uint8_t* type = columns[1] + page - count;
        memset(op, IR_ADD, count);
        memset(type, IR_I32, count);
        const IrFunction function = { .op = op, .type = type, .instruction_count = count };
        ir_test_expect(tally, !ir_function_has_simd(&function) && !ir_function_has_simd_scalar(&function), "a scalar function's columns hold no vector or mask");
        for (uint32_t place = 0; place < count; place += 1)
        {
            for (uint32_t m = 0; m < 4; m += 1)
            {
                op[place] = markers[m][0];
                type[place] = markers[m][1];
                ir_test_expect(tally, ir_function_has_simd(&function) && ir_function_has_simd_scalar(&function), "a vector type or operation at any place, the tail's last among them");
                op[place] = IR_ADD;
                type[place] = IR_I32;
            }
        }
    }
    for (uint32_t k = 0; k < 2; k += 1)
    {
        if (columns[k] != MAP_FAILED)
        {
            munmap(columns[k], page * 2);
        }
    }
}

// A function whose only SIMD values are masks (#179): its masks loaded, complemented and stored. SSA promotion and SCCP,
// dead code and CFG cleanup take it (ir_function_has_vectors), by both paths, into a function that verifies; GVN, LICM,
// SROA and the inliner, which don't model masks, still refuse it transactionally.
static void ir_simd_mask_consumer_test(IrTestTally* tally, Arena* input, Arena* output, Arena* scratch)
{
    static const uint8_t masks[4] = { IR_M8, IR_M16, IR_M32, IR_M64 };
    for (uint32_t w = 0; w < 4; w += 1)
    {
        const uint64_t mark = input->length;
        IrFunction function = { 0 };
        const IrSizes sizes = { .instruction_count = 5, .block_count = 1, .parameter_count = 1 };
        const bool ready = ir_create(input, &sizes, &function) == IR_OK;
        ir_test_expect(tally, ready, "a mask consumer fixture allocates");
        if (ready)
        {
            const IrType mask = (IrType)masks[w];
            function.result_type = IR_VOID;
            function.parameter_types[0] = IR_PTR;
            function.block_offsets[1] = 5;
            ir_test_instruction(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 1, IR_LOAD, mask, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 2, IR_NOT, mask, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 3, IR_STORE, IR_VOID, 0, 2, BUSTER_IR_NONE);
            ir_test_instruction(&function, 4, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_expect(tally, ir_function_has_simd(&function) && !ir_function_has_vectors(&function) && !ir_function_has_vectors_path(&function, false),
                           "a function of masks alone has SIMD values but no vectors, by both paths");
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                IrFunction promoted = { 0 };
                IrCheck checked = scalar ? ssa_ir_promote_scalar(&function, output, scratch, &promoted) : ssa_ir_promote(&function, output, scratch, &promoted);
                ir_test_expect(tally, checked.error == IR_OK && promoted.instruction_count == function.instruction_count && scratch->length == 0,
                               "SSA promotion takes a function of masks alone");
                IrOptimizeResult optimized = { 0 };
                IrOptimizeOptions options = ir_optimize_options();
                options.path = scalar != 0 ? BUSTER_IR_OPTIMIZE_SCALAR : BUSTER_IR_OPTIMIZE_VECTOR;
                checked = ir_optimize(&function, output, scratch, options, &optimized);
                ir_test_expect(tally, checked.error == IR_OK && ir_verify_scalar(&optimized.function, scratch, NULL).error == IR_OK && scratch->length == 0,
                               "SCCP, dead code and CFG cleanup take a function of masks alone, into a function that verifies");
                arena_pop_to(output, 0);
                IrGvnResult gvn = { .function.instruction_count = 123 };
                IrGvnOptions gvn_options = ir_gvn_options();
                gvn_options.scalar = scalar != 0;
                checked = ir_optimize_gvn(&function, output, scratch, gvn_options, &gvn);
                ir_test_expect(tally, checked.error == IR_ERROR_UNSUPPORTED && gvn.function.instruction_count == 123 && output->length == 0 && scratch->length == 0, "GVN refuses masks");
                IrLicmResult licm = { .function.instruction_count = 123 };
                IrLicmOptions licm_options = ir_licm_options();
                licm_options.scalar = scalar != 0;
                checked = ir_optimize_licm(&function, output, scratch, licm_options, &licm);
                ir_test_expect(tally, checked.error == IR_ERROR_UNSUPPORTED && licm.function.instruction_count == 123 && output->length == 0 && scratch->length == 0, "LICM refuses masks");
                IrSroaResult sroa = { .function.instruction_count = 123 };
                const IrSroaOptions sroa_options = { .scalar = scalar != 0 };
                checked = ir_optimize_sroa(&function, output, scratch, sroa_options, &sroa);
                ir_test_expect(tally, checked.error == IR_ERROR_UNSUPPORTED && sroa.function.instruction_count == 123 && output->length == 0 && scratch->length == 0, "SROA refuses masks");
                const uint32_t symbols[1] = { 1 };
                const uint8_t flags[1] = { BUSTER_IR_INLINE_INTERNAL };
                function.symbol_count = 2;
                const IrInlineUnit unit = { .functions = &function, .symbols = symbols, .flags = flags, .count = 1, .symbol_count = 2 };
                IrInlineResult inlined = { .function_count = 123 };
                IrInlineOptions inline_options = ir_inline_options();
                inline_options.scalar = scalar != 0;
                checked = ir_optimize_inline(&unit, output, scratch, inline_options, &inlined, NULL);
                ir_test_expect(tally, checked.error == IR_ERROR_UNSUPPORTED && inlined.function_count == 123 && output->length == 0 && scratch->length == 0, "the inliner refuses masks");
                function.symbol_count = 0;
            }
        }
        arena_pop_to(input, mark);
    }
}

static int run_ir_simd_tests(int argc, char** argv)
{
    (void)argv;
    Arena input = arena_reserve(UINT64_C(1) << 24);
    Arena output = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 24);
    IrTestTally tally = { .seed = 732 };
    ir_simd_typed_test(&tally);
    ir_simd_scan_test(&tally);
    if (input.base != NULL && output.base != NULL && scratch.base != NULL)
    {
        ir_simd_mask_consumer_test(&tally, &input, &output, &scratch);
    }
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
            options.path = scalar != 0 ? BUSTER_IR_OPTIMIZE_SCALAR : BUSTER_IR_OPTIMIZE_VECTOR;
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
                       "ordinary interpreter explicitly refuses first-class vector ABI results and restores its arena");
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
