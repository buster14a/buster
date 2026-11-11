// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1450's lane optimization fixtures. The semantic oracle is ir_simd_model.c, independently of the optimizer's
// batched transfers. Complete functions also execute through the shared interpreter before and after each path.
// Included by ir_optimize_test.c; the GVN/LICM fixtures reuse the interpreter comparison helper below.

static InterpreterResult ir_optimize_test_simd_run(const IrFunction* f, const InterpreterValue* arguments, uint32_t count, Arena* scratch)
{
    const IrInterpreterFunction binding = {
        .symbol = 1,
        .function = f,
    };
    const IrInterpreterProgram program = {
        .functions = &binding,
        .function_count = 1,
    };
    const InterpreterResult result = ir_interpret(&program, 1, arguments, count, NULL, scratch);
    return result;
}

static void ir_optimize_test_simd_semantics(IrOptimizeTest* test, const IrFunction* before, const IrFunction* after, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    for (uint32_t trial = 0; trial < (before->parameter_count != 0 ? 2u : 1u); trial += 1)
    {
        const InterpreterValue argument = { .low = trial };
        const InterpreterResult a = ir_optimize_test_simd_run(before, &argument, before->parameter_count, scratch);
        const InterpreterResult b = ir_optimize_test_simd_run(after, &argument, before->parameter_count, scratch);
        ir_optimize_test_expect(test, a.status == INTERPRETER_OK && b.status == INTERPRETER_OK &&
                                a.value.low == b.value.low && a.value.high == b.value.high && scratch->length == mark,
                                "SIMD optimization preserves all interpreted result bits and scratch");
    }
}

static void ir_optimize_test_simd_rewrite(IrOptimizeTest* test, const IrFunction* f, Arena* vector, Arena* scalar, Arena* scratch, uint32_t passes)
{
    const uint64_t vm = vector->length;
    const uint64_t sm = scalar->length;
    IrOptimizeOptions options = ir_optimize_options();
    options.passes = passes;
    options.path = BUSTER_IR_OPTIMIZE_VECTOR;
    IrOptimizeResult a = { 0 };
    IrOptimizeResult b = { 0 };
    const IrCheck ac = ir_optimize(f, vector, scratch, options, &a);
    options.path = BUSTER_IR_OPTIMIZE_SCALAR;
    const IrCheck bc = ir_optimize(f, scalar, scratch, options, &b);
    ir_optimize_test_expect(test, ac.error == IR_OK && bc.error == IR_OK, "both SCCP paths accept SIMD fixture");
    if (ac.error == IR_OK && bc.error == IR_OK)
    {
        ir_optimize_test_expect(test, ir_optimize_test_equal(&a.function, &b.function), "SIMD SCCP twins preserve exact wide constants and rows");
        ir_optimize_test_simd_semantics(test, f, &a.function, scratch);
        ir_optimize_test_simd_semantics(test, f, &b.function, scratch);
        uint32_t before_effects = 0;
        uint32_t after_effects = 0;
        for (uint32_t i = 0; i < f->instruction_count; i += 1)
        {
            before_effects += ir_optimize_simd_effect(f, i);
        }
        for (uint32_t i = 0; i < a.function.instruction_count; i += 1)
        {
            after_effects += ir_optimize_simd_effect(&a.function, i);
        }
        ir_optimize_test_expect(test, after_effects == before_effects, "SIMD floating operations retain their environment effects");
        arena_pop_to(scalar, sm);
        options.path = BUSTER_IR_OPTIMIZE_VECTOR;
        options.sweep_limit = 0;
        const IrCheck fallback = ir_optimize(f, scalar, scratch, options, &b);
        ir_optimize_test_expect(test, fallback.error == IR_OK && ir_optimize_test_equal(&a.function, &b.function), "SIMD SCCP worklist completion matches converged sweeps");
    }
    arena_pop_to(vector, vm);
    arena_pop_to(scalar, sm);
}

