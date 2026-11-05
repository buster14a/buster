// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// SIMD SCCP (#1450), included after the scalar lattice publisher. Wide values are sparse: only vector-result rows
// have a 64-byte cell, allocated on the first vector transfer. Scalar functions do not scan or allocate a wide map.
// Integer lanes reuse the independent batched integer transfer, not ir_simd_evaluate (the test oracle). Floating
// values have the IR's default-rounding semantics, but their original rows remain to preserve flags and traps.

static bool ir_optimize_simd_effect(const IrFunction* f, uint32_t i)
{
    const uint32_t op = f->op[i];
    const bool result = f->flags[i] >= IR_SIMD_LANE_F32 &&
                        (op == IR_VCMP || op == IR_FADD || op == IR_FSUB || op == IR_FMUL || op == IR_FDIV);
    return result;
}

static bool ir_optimize_simd_storage(IrOptimizeWork* work)
{
    if (work->vector_indices == NULL && work->simd_error == IR_OK)
    {
        const IrFunction* f = work->function;
        const uint32_t count = f->instruction_count;
        const uint8_t* const types = f->type;
        uint32_t vectors = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            vectors += ir_vector_type(types[i]);
        }
        bool ok = true;
        work->vector_indices = ir_allocate(work->scratch, count, sizeof(uint32_t), &ok);
        work->vector_values = ir_allocate(work->scratch, vectors, sizeof(IrSimdValue), &ok);
        if (ok)
        {
            uint32_t next = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                work->vector_indices[i] = ir_vector_type(types[i]) ? next++ : BUSTER_IR_NONE;
            }
        }
        else
        {
            work->simd_error = IR_ERROR_MEMORY;
        }
    }
    return work->simd_error == IR_OK;
}

static IrSimdValue ir_optimize_simd_value(const IrOptimizeWork* work, uint32_t i)
{
    IrSimdValue result = { 0 };
    if (ir_vector_type(work->function->type[i]))
    {
        result = work->vector_values[work->vector_indices[i]];
    }
    else
    {
        result.words[0] = work->values[i].low;
        result.words[1] = work->values[i].high;
    }
    return result;
}

static uint8_t ir_optimize_simd_meet(uint8_t left, const IrSimdValue* a, uint8_t right, const IrSimdValue* b, IrSimdValue* value)
{
    uint8_t result = IROPT_OVERDEFINED;
    if (left == IROPT_UNKNOWN)
    {
        result = right;
        *value = *b;
    }
    else if (right == IROPT_UNKNOWN)
    {
        result = left;
        *value = *a;
    }
    else if (left == IROPT_CONSTANT && right == IROPT_CONSTANT && memcmp(a, b, sizeof(*a)) == 0)
    {
        result = IROPT_CONSTANT;
        *value = *a;
    }
    return result;
}

// Compare IEEE bit patterns without evaluating a floating operation in the compiler. Signed zeros compare equal;
// after excluding NaNs, complementing negative encodings and biasing positive ones gives numeric order.
static bool ir_optimize_simd_float_compare(uint64_t a, uint64_t b, uint32_t width, uint32_t predicate)
{
    const uint64_t sign = UINT64_C(1) << (width - 1);
    const uint64_t mask = UINT64_MAX >> (64 - width);
    const uint64_t exponent = width == 32 ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    const uint64_t fraction = width == 32 ? UINT64_C(0x007fffff) : UINT64_C(0x000fffffffffffff);
    const bool unordered = ((a & exponent) == exponent && (a & fraction) != 0) || ((b & exponent) == exponent && (b & fraction) != 0);
    const bool equal = a == b || ((a | b) & ~sign) == 0;
    const uint64_t left = (a & sign) != 0 ? (~a & mask) : (a ^ sign);
    const uint64_t right = (b & sign) != 0 ? (~b & mask) : (b ^ sign);
    bool result = false;
    switch (predicate)
    {
        case IR_EQ: result = !unordered && equal; break;
        case IR_NE: result = unordered || !equal; break;
        case IR_LT: result = !unordered && !equal && left < right; break;
        case IR_LE: result = !unordered && (equal || left < right); break;
        case IR_GT: result = !unordered && !equal && left > right; break;
        case IR_GE: result = !unordered && (equal || left > right); break;
        default: break;
    }
    return result;
}