static bool ir_optimize_test_simd_function(Arena* input, uint32_t type, uint32_t lane, uint32_t op, uint32_t predicate,
                                         const IrSimdValue operands[3], const IrSimdValue* expected, IrFunction* f)
{
    const uint32_t lane_mask = ir_lane_mask(type, lane);
    const uint32_t result_mask = op == IR_VCMP ? lane_mask : ir_lane_mask(type, IR_SIMD_LANE_I8);
    const uint32_t count = op == IR_VCMP ? 5 : 7;
    const IrSizes sizes = {
        .instruction_count = count,
        .block_count = 1,
        .constant_count = 1,
        .vector_constant_count = 3,
    };
    const bool created = ir_create(input, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = result_mask;
        f->block_offsets[1] = count;
        f->constants[0].low = op == IR_VSELECT ? operands[0].words[0] : 0;
        f->vector_constants[0] = operands[op == IR_VSELECT ? 1 : 0];
        f->vector_constants[1] = operands[op == IR_VSELECT ? 2 : 1];
        f->vector_constants[2] = op == IR_VCMP ? (IrSimdValue){ 0 } : *expected;
        ir_optimize_test_instruction(f, 0, IR_VCONST, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_VCONST, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, lane_mask, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, op, op == IR_VCMP ? lane_mask : type, op == IR_VSELECT ? 2 : 0,
                                     op == IR_VSELECT ? 0 : op == IR_NOT ? BUSTER_IR_NONE : 1,
                                     op == IR_VCMP ? predicate : op == IR_VSELECT ? 1 : BUSTER_IR_NONE);
        f->flags[3] = lane;
        if (op == IR_VCMP)
        {
            ir_optimize_test_instruction(f, 4, IR_RET, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        else
        {
            ir_optimize_test_instruction(f, 4, IR_VCONST, type, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 5, IR_VCMP, result_mask, 3, 4, IR_EQ);
            f->flags[5] = IR_SIMD_LANE_I8;
            ir_optimize_test_instruction(f, 6, IR_RET, IR_VOID, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
    }
    return created;
}

// The model's lanes under one MXCSR. Strict floating semantics keep its arithmetic between the two writes.
static __attribute__((noinline)) bool ir_optimize_test_simd_model_under(uint32_t mxcsr, const IrSimdAction* action, const IrSimdValue* const arguments[3], IrSimdValue* value)
{
#pragma STDC FENV_ACCESS ON
    const uint32_t saved = _mm_getcsr();
    _mm_setcsr(mxcsr);
    const bool result = ir_simd_evaluate(action, arguments, NULL, NULL, 0, value);
    _mm_setcsr(saved);
    return result;
}

// The folding rule from the model alone: a floating operation's lanes fold where they give the default
// environment's bits rounding down, up and toward zero and under FTZ with DAZ, and no lane without a NaN operand has
// a subnormal one (whose DAZ reading may agree by chance, as a subnormal times zero's does).
static bool ir_optimize_test_simd_environment_free(const IrSimdAction* action, const IrSimdValue* const arguments[3], const IrSimdValue* expected, uint32_t lanes, uint32_t width)
{
    static const uint32_t controls[] = {
        UINT32_C(0x3f80),
        UINT32_C(0x5f80),
        UINT32_C(0x7f80),
        UINT32_C(0x9fc0),
    };
    const uint64_t exponent = width == 32 ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    const uint64_t fraction = width == 32 ? UINT64_C(0x007fffff) : UINT64_C(0x000fffffffffffff);
    bool result = true;
    for (uint32_t control = 0; control < sizeof(controls) / sizeof(controls[0]); control += 1)
    {
        IrSimdValue value = { 0 };
        result = result && ir_optimize_test_simd_model_under(controls[control], action, arguments, &value) && memcmp(&value, expected, sizeof(value)) == 0;
    }
    for (uint32_t lane = 0; lane < lanes; lane += 1)
    {
        const uint64_t a = ir_simd_lane_read(arguments[0], width, lane);
        const uint64_t b = ir_simd_lane_read(arguments[1], width, lane);
        const bool nan = ((a & exponent) == exponent && (a & fraction) != 0) || ((b & exponent) == exponent && (b & fraction) != 0);
        const bool subnormal = ((a & exponent) == 0 && (a & fraction) != 0) || ((b & exponent) == 0 && (b & fraction) != 0);
        result = result && (nan || !subnormal);
    }
    return result;
}

static void ir_optimize_test_simd_lanes(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* scalar, Arena* scratch)
{
    static const uint8_t operations[] = {
        IR_ADD, IR_SUB, IR_MUL, IR_AND, IR_OR, IR_XOR, IR_NOT, IR_SHL, IR_LSHR, IR_ASHR,
        IR_FADD, IR_FSUB, IR_FMUL, IR_FDIV, IR_VCMP, IR_VSELECT,
    };
    static const uint8_t model[] = {
        IR_SIMD_ADD, IR_SIMD_SUB, IR_SIMD_MUL, IR_SIMD_AND, IR_SIMD_OR, IR_SIMD_XOR, IR_SIMD_NOT,
        IR_SIMD_SHL, IR_SIMD_LSHR, IR_SIMD_ASHR, IR_SIMD_FADD, IR_SIMD_FSUB, IR_SIMD_FMUL, IR_SIMD_FDIV,
        IR_SIMD_COMPARE, IR_SIMD_SELECT,
    };
    static const uint32_t singles[] = {
        0, UINT32_C(0x80000000), UINT32_C(0x3f800000), UINT32_C(0xbf800000), UINT32_C(0x7f800000), UINT32_C(0xff800000),
        UINT32_C(0x7fc00123), UINT32_C(0xffc00456), UINT32_C(0x7f800789), 1, UINT32_C(0x007fffff), UINT32_C(0x00800000),
        UINT32_C(0x7f7fffff), UINT32_C(0x3eaaaaab), UINT32_C(0x3f000000), UINT32_C(0x40400000),
    };
    static const uint64_t doubles[] = {
        0, UINT64_C(0x8000000000000000), UINT64_C(0x3ff0000000000000), UINT64_C(0xbff0000000000000),
        UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8000000000123), UINT64_C(0xfff8000000000456),
        UINT64_C(0x7ff0000000000789), 1, UINT64_C(0x000fffffffffffff), UINT64_C(0x0010000000000000),
        UINT64_C(0x7fefffffffffffff), UINT64_C(0x3fd5555555555555), UINT64_C(0x3fe0000000000000), UINT64_C(0x4008000000000000),
    };
    // Odd trials: distinct powers of two, an infinity and NaNs, whose sums, differences, products and quotients are
    // exact and never cancel, so that whole vectors fold; odd lanes negate the second operand.
    static const uint32_t exact_singles[] = {
        UINT32_C(0x3f800000), UINT32_C(0x40000000), UINT32_C(0x3f000000), UINT32_C(0x3e800000), UINT32_C(0x40800000), UINT32_C(0x41000000),
        UINT32_C(0x41800000), UINT32_C(0x3e000000), UINT32_C(0x42000000), UINT32_C(0x42800000), UINT32_C(0x3d800000), UINT32_C(0x7f800000),
        UINT32_C(0x7fc00123), UINT32_C(0x7f800789), UINT32_C(0x43000000), UINT32_C(0x43800000),
    };
    static const uint64_t exact_doubles[] = {
        UINT64_C(0x3ff0000000000000), UINT64_C(0x4000000000000000), UINT64_C(0x3fe0000000000000), UINT64_C(0x3fd0000000000000),
        UINT64_C(0x4010000000000000), UINT64_C(0x4020000000000000), UINT64_C(0x4030000000000000), UINT64_C(0x3fc0000000000000),
        UINT64_C(0x4040000000000000), UINT64_C(0x4050000000000000), UINT64_C(0x3fb0000000000000), UINT64_C(0x7ff0000000000000),
        UINT64_C(0x7ff8000000000123), UINT64_C(0x7ff0000000000789), UINT64_C(0x4060000000000000), UINT64_C(0x4070000000000000),
    };
    uint32_t floating_folds[5] = { 0 };
    uint32_t floating_refusals[5] = { 0 };
    const uint32_t saved_mxcsr = _mm_getcsr();
    for (uint32_t type = IR_V4I32; type <= IR_V16I32; type += 1)
    {
        const uint32_t bits = ir_type_bits[type];
        for (uint32_t lane = 0; lane < IR_SIMD_LANE_COUNT; lane += 1)
        {
            const uint32_t width = ir_simd_lane_bits((IrSimdLane)lane);
            const uint32_t mask_type = ir_lane_mask(type, lane);
            for (uint32_t operation = 0; operation < sizeof(operations); operation += 1)
            {
                const uint32_t op = operations[operation];
                IrSimdAction action = {
                    .operation = (IrSimdOperation)model[operation],
                    .result = ir_simd_shape(op == IR_VCMP ? mask_type : type),
                    .operands = {
                        ir_simd_shape(op == IR_VSELECT ? mask_type : type),
                        op == IR_NOT ? IR_SIMD_NONE : ir_simd_shape(type),
                        op == IR_VSELECT ? ir_simd_shape(type) : IR_SIMD_NONE,
                    },
                    .lane = (IrSimdLane)lane,
                };
                if (ir_simd_action_valid(&action))
                {
                    for (uint32_t trial = 0; trial < 16; trial += 1)
                    {
                        const uint64_t mark = input->length;
                        IrSimdValue operands[3] = { 0 };
                        for (uint32_t element = 0; element < bits / width; element += 1)
                        {
                            const uint32_t at = (trial + element) & 15;
                            uint64_t a = ir_optimize_test_random(test);
                            uint64_t b = ir_optimize_test_random(test);
                            if (lane >= IR_SIMD_LANE_F32 && (trial & 1) != 0)
                            {
                                const uint64_t negative = (element & 1) != 0 ? UINT64_C(1) << (width - 1) : 0;
                                a = width == 32 ? exact_singles[at] : exact_doubles[at];
                                b = (width == 32 ? exact_singles[(at + trial) & 15] : exact_doubles[(at + trial) & 15]) ^ negative;
                            }
                            else if (lane >= IR_SIMD_LANE_F32)
                            {
                                a = width == 32 ? singles[at] : doubles[at];
                                b = width == 32 ? singles[(at + trial) & 15] : doubles[(at + trial) & 15];
                            }
                            else if (op == IR_SHL || op == IR_LSHR || op == IR_ASHR)
                            {
                                const uint64_t counts[] = { 0, width - 1, width, width + 1, UINT64_MAX, 1 };
                                b = counts[at % 6];
                            }
                            ir_simd_lane_write(operands + (op == IR_VSELECT ? 1 : 0), width, element, a);
                            ir_simd_lane_write(operands + (op == IR_VSELECT ? 2 : 1), width, element, b);
                        }
                        if (op == IR_VSELECT)
                        {
                            operands[0].words[0] = ir_optimize_test_random(test) & (UINT64_MAX >> (64 - ir_type_bits[mask_type]));
                        }
                        action.predicate = op == IR_VCMP ? (IrSimdPredicate)(trial % (lane >= IR_SIMD_LANE_F32 ? 6 : IR_PREDICATE_COUNT)) : IR_SIMD_EQ;
                        const IrSimdValue* arguments[3] = { operands, operands + 1, operands + 2 };
                        IrSimdValue expected = { 0 };
                        _mm_setcsr(UINT32_C(0x1f80));
                        const bool modeled = ir_simd_evaluate(&action, arguments, NULL, NULL, 0, &expected);
                        IrFunction f = { 0 };
                        const bool created = modeled && ir_optimize_test_simd_function(input, type, lane, op, action.predicate, operands, &expected, &f);
                        ir_optimize_test_expect(test, created, "SIMD fold fixture and independent model accept lane operation");
                        if (created)
                        {
                            const bool floating = lane >= IR_SIMD_LANE_F32 && (op == IR_VCMP || (op >= IR_FADD && op <= IR_FDIV));
                            const uint32_t kind = op == IR_VCMP ? 4 : op - IR_FADD;
                            const bool foldable = !floating || ir_optimize_test_simd_environment_free(&action, arguments, &expected, bits / width, width);
                            if (floating)
                            {
                                floating_folds[kind] += foldable;
                                floating_refusals[kind] += !foldable;
                            }
                            for (uint32_t path = 0; path < 2; path += 1)
                            {
                                // A compiler embedded in another program must neither use nor leak its caller's FP state.
                                const uint32_t unusual_mxcsr = UINT32_C(0xffc0) | trial;
                                _mm_setcsr(unusual_mxcsr);
                                IrSimdValue folded = { 0 };
                                const bool ok = ir_optimize_simd_fold(&f, 3, operands, path != 0, &folded);
                                const uint32_t restored_mxcsr = _mm_getcsr();
                                _mm_setcsr(UINT32_C(0x1f80));
                                ir_optimize_test_expect(test, ok == foldable && (!ok || memcmp(&folded, &expected, sizeof(folded)) == 0) && restored_mxcsr == unusual_mxcsr,
                                                        "SIMD fold twin folds what every environment agrees on, matching model, NaN payloads and caller MXCSR");
                            }
                            if (trial == 0 || trial == 7 || trial == 8)
                            {
                                ir_optimize_test_simd_rewrite(test, &f, vector, scalar, scratch, 7);
                                IrOptimizeResult result = { 0 };
                                IrOptimizeOptions options = ir_optimize_options();
                                options.path = BUSTER_IR_OPTIMIZE_VECTOR;
                                const IrCheck check = ir_optimize(&f, vector, scratch, options, &result);
                                const IrFunction* out = &result.function;
                                const uint32_t ret = check.error == IR_OK ? out->instruction_count - 1 : 0;
                                const bool constant = check.error == IR_OK && out->op[out->a[ret]] == IR_CONST;
                                ir_optimize_test_expect(test, check.error == IR_OK && (foldable ? result.folded != 0 && constant : !constant),
                                                        "environment-dependent SIMD results remain live; other constants propagate");
                                arena_pop_to(vector, 0);
                            }
                        }
                        arena_pop_to(input, mark);
                    }
                }
            }
        }
    }
    _mm_setcsr(saved_mxcsr);
    for (uint32_t kind = 0; kind < 5; kind += 1)
    {
        ir_optimize_test_expect(test, floating_folds[kind] != 0 && floating_refusals[kind] != 0, "each floating lane operation both folds and refuses");
    }
}

// A random floating operand, mostly near 1.0 with few significant bits, so that many sums, products and quotients are
// exact; otherwise a special value, a subnormal or any exponent.
static uint64_t ir_optimize_test_simd_float_operand(IrOptimizeTest* test, uint32_t width)
{
    static const uint64_t specials[2][10] = {
        {
            0,
            UINT64_C(0x80000000),
            UINT64_C(0x7f800000),
            UINT64_C(0xff800000),
            UINT64_C(0x7fc00001),
            UINT64_C(0xff800002),
            1,
            UINT64_C(0x807fffff),
            UINT64_C(0x00800000),
            UINT64_C(0x7f7fffff),
        },
        {
            0,
            UINT64_C(0x8000000000000000),
            UINT64_C(0x7ff0000000000000),
            UINT64_C(0xfff0000000000000),
            UINT64_C(0x7ff8000000000001),
            UINT64_C(0xfff0000000000002),
            1,
            UINT64_C(0x800fffffffffffff),
            UINT64_C(0x0010000000000000),
            UINT64_C(0x7fefffffffffffff),
        },
    };
    const uint32_t precision = width == 32 ? 24 : 53;
    const uint64_t bias = width == 32 ? 127 : 1023;
    const uint64_t sign = UINT64_C(1) << (width - 1);
    const uint64_t random = ir_optimize_test_random(test);
    const uint64_t choice = random & 7;
    const uint64_t kept = (random >> 3) % precision;
    const uint64_t fraction = (ir_optimize_test_random(test) & ((UINT64_C(1) << (precision - 1)) - 1)) >> kept << kept;
    const uint64_t exponent = choice == 6 ? (random >> 10) % (2 * bias + 1) : choice == 5 ? 1 + (random >> 10) % 3 : bias - 6 + (random >> 10) % 13;
    uint64_t result = ((random >> 40) & 1) != 0 ? sign : 0;
    if (choice == 7)
    {
        result = specials[width == 64][(random >> 10) % 10];
    }
    else
    {
        result |= (exponent << (precision - 1)) | fraction;
    }
    return result;
}

// Both paths and the model on single lane pairs, each repeated across a 16-byte vector so that the whole vector's
// verdict is the pair's.
static void ir_optimize_test_simd_float_pairs(IrOptimizeTest* test)
{
    static const uint8_t operations[] = {
        IR_FADD,
        IR_FSUB,
        IR_FMUL,
        IR_FDIV,
        IR_VCMP,
    };
    static const uint8_t model[] = {
        IR_SIMD_FADD,
        IR_SIMD_FSUB,
        IR_SIMD_FMUL,
        IR_SIMD_FDIV,
        IR_SIMD_COMPARE,
    };
    const uint32_t saved_mxcsr = _mm_getcsr();
    uint32_t folds = 0;
    uint32_t refusals = 0;
    for (uint32_t trial = 0; trial < 4096; trial += 1)
    {
        const uint32_t width = (trial & 1) != 0 ? 64 : 32;
        const uint32_t kind = (trial >> 1) % 5;
        const uint32_t op = operations[kind];
        const uint32_t predicate = (trial / 10) % 6;
        const uint64_t a = ir_optimize_test_simd_float_operand(test, width);
        const uint64_t b = (trial & 2) != 0 ? a ^ (ir_optimize_test_random(test) & 3) : ir_optimize_test_simd_float_operand(test, width);
        IrSimdValue operands[2] = { 0 };
        for (uint32_t lane = 0; lane < 128 / width; lane += 1)
        {
            ir_simd_lane_write(operands, width, lane, a);
            ir_simd_lane_write(operands + 1, width, lane, b);
        }
        const IrSimdAction action = {
            .operation = (IrSimdOperation)model[kind],
            .result = op == IR_VCMP ? IR_SIMD_M8 : IR_SIMD_V4I32,
            .operands = {
                IR_SIMD_V4I32,
                IR_SIMD_V4I32,
                IR_SIMD_NONE,
            },
            .predicate = op == IR_VCMP ? (IrSimdPredicate)predicate : IR_SIMD_EQ,
            .lane = width == 32 ? IR_SIMD_LANE_F32 : IR_SIMD_LANE_F64,
        };
        const IrSimdValue* arguments[3] = { operands, operands + 1, NULL };
        IrSimdValue expected = { 0 };
        _mm_setcsr(UINT32_C(0x1f80));
        const bool modeled = ir_simd_evaluate(&action, arguments, NULL, NULL, 0, &expected);
        const bool foldable = modeled && ir_optimize_test_simd_environment_free(&action, arguments, &expected, 128 / width, width);
        IrSimdValue folded[2] = { 0 };
        bool ok[2] = { false, false };
        for (uint32_t path = 0; path < 2; path += 1)
        {
            _mm_setcsr(UINT32_C(0xffc0) | (trial & 0x3f));
            ok[path] = ir_optimize_simd_float(op, width, 128, predicate, operands, operands + 1, path != 0, folded + path);
        }
        const uint32_t restored = _mm_getcsr();
        _mm_setcsr(UINT32_C(0x1f80));
        folds += foldable;
        refusals += !foldable;
        ir_optimize_test_expect(test, modeled && ok[0] == foldable && ok[1] == foldable && restored == (UINT32_C(0xffc0) | (trial & 0x3f)) &&
                                (!foldable || (memcmp(folded, &expected, sizeof(expected)) == 0 && memcmp(folded + 1, &expected, sizeof(expected)) == 0)),
                                "random floating lane pairs: both paths fold exactly what every environment agrees on");
    }
    _mm_setcsr(saved_mxcsr);
    ir_optimize_test_expect(test, folds > 512 && refusals > 512, "random floating lane pairs both fold and refuse");
}

static void ir_optimize_test_simd_flow(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* scalar, Arena* scratch)
{
    const uint64_t mark = input->length;
    const IrSizes sizes = {
        .instruction_count = 11,
        .block_count = 4,
        .vector_constant_count = 2,
        .parameter_count = 1,
        .extra_count = 4,
    };
    IrFunction f = { 0 };
    const bool created = ir_create(input, &sizes, &f) == IR_OK;
    ir_optimize_test_expect(test, created, "SIMD lattice phi and bitcast fixture allocation");
    if (created)
    {
        f.result_type = IR_I128;
        f.parameter_types[0] = IR_I1;
        const uint32_t offsets[] = { 0, 4, 5, 6, 11 };
        memcpy(f.block_offsets, offsets, sizeof(offsets));
        f.vector_constants[0] = (IrSimdValue){ .words = { UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210) } };
        f.vector_constants[1] = f.vector_constants[0];
        const uint32_t incoming[] = { 1, 0, 2, 1 };
        memcpy(f.extra, incoming, sizeof(incoming));
        ir_optimize_test_instruction(&f, 0, IR_VCONST, IR_V4I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 1, IR_VCONST, IR_V4I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 2, IR_PARAM, IR_I1, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 3, IR_CBR, IR_VOID, 2, 1, 2);
        ir_optimize_test_instruction(&f, 4, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 5, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 6, IR_PHI, IR_V4I32, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 7, IR_BITCAST, IR_I128, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 8, IR_BITCAST, IR_V4I32, 7, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 9, IR_BITCAST, IR_I128, 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 10, IR_RET, IR_VOID, 9, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t distinct = 0; distinct < 2; distinct += 1)
        {
            f.vector_constants[1].words[1] ^= distinct;
            for (uint32_t passes = 0; passes < 16; passes += 1)
            {
                ir_optimize_test_simd_rewrite(test, &f, vector, scalar, scratch, passes);
            }
        }
        // An ordinary scalar select with an unknown predicate must also compare all vector words.
        f.op[6] = IR_SELECT;
        f.a[6] = 2;
        f.b[6] = 0;
        f.c[6] = 1;
        ir_optimize_test_simd_rewrite(test, &f, vector, scalar, scratch, 7);
        const uint64_t scratch_mark = scratch->length;
        IrOptimizeWork work = { 0 };
        const IrCheck prepared = ir_optimize_prepare(&work, &f, vector, scratch, true, true);
        ir_optimize_test_expect(test, prepared.error == IR_OK && work.vector_indices == NULL && work.vector_values == NULL,
                                "wide lattice storage is absent until a SIMD transfer");
        if (prepared.error == IR_OK)
        {
            Arena tiny = *scratch;
            tiny.capacity = tiny.length;
            work.scratch = &tiny;
            ir_optimize_test_expect(test, !ir_optimize_simd_storage(&work) && work.simd_error == IR_ERROR_MEMORY,
                                    "sparse SIMD lattice allocation failure is explicit");
        }
        arena_pop_to(scratch, scratch_mark);
    }
    arena_pop_to(input, mark);
}

static void ir_optimize_test_simd_loop(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* scalar, Arena* scratch)
{
    const uint64_t mark = input->length;
    const IrSizes sizes = {
        .instruction_count = 14,
        .block_count = 3,
        .constant_count = 3,
        .vector_constant_count = 2,
        .extra_count = 8,
    };
    IrFunction f = { 0 };
    const bool created = ir_create(input, &sizes, &f) == IR_OK;
    ir_optimize_test_expect(test, created, "changing SIMD loop-phi fixture allocation");
    if (created)
    {
        f.result_type = IR_I128;
        const uint32_t offsets[] = { 0, 6, 12, 14 };
        const uint32_t incoming[] = { 0, 0, 1, 8, 0, 2, 1, 9 };
        memcpy(f.block_offsets, offsets, sizeof(offsets));
        memcpy(f.extra, incoming, sizeof(incoming));
        memset(f.vector_constants[1].words, 1, 16);
        f.constants[1].low = 1;
        f.constants[2].low = 3;
        ir_optimize_test_instruction(&f, 0, IR_VCONST, IR_V4I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 1, IR_VCONST, IR_V4I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t i = 2; i < 5; i += 1)
        {
            ir_optimize_test_instruction(&f, i, IR_CONST, IR_I64, i - 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        ir_optimize_test_instruction(&f, 5, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 6, IR_PHI, IR_V4I32, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 7, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 8, IR_ADD, IR_V4I32, 6, 1, BUSTER_IR_NONE);
        f.flags[8] = IR_SIMD_LANE_I8;
        ir_optimize_test_instruction(&f, 9, IR_ADD, IR_I64, 7, 3, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 10, IR_ICMP, IR_I1, 9, 4, IR_LT);
        ir_optimize_test_instruction(&f, 11, IR_CBR, IR_VOID, 10, 1, 2);
        ir_optimize_test_instruction(&f, 12, IR_BITCAST, IR_I128, 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 13, IR_RET, IR_VOID, 12, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t passes = 0; passes < 16; passes += 1)
        {
            ir_optimize_test_simd_rewrite(test, &f, vector, scalar, scratch, passes);
        }
    }
    arena_pop_to(input, mark);
}

static void ir_optimize_test_simd_splats(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* scalar, Arena* scratch)
{
    static const uint8_t sources[] = { IR_I8, IR_I16, IR_I32, IR_I64, IR_F32, IR_F64 };
    for (uint32_t type = IR_V4I32; type <= IR_V16I32; type += 1)
    {
        for (uint32_t source = 0; source < sizeof(sources); source += 1)
        {
            const uint64_t mark = input->length;
            const uint32_t scalar_type = sources[source];
            const uint32_t bits = ir_type_bits[scalar_type];
            const uint64_t word = (source == 4 ? UINT64_C(0x7f800123) : source == 5 ? UINT64_C(0x7ff0000000000456) : UINT64_C(0xfedcba9876543210)) & (UINT64_MAX >> (64 - bits));
            const uint32_t mask = ir_lane_mask(type, IR_SIMD_LANE_I8);
            const IrSizes sizes = {
                .instruction_count = 5,
                .block_count = 1,
                .constant_count = 1,
                .vector_constant_count = 1,
            };
            IrFunction f = { 0 };
            const bool created = ir_create(input, &sizes, &f) == IR_OK;
            ir_optimize_test_expect(test, created, "constant splat optimizer fixture allocation");
            if (created)
            {
                f.result_type = mask;
                f.block_offsets[1] = 5;
                f.constants[0].low = word;
                // Independent expected bytes, including signaling-NaN payloads: a splat performs no arithmetic.
                const uint32_t bytes = ir_type_bits[type] / 8;
                for (uint32_t byte = 0; byte < bytes; byte += 1)
                {
                    const uint8_t value = (uint8_t)(word >> (8 * (byte % (bits / 8))));
                    f.vector_constants[0].words[byte / 8] |= (uint64_t)value << (8 * (byte & 7));
                }
                ir_optimize_test_instruction(&f, 0, IR_CONST, scalar_type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_optimize_test_instruction(&f, 1, IR_INTRINSIC, type, 0, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_SPLAT);
                ir_optimize_test_instruction(&f, 2, IR_VCONST, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_optimize_test_instruction(&f, 3, IR_VCMP, mask, 1, 2, IR_EQ);
                f.flags[3] = IR_SIMD_LANE_I8;
                ir_optimize_test_instruction(&f, 4, IR_RET, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_optimize_test_simd_rewrite(test, &f, vector, scalar, scratch, 7);
                IrOptimizeResult result = { 0 };
                IrOptimizeOptions options = ir_optimize_options();
                options.path = BUSTER_IR_OPTIMIZE_VECTOR;
                const IrCheck check = ir_optimize(&f, vector, scratch, options, &result);
                ir_optimize_test_expect(test, check.error == IR_OK && result.function.instruction_count == 2 && result.function.op[0] == IR_CONST,
                                        "constant unary splat folds through lane comparison without reading absent b");
                arena_pop_to(vector, 0);
            }
            arena_pop_to(input, mark);
        }
    }
}

// The width changes (#1840): SCCP folds a constant insert, the extract of its inserted part and the extract of a widen's
// zeros past its operand, by both paths, through a comparison with the values written out, to its constant mask; and
// keeps an extract of another part apart from one whose value differs.
static void ir_optimize_test_simd_widths(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* scalar, Arena* scratch)
{
    for (uint32_t part = 0; part < 4; part += 1)
    {
        const uint64_t mark = input->length;
        const IrSizes sizes = { .instruction_count = 12, .block_count = 1, .vector_constant_count = 3 };
        IrFunction f = { 0 };
        const bool created = ir_create(input, &sizes, &f) == IR_OK;
        ir_optimize_test_expect(test, created, "constant width change optimizer fixture allocation");
        if (created)
        {
            f.result_type = IR_M16;
            f.block_offsets[1] = 12;
            for (uint32_t w = 0; w < 8; w += 1)
            {
                f.vector_constants[0].words[w] = UINT64_C(0x0123456789abcdef) * (w + 3);
            }
            f.vector_constants[1].words[0] = UINT64_C(0xfeedfacecafebeef);
            f.vector_constants[1].words[1] = UINT64_C(0x0badf00d8badf00d);
            ir_optimize_test_instruction(&f, 0, IR_VCONST, IR_V16I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&f, 1, IR_VCONST, IR_V4I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&f, 2, IR_INTRINSIC, IR_V16I32, 0, 1, BUSTER_IR_INTRINSIC_INSERT);
            f.flags[2] = (uint8_t)part;
            ir_optimize_test_instruction(&f, 3, IR_INTRINSIC, IR_V4I32, 2, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_EXTRACT);
            f.flags[3] = (uint8_t)part;
            // The inserted part back, equal to the constant inserted, every byte.
            ir_optimize_test_instruction(&f, 4, IR_VCMP, IR_M16, 3, 1, IR_EQ);
            f.flags[4] = IR_SIMD_LANE_I8;
            // A widen's part 1 is zeros: equal to the zero constant.
            ir_optimize_test_instruction(&f, 5, IR_INTRINSIC, IR_V8I32, 1, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_WIDEN);
            ir_optimize_test_instruction(&f, 6, IR_INTRINSIC, IR_V4I32, 5, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_EXTRACT);
            f.flags[6] = 1;
            ir_optimize_test_instruction(&f, 7, IR_VCONST, IR_V4I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&f, 8, IR_VCMP, IR_M16, 6, 7, IR_EQ);
            f.flags[8] = IR_SIMD_LANE_I8;
            // Another part of the insert's result is the first constant's, which differs from the inserted one.
            ir_optimize_test_instruction(&f, 9, IR_INTRINSIC, IR_V4I32, 2, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_EXTRACT);
            f.flags[9] = (uint8_t)((part + 1) & 3);
            ir_optimize_test_instruction(&f, 10, IR_VCMP, IR_M16, 9, 3, IR_NE);
            f.flags[10] = IR_SIMD_LANE_I8;
            // Its bytes against the inserted ones, written out.
            uint64_t differ = 0;
            for (uint32_t byte = 0; byte < 16; byte += 1)
            {
                const uint32_t at = ((part + 1) & 3) * 16 + byte;
                const uint8_t mine = (uint8_t)(f.vector_constants[0].words[at / 8] >> (8 * (at & 7)));
                const uint8_t inserted = (uint8_t)(f.vector_constants[1].words[byte / 8] >> (8 * (byte & 7)));
                differ |= (uint64_t)(mine != inserted) << byte;
            }
            ir_optimize_test_instruction(&f, 11, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_simd_rewrite(test, &f, vector, scalar, scratch, 7);
            IrOptimizeResult result = { 0 };
            IrOptimizeOptions options = ir_optimize_options();
            options.path = BUSTER_IR_OPTIMIZE_VECTOR;
            IrCheck check = ir_optimize(&f, vector, scratch, options, &result);
            ir_optimize_test_expect(test, check.error == IR_OK && result.function.instruction_count == 2 && result.function.op[0] == IR_CONST && result.function.constants[0].low == 0xffff,
                                    "a constant insert's inserted part folds to its every-lane-equal mask");
            arena_pop_to(vector, 0);
            f.a[11] = 8;
            check = ir_optimize(&f, vector, scratch, options, &result);
            ir_optimize_test_expect(test, check.error == IR_OK && result.function.instruction_count == 2 && result.function.op[0] == IR_CONST && result.function.constants[0].low == 0xffff,
                                    "a widen's zeros past its operand fold");
            arena_pop_to(vector, 0);
            f.a[11] = 10;
            check = ir_optimize(&f, vector, scratch, options, &result);
            ir_optimize_test_expect(test, check.error == IR_OK && result.function.instruction_count == 2 && result.function.op[0] == IR_CONST && result.function.constants[0].low == differ && differ != 0,
                                    "another part's extract keeps its own value");
            arena_pop_to(vector, 0);
        }
        arena_pop_to(input, mark);
    }
}

static void ir_optimize_test_simd(IrOptimizeTest* test)
{
    Arena input = arena_reserve(UINT64_C(16) << 20);
    Arena vector = arena_reserve(UINT64_C(16) << 20);
    Arena scalar = arena_reserve(UINT64_C(16) << 20);
    Arena scratch = arena_reserve(UINT64_C(16) << 20);
    const bool ready = input.base != NULL && vector.base != NULL && scalar.base != NULL && scratch.base != NULL;
    ir_optimize_test_expect(test, ready, "SIMD optimization test arenas");
    if (ready)
    {
        ir_optimize_test_simd_lanes(test, &input, &vector, &scalar, &scratch);
        ir_optimize_test_simd_float_pairs(test);
        ir_optimize_test_simd_flow(test, &input, &vector, &scalar, &scratch);
        ir_optimize_test_simd_loop(test, &input, &vector, &scalar, &scratch);
        ir_optimize_test_simd_splats(test, &input, &vector, &scalar, &scratch);
        ir_optimize_test_simd_widths(test, &input, &vector, &scalar, &scratch);
    }
    arena_release(&input);
    arena_release(&vector);
    arena_release(&scalar);
    arena_release(&scratch);
}