// Both paths save MXCSR, disable FTZ/DAZ, mask exceptions and use the IR's rounding mode before restoring every bit.
// AVX-512 also supplies explicit round-to-nearest/SAE. The
// strict floating scope prevents arithmetic crossing those changes. Repair NaNs afterwards, so operand swapping
// by the host compiler cannot choose a different payload from the IR's first-NaN rule.
static void ir_optimize_simd_float(uint32_t op, uint32_t width, uint32_t bits, const IrSimdValue* left, const IrSimdValue* right, bool scalar, IrSimdValue* output)
{
#pragma STDC FENV_ACCESS ON
    (void)scalar;
    IrSimdValue staged = { 0 };
    const uint32_t mxcsr = _mm_getcsr();
    _mm_setcsr(UINT32_C(0x1f80));
#if BUSTER_HOST_VECTOR
    if (!scalar)
    {
        const __m512i a = _mm512_loadu_si512(left);
        const __m512i b = _mm512_loadu_si512(right);
        if (width == 32)
        {
            const __m512 x = _mm512_castsi512_ps(a);
            const __m512 y = _mm512_castsi512_ps(b);
            __m512 z;
            if (op == IR_FADD)
            {
                z = _mm512_add_round_ps(x, y, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
            }
            else if (op == IR_FSUB)
            {
                z = _mm512_sub_round_ps(x, y, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
            }
            else if (op == IR_FMUL)
            {
                z = _mm512_mul_round_ps(x, y, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
            }
            else
            {
                z = _mm512_div_round_ps(x, y, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
            }
            _mm512_storeu_si512(&staged, _mm512_castps_si512(z));
        }
        else
        {
            const __m512d x = _mm512_castsi512_pd(a);
            const __m512d y = _mm512_castsi512_pd(b);
            __m512d z;
            if (op == IR_FADD)
            {
                z = _mm512_add_round_pd(x, y, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
            }
            else if (op == IR_FSUB)
            {
                z = _mm512_sub_round_pd(x, y, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
            }
            else if (op == IR_FMUL)
            {
                z = _mm512_mul_round_pd(x, y, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
            }
            else
            {
                z = _mm512_div_round_pd(x, y, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
            }
            _mm512_storeu_si512(&staged, _mm512_castpd_si512(z));
        }
    }
    else
#endif
    {
        for (uint32_t lane = 0; lane < bits / width; lane += 1)
        {
            const uint64_t a = ir_simd_lane_read(left, width, lane);
            const uint64_t b = ir_simd_lane_read(right, width, lane);
            uint64_t word = 0;
            if (width == 32)
            {
                const uint32_t aa = (uint32_t)a;
                const uint32_t bb = (uint32_t)b;
                float x;
                float y;
                memcpy(&x, &aa, sizeof(x));
                memcpy(&y, &bb, sizeof(y));
                const float z = op == IR_FADD ? x + y : op == IR_FSUB ? x - y : op == IR_FMUL ? x * y : x / y;
                uint32_t zz;
                memcpy(&zz, &z, sizeof(zz));
                word = zz;
            }
            else
            {
                double x;
                double y;
                memcpy(&x, &a, sizeof(x));
                memcpy(&y, &b, sizeof(y));
                const double z = op == IR_FADD ? x + y : op == IR_FSUB ? x - y : op == IR_FMUL ? x * y : x / y;
                memcpy(&word, &z, sizeof(word));
            }
            ir_simd_lane_write(&staged, width, lane, word);
        }
    }
    _mm_setcsr(mxcsr);
    const uint64_t exponent = width == 32 ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    const uint64_t fraction = width == 32 ? UINT64_C(0x007fffff) : UINT64_C(0x000fffffffffffff);
    const uint64_t quiet = width == 32 ? UINT64_C(0x00400000) : UINT64_C(0x0008000000000000);
    for (uint32_t lane = 0; lane < bits / width; lane += 1)
    {
        const uint64_t a = ir_simd_lane_read(left, width, lane);
        const uint64_t b = ir_simd_lane_read(right, width, lane);
        const bool a_nan = (a & exponent) == exponent && (a & fraction) != 0;
        const bool b_nan = (b & exponent) == exponent && (b & fraction) != 0;
        if (a_nan || b_nan)
        {
            ir_simd_lane_write(&staged, width, lane, (a_nan ? a : b) | quiet);
        }
    }
    for (uint32_t word = bits / 64; word < 8; word += 1)
    {
        staged.words[word] = 0;
    }
    *output = staged;
}

static bool ir_optimize_simd_fold(const IrFunction* f, uint32_t i, const IrSimdValue operands[3], bool scalar, IrSimdValue* output)
{
    const uint32_t op = f->op[i];
    const uint32_t bits = ir_type_bits[f->type[op == IR_VCMP ? f->a[i] : i]];
    const uint32_t width = ir_simd_lane_bits((IrSimdLane)f->flags[i]);
    IrSimdValue staged = { 0 };
    bool folded = true;
    if (op == IR_FADD || op == IR_FSUB || op == IR_FMUL || op == IR_FDIV)
    {
        ir_optimize_simd_float(op, width, bits, operands, operands + 1, scalar, &staged);
    }
    else if (op == IR_AND || op == IR_OR || op == IR_XOR || op == IR_NOT)
    {
        for (uint32_t word = 0; word < bits / 64; word += 1)
        {
            const uint64_t a = operands[0].words[word];
            const uint64_t b = operands[1].words[word];
            staged.words[word] = op == IR_AND ? a & b : op == IR_OR ? a | b : op == IR_XOR ? a ^ b : ~a;
        }
    }
    else if (op == IR_VSELECT)
    {
        for (uint32_t lane = 0; lane < bits / width; lane += 1)
        {
            const uint32_t selected = (operands[0].words[0] & (UINT64_C(1) << lane)) != 0 ? 1 : 2;
            ir_simd_lane_write(&staged, width, lane, ir_simd_lane_read(operands + selected, width, lane));
        }
    }
    else if (op == IR_VCMP && f->flags[i] >= IR_SIMD_LANE_F32)
    {
        for (uint32_t lane = 0; lane < bits / width; lane += 1)
        {
            const uint64_t a = ir_simd_lane_read(operands, width, lane);
            const uint64_t b = ir_simd_lane_read(operands + 1, width, lane);
            staged.words[0] |= (uint64_t)ir_optimize_simd_float_compare(a, b, width, f->c[i]) << lane;
        }
    }
    else
    {
        bool swap = false;
        uint32_t operation = ir_optimize_fold_operation(f, i, &swap);
        if (op == IR_VCMP)
        {
            static const uint8_t predicates[IR_PREDICATE_COUNT] = {
                IROPT_FOLD_EQ,
                IROPT_FOLD_NE,
                IROPT_FOLD_SLT,
                IROPT_FOLD_SLE,
                IROPT_FOLD_SLT,
                IROPT_FOLD_SLE,
                IROPT_FOLD_ULT,
                IROPT_FOLD_ULE,
                IROPT_FOLD_ULT,
                IROPT_FOLD_ULE,
            };
            operation = predicates[f->c[i]];
            swap = f->c[i] == IR_GT || f->c[i] == IR_GE || f->c[i] == IR_UGT || f->c[i] == IR_UGE;
        }
        folded = operation != IROPT_FOLD_NONE;
        const uint32_t lanes = bits / width;
        for (uint32_t first = 0; folded && first < lanes; first += BUSTER_IR_OPTIMIZE_BATCH)
        {
            const uint32_t count = lanes - first < BUSTER_IR_OPTIMIZE_BATCH ? lanes - first : BUSTER_IR_OPTIMIZE_BATCH;
            uint32_t operations[BUSTER_IR_OPTIMIZE_BATCH];
            uint32_t widths[BUSTER_IR_OPTIMIZE_BATCH];
            uint64_t left[BUSTER_IR_OPTIMIZE_BATCH];
            uint64_t right[BUSTER_IR_OPTIMIZE_BATCH];
            uint64_t values[BUSTER_IR_OPTIMIZE_BATCH] = { 0 };
            uint16_t active = 0;
            for (uint32_t lane = 0; lane < count; lane += 1)
            {
                operations[lane] = operation;
                widths[lane] = width;
                left[lane] = ir_simd_lane_read(operands + (swap ? 1 : 0), width, first + lane);
                right[lane] = ir_simd_lane_read(operands + (swap ? 0 : 1), width, first + lane);
                const bool shift = op == IR_SHL || op == IR_LSHR || op == IR_ASHR;
                if (shift && right[lane] >= width)
                {
                    const bool negative = (left[lane] & (UINT64_C(1) << (width - 1))) != 0;
                    values[lane] = op == IR_ASHR && negative ? UINT64_MAX >> (64 - width) : 0;
                }
                else
                {
                    active |= UINT32_C(1) << lane;
                }
            }
            const IrOptimizeFoldBatch batch = {
                .count = count,
                .operation = operations,
                .width = widths,
                .left = left,
                .right = right,
                .result = values,
            };
            const uint16_t done = scalar ? ir_optimize_fold_scalar(batch, active) : ir_optimize_fold_vector(batch, active);
            folded = done == active;
            for (uint32_t lane = 0; lane < count; lane += 1)
            {
                if (op == IR_VCMP)
                {
                    staged.words[0] |= values[lane] << (first + lane);
                }
                else
                {
                    ir_simd_lane_write(&staged, width, first + lane, values[lane]);
                }
            }
        }
    }
    if (folded)
    {
        *output = staged;
    }
    return folded;
}

static uint8_t ir_optimize_simd_transfer(const IrOptimizeWork* work, uint32_t i, IrSimdValue* value)
{
    const IrFunction* f = work->function;
    const uint32_t op = f->op[i];
    const uint32_t a = f->a[i];
    const uint32_t b = f->b[i];
    uint8_t result = IROPT_OVERDEFINED;
    if (op == IR_VCONST)
    {
        result = IROPT_CONSTANT;
        *value = f->vector_constants[a];
    }
    else if (op == IR_INTRINSIC && f->c[i] == BUSTER_IR_INTRINSIC_SPLAT)
    {
        result = work->states[a];
        if (result == IROPT_CONSTANT && !ir_splat_value(f->type[i], f->type[a], work->values[a].low, value))
        {
            result = IROPT_OVERDEFINED;
        }
    }
    else if (op == IR_COPY || op == IR_BITCAST)
    {
        result = work->states[a];
        if (result == IROPT_CONSTANT)
        {
            *value = ir_optimize_simd_value(work, a);
        }
    }
    else if (op == IR_PHI)
    {
        result = IROPT_UNKNOWN;
        for (uint32_t incoming = 0; incoming < b; incoming += 1)
        {
            const uint32_t predecessor = f->extra[a + incoming * 2];
            const uint32_t source = f->extra[a + incoming * 2 + 1];
            const uint32_t edge = ir_optimize_edge(&work->cfg, predecessor, work->cfg.instruction_blocks[i]);
            if (work->edges[edge] != 0)
            {
                const IrSimdValue incoming_value = ir_optimize_simd_value(work, source);
                result = ir_optimize_simd_meet(result, value, work->states[source], &incoming_value, value);
            }
        }
    }
    else if (op == IR_SELECT)
    {
        if (work->states[a] == IROPT_CONSTANT)
        {
            const uint32_t source = work->values[a].low != 0 ? b : f->c[i];
            result = work->states[source];
            if (result == IROPT_CONSTANT)
            {
                *value = ir_optimize_simd_value(work, source);
            }
        }
        else
        {
            const IrSimdValue left = ir_optimize_simd_value(work, b);
            const IrSimdValue right = ir_optimize_simd_value(work, f->c[i]);
            if (work->states[b] == IROPT_CONSTANT && work->states[f->c[i]] == IROPT_CONSTANT && memcmp(&left, &right, sizeof(left)) == 0)
            {
                result = IROPT_CONSTANT;
                *value = left;
            }
            else if (work->states[a] == IROPT_UNKNOWN)
            {
                result = IROPT_UNKNOWN;
            }
        }
    }
    else if (op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_AND || op == IR_OR || op == IR_XOR || op == IR_NOT ||
             op == IR_SHL || op == IR_LSHR || op == IR_ASHR || op == IR_FADD || op == IR_FSUB || op == IR_FMUL || op == IR_FDIV ||
             op == IR_VCMP || op == IR_VSELECT)
    {
        const uint32_t mask = ir_value_mask(f, i);
        const uint32_t ids[3] = { a, b, f->c[i] };
        IrSimdValue operands[3] = { 0 };
        bool unknown = false;
        bool overdefined = false;
        for (uint32_t operand = 0; operand < 3; operand += 1)
        {
            if ((mask & (1u << operand)) != 0)
            {
                const uint32_t id = ids[operand];
                unknown = unknown || work->states[id] == IROPT_UNKNOWN;
                overdefined = overdefined || work->states[id] == IROPT_OVERDEFINED;
                if (work->states[id] == IROPT_CONSTANT)
                {
                    operands[operand] = ir_optimize_simd_value(work, id);
                }
            }
        }
        if (!overdefined)
        {
            result = unknown ? IROPT_UNKNOWN : ir_optimize_simd_fold(f, i, operands, work->scalar, value) ? IROPT_CONSTANT : IROPT_OVERDEFINED;
        }
    }
    return result;
}

static bool ir_optimize_simd_visit(IrOptimizeWork* work, uint32_t i, bool queue)
{
    bool changed = false;
    if (ir_optimize_simd_storage(work))
    {
        IrSimdValue value = { 0 };
        const uint8_t state = ir_optimize_simd_transfer(work, i, &value);
        if (ir_vector_type(work->function->type[i]))
        {
            IrSimdValue* const current = work->vector_values + work->vector_indices[i];
            IrSimdValue combined = { 0 };
            const uint8_t next = ir_optimize_simd_meet(work->states[i], current, state, &value, &combined);
            changed = next != work->states[i];
            if (changed)
            {
                work->states[i] = next;
                *current = next == IROPT_CONSTANT ? combined : (IrSimdValue){ 0 };
                if (queue)
                {
                    ir_optimize_enqueue_users(work, i);
                }
            }
        }
        else
        {
            const IrConstant scalar = { .low = value.words[0], .high = value.words[1] };
            changed = ir_optimize_publish(work, i, state, scalar, queue);
        }
    }
    return changed;
}
