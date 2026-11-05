// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original fixed-width semantic controls. Clang's C vector operators provide an independent arithmetic result;
// explicit arrays/patterns provide comparison, selection, mask-tail and bounded-memory expectations. No generated
// assembly or compiler-host feature bit is an admission oracle. Include after ir_simd_model.c and stdio.h.

typedef struct IrSimdModelTest IrSimdModelTest;
struct IrSimdModelTest
{
    uint64_t checks;
    uint64_t failures;
    FILE* diagnostics;
};

static void ir_simd_model_expect(IrSimdModelTest* test, bool valid, const char* description)
{
    test->checks += 1;
    if (!valid)
    {
        if (test->diagnostics != NULL && test->failures < 12)
        {
            fprintf(test->diagnostics, "IR SIMD model: %s\n", description);
        }
        test->failures += 1;
    }
}

static void ir_simd_model_arithmetic(IrSimdModelTest* test)
{
    typedef uint32_t ReferenceVector __attribute__((ext_vector_type(16)));
    for (uint32_t width = 0; width < 3; width += 1)
    {
        const IrSimdShape shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        const uint32_t lanes = 4u << width;
        for (uint32_t sample = 0; sample < 73; sample += 1)
        {
            ReferenceVector a = { 0 };
            ReferenceVector b = { 0 };
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                a[lane] = (sample + lane * 17) * UINT32_C(0xfedcba97);
                b[lane] = (sample * 7 + lane + 1) * UINT32_C(0x81234567);
            }
            IrSimdValue inputs[2] = { 0 };
            memcpy(inputs[0].words, &a, lanes * sizeof(uint32_t));
            memcpy(inputs[1].words, &b, lanes * sizeof(uint32_t));
            const IrSimdValue* values[3] = { inputs, inputs + 1, NULL };
            const IrSimdOperation operations[] = { IR_SIMD_ADD, IR_SIMD_SUB, IR_SIMD_MUL, IR_SIMD_AND, IR_SIMD_OR, IR_SIMD_XOR };
            for (uint32_t operation = 0; operation < sizeof(operations) / sizeof(*operations); operation += 1)
            {
                ReferenceVector expected = { 0 };
                switch (operations[operation])
                {
                    case IR_SIMD_ADD: expected = a + b; break;
                    case IR_SIMD_SUB: expected = a - b; break;
                    case IR_SIMD_MUL: expected = a * b; break;
                    case IR_SIMD_AND: expected = a & b; break;
                    case IR_SIMD_OR: expected = a | b; break;
                    case IR_SIMD_XOR: expected = a ^ b; break;
                    default: break;
                }
                const IrSimdAction action = { .operation = operations[operation], .result = shape, .operands = { shape, shape } };
                IrSimdValue actual = { 0 };
                const bool valid = ir_simd_evaluate(&action, values, NULL, NULL, 0, &actual);
                ir_simd_model_expect(test, valid && memcmp(&actual, &expected, lanes * sizeof(uint32_t)) == 0 &&
                                    ir_simd_value_valid(&actual, shape), "integer lanes agree with independent C vector arithmetic, including wrap and zero tails");
            }
        }
    }
}

static void ir_simd_model_masks(IrSimdModelTest* test)
{
    const uint32_t a[16] = { UINT32_MAX, 0, 7, UINT32_C(0x80000000), 17, 17, 99, 0, 6, 7, 8, 9, 10, 11, 12, 13 };
    const uint32_t b[16] = { 0, 0, 6, UINT32_C(0x7fffffff), 17, 18, 2, UINT32_MAX, 9, 8, 7, 6, 5, 4, 3, 2 };
    for (uint32_t width = 0; width < 3; width += 1)
    {
        const IrSimdShape shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        const IrSimdShape mask = width == 2 ? IR_SIMD_M16 : IR_SIMD_M8;
        const uint32_t lanes = 4u << width;
        IrSimdValue inputs[3] = { 0 };
        memcpy(inputs[0].words, a, lanes * sizeof(uint32_t));
        memcpy(inputs[1].words, b, lanes * sizeof(uint32_t));
        const IrSimdValue* operands[3] = { inputs, inputs + 1, NULL };
        IrSimdValue compared = { 0 };
        for (uint32_t predicate = 0; predicate < IR_SIMD_PREDICATE_COUNT; predicate += 1)
        {
            const IrSimdAction compare = { .operation = IR_SIMD_COMPARE, .result = mask, .operands = { shape, shape }, .predicate = (IrSimdPredicate)predicate };
            uint64_t expected = 0;
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                int32_t signed_a = 0;
                int32_t signed_b = 0;
                memcpy(&signed_a, a + lane, sizeof(signed_a));
                memcpy(&signed_b, b + lane, sizeof(signed_b));
                bool relation = false;
                switch (predicate)
                {
                    case IR_SIMD_EQ: relation = a[lane] == b[lane]; break;
                    case IR_SIMD_NE: relation = a[lane] != b[lane]; break;
                    case IR_SIMD_LT: relation = signed_a < signed_b; break;
                    case IR_SIMD_LE: relation = signed_a <= signed_b; break;
                    case IR_SIMD_GT: relation = signed_a > signed_b; break;
                    case IR_SIMD_GE: relation = signed_a >= signed_b; break;
                    case IR_SIMD_ULT: relation = a[lane] < b[lane]; break;
                    case IR_SIMD_ULE: relation = a[lane] <= b[lane]; break;
                    case IR_SIMD_UGT: relation = a[lane] > b[lane]; break;
                    case IR_SIMD_UGE: relation = a[lane] >= b[lane]; break;
                    default: break;
                }
                expected |= (uint64_t)relation << lane;
            }
            ir_simd_model_expect(test, ir_simd_evaluate(&compare, operands, NULL, NULL, 0, &compared) && compared.words[0] == expected &&
                                ir_simd_value_valid(&compared, mask), "every signed/unsigned comparison creates a width-qualified mask with unused bits clear");
        }
        compared.words[0] = UINT64_C(0xaa);
        const IrSimdValue* selected[3] = { &compared, inputs, inputs + 1 };
        const IrSimdAction select = { .operation = IR_SIMD_SELECT, .result = shape, .operands = { mask, shape, shape } };
        IrSimdValue actual = { 0 };
        uint32_t wanted[16] = { 0 };
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            wanted[lane] = (UINT64_C(0xaa) & (UINT64_C(1) << lane)) != 0 ? a[lane] : b[lane];
        }
        ir_simd_model_expect(test, ir_simd_evaluate(&select, selected, NULL, NULL, 0, &actual) &&
                            memcmp(actual.words, wanted, lanes * sizeof(uint32_t)) == 0 && ir_simd_value_valid(&actual, shape),
                            "predication uses low mask bits and the explicit merge operand, including inactive lanes");
        selected[2] = inputs + 2;
        memset(wanted, 0, sizeof(wanted));
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            wanted[lane] = (UINT64_C(0xaa) & (UINT64_C(1) << lane)) != 0 ? a[lane] : 0;
        }
        ir_simd_model_expect(test, ir_simd_evaluate(&select, selected, NULL, NULL, 0, inputs) &&
                            memcmp(inputs[0].words, wanted, lanes * sizeof(uint32_t)) == 0, "an explicit zero merge supports zeroing and an aliased result");
    }
    for (uint32_t shape = IR_SIMD_M8; shape <= IR_SIMD_M64; shape += 1)
    {
        const uint32_t bits = 8u << (shape - IR_SIMD_M8);
        const uint64_t mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
        const IrSimdValue input = { .words = { UINT64_C(0x55) } };
        const IrSimdValue* values[3] = { &input, NULL, NULL };
        const IrSimdAction action = { .operation = IR_SIMD_NOT, .result = (IrSimdShape)shape, .operands = { (IrSimdShape)shape } };
        IrSimdValue actual = { 0 };
        ir_simd_model_expect(test, ir_simd_evaluate(&action, values, NULL, NULL, 0, &actual) && actual.words[0] == (~UINT64_C(0x55) & mask),
                            "mask logic is width-qualified through 64 bits without scalar i1/i64 conflation");
        const IrSimdValue right = { .words = { UINT64_C(0x96) } };
        values[1] = &right;
        for (uint32_t operation = IR_SIMD_AND; operation <= IR_SIMD_XOR; operation += 1)
        {
            const IrSimdAction binary = { .operation = (IrSimdOperation)operation, .result = (IrSimdShape)shape, .operands = { (IrSimdShape)shape, (IrSimdShape)shape } };
            const uint64_t expected = operation == IR_SIMD_AND ? UINT64_C(0x14) : operation == IR_SIMD_OR ? UINT64_C(0xd7) : UINT64_C(0xc3);
            ir_simd_model_expect(test, ir_simd_evaluate(&binary, values, NULL, NULL, 0, &actual) && actual.words[0] == expected &&
                                ir_simd_value_valid(&actual, (IrSimdShape)shape), "binary mask logic preserves exact width and distinct bit patterns");
        }
    }
}

static void ir_simd_model_memory(IrSimdModelTest* test)
{
    uint8_t bytes[66];
    memset(bytes, 0xa5, sizeof(bytes));
    IrSimdMemory memory = { .bytes = bytes + 1, .size = 64 };
    IrSimdValue source = { .words = { 1, 2, 3, 4, 5, 6, 7, 8 } };
    const IrSimdValue* values[3] = { &source, NULL, NULL };
    IrSimdValue actual = { 0 };
    IrSimdAction store = { .operation = IR_SIMD_STORE, .operands = { IR_SIMD_V16I32 }, .volatile_memory = true };
    IrSimdAction load = { .operation = IR_SIMD_LOAD, .result = IR_SIMD_V16I32, .volatile_memory = true };
    const IrSimdValue* absent[3] = { NULL, NULL, NULL };
    bool valid = ir_simd_evaluate(&store, values, NULL, &memory, 0, &actual) &&
                 ir_simd_evaluate(&load, absent, NULL, &memory, 0, &actual);
    ir_simd_model_expect(test, valid && memcmp(&actual, &source, sizeof(source)) == 0 && memory.reads == 64 && memory.writes == 64 &&
                        memory.volatile_accesses == 128 && bytes[0] == 0xa5 && bytes[65] == 0xa5,
                        "bounded vector memory preserves bytes and explicit ordered volatile effects without touching guard tails");
    const IrSimdMemory before = memory;
    const IrSimdValue saved = actual;
    const uint8_t last = bytes[65];
    valid = !ir_simd_evaluate(&store, values, NULL, &memory, 1, &actual) && !ir_simd_evaluate(&load, absent, NULL, &memory, UINT64_MAX, &actual);
    ir_simd_model_expect(test, valid && memcmp(&memory, &before, sizeof(memory)) == 0 && memcmp(&actual, &saved, sizeof(saved)) == 0 && bytes[65] == last,
                        "out-of-bounds memory refuses transactionally before any access or effect");
    memory.writes = UINT64_MAX;
    ir_simd_model_expect(test, !ir_simd_evaluate(&store, values, NULL, &memory, 0, &actual) && memory.volatile_accesses == before.volatile_accesses,
                        "effect counter overflow cannot wrap or perform a store");
}

// A lane of `bits` bits of `value`, written out independently of the model's lane reads.
static uint64_t ir_simd_model_test_lane(const IrSimdValue* value, uint32_t bits, uint32_t lane)
{
    uint64_t result = 0;
    memcpy(&result, (const uint8_t*)value->words + lane * (bits / 8), bits / 8);
    return result;
}

static int64_t ir_simd_model_test_signed(uint64_t value, uint32_t bits)
{
    const uint64_t sign = UINT64_C(1) << (bits - 1);
    return (int64_t)((value ^ sign) - sign);
}

// The lanes (#141's element types) against C's scalar operators lane by lane: 8-, 16-, 32- and 64-bit integers'
// arithmetic, products, shifts by each lane's count (zero past the lane's bits, the sign's every bit for an arithmetic
// one: the vpsllv family's), signed and unsigned comparisons into a bit per lane, floats' and doubles' arithmetic and C's
// comparisons, with finite values and one NaN lane per sample (no NaN reaches the arithmetic, whose NaN rule is x86's and
// test-tier1-masks checks natively), and selections at each lane width.
static void ir_simd_model_lanes(IrSimdModelTest* test)
{
    uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
    for (uint32_t width = 0; width < 3; width += 1)
    {
        const IrSimdShape shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        const uint32_t bytes = 16u << width;
        for (uint32_t kind = 0; kind < IR_SIMD_LANE_COUNT; kind += 1)
        {
            const IrSimdLane lane = (IrSimdLane)kind;
            const uint32_t bits = ir_simd_lane_bits(lane);
            const uint32_t lanes = bytes * 8 / bits;
            const bool floating = ir_simd_lane_floating(lane);
            const IrSimdShape mask = ir_simd_lane_mask(shape, lane);
            ir_simd_model_expect(test, mask == (lanes <= 8 ? IR_SIMD_M8 : lanes == 16 ? IR_SIMD_M16 : lanes == 32 ? IR_SIMD_M32 : IR_SIMD_M64),
                                 "a comparison's mask has a bit per lane");
            for (uint32_t sample = 0; sample < 41; sample += 1)
            {
                IrSimdValue a = { 0 };
                IrSimdValue b = { 0 };
                for (uint32_t k = 0; k < lanes; k += 1)
                {
                    uint64_t x = 0;
                    uint64_t y = 0;
                    for (uint32_t r = 0; r < 2; r += 1)
                    {
                        state ^= state << 13;
                        state ^= state >> 7;
                        state ^= state << 17;
                        if (floating && bits == 32)
                        {
                            const float value = (float)((int64_t)(state % 20001) - 10000) / 64.0f;
                            uint32_t word = 0;
                            memcpy(&word, &value, sizeof(word));
                            *(r == 0 ? &x : &y) = word;
                        }
                        else if (floating)
                        {
                            const double value = (double)((int64_t)(state % 2000001) - 1000000) / 1024.0;
                            memcpy(r == 0 ? &x : &y, &value, sizeof(value));
                        }
                        else
                        {
                            *(r == 0 ? &x : &y) = state;
                        }
                    }
                    // Equal lanes now and then, so that == and <= see them.
                    y = sample % 5 == 0 && k % 3 == 0 ? x : y;
                    memcpy((uint8_t*)a.words + k * (bits / 8), &x, bits / 8);
                    memcpy((uint8_t*)b.words + k * (bits / 8), &y, bits / 8);
                }
                IrSimdValue counts = b;
                for (uint32_t k = 0; !floating && k < lanes; k += 1)
                {
                    // Counts to twice the lane's bits, half of them past it.
                    const uint64_t count = ir_simd_model_test_lane(&b, bits, k) % (2 * bits);
                    memcpy((uint8_t*)counts.words + k * (bits / 8), &count, bits / 8);
                }
                const IrSimdOperation operations[] = { IR_SIMD_ADD, IR_SIMD_SUB, IR_SIMD_MUL, IR_SIMD_SHL, IR_SIMD_LSHR, IR_SIMD_ASHR,
                                                       IR_SIMD_FADD, IR_SIMD_FSUB, IR_SIMD_FMUL, IR_SIMD_FDIV };
                for (uint32_t o = 0; o < sizeof(operations) / sizeof(*operations); o += 1)
                {
                    const IrSimdOperation operation = operations[o];
                    const bool shift = operation == IR_SIMD_SHL || operation == IR_SIMD_LSHR || operation == IR_SIMD_ASHR;
                    const bool real = operation >= IR_SIMD_FADD;
                    const IrSimdAction action = { .operation = operation, .result = shape, .operands = { shape, shape }, .lane = lane };
                    ir_simd_model_expect(test, ir_simd_action_valid(&action) == (real == floating), "an operation takes the lanes of its kind alone");
                    if (real == floating)
                    {
                        const IrSimdValue* values[3] = { &a, shift ? &counts : &b, NULL };
                        IrSimdValue output = { 0 };
                        bool valid = ir_simd_evaluate(&action, values, NULL, NULL, 0, &output);
                        for (uint32_t k = 0; valid && k < lanes; k += 1)
                        {
                            const uint64_t x = ir_simd_model_test_lane(&a, bits, k);
                            const uint64_t y = ir_simd_model_test_lane(values[1], bits, k);
                            const uint64_t mask_bits = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
                            uint64_t expected = 0;
                            if (floating && bits == 32)
                            {
                                float fx = 0;
                                float fy = 0;
                                uint32_t wx = (uint32_t)x;
                                uint32_t wy = (uint32_t)y;
                                memcpy(&fx, &wx, sizeof(fx));
                                memcpy(&fy, &wy, sizeof(fy));
                                const float z = operation == IR_SIMD_FADD ? fx + fy : operation == IR_SIMD_FSUB ? fx - fy : operation == IR_SIMD_FMUL ? fx * fy : fx / fy;
                                uint32_t wz = 0;
                                memcpy(&wz, &z, sizeof(wz));
                                expected = wz;
                            }
                            else if (floating)
                            {
                                double fx = 0;
                                double fy = 0;
                                memcpy(&fx, &x, sizeof(fx));
                                memcpy(&fy, &y, sizeof(fy));
                                const double z = operation == IR_SIMD_FADD ? fx + fy : operation == IR_SIMD_FSUB ? fx - fy : operation == IR_SIMD_FMUL ? fx * fy : fx / fy;
                                memcpy(&expected, &z, sizeof(expected));
                            }
                            else
                            {
                                const int64_t sx = ir_simd_model_test_signed(x, bits);
                                switch (operation)
                                {
                                    case IR_SIMD_ADD: expected = x + y; break;
                                    case IR_SIMD_SUB: expected = x - y; break;
                                    case IR_SIMD_MUL: expected = x * y; break;
                                    case IR_SIMD_SHL: expected = y >= bits ? 0 : x << y; break;
                                    case IR_SIMD_LSHR: expected = y >= bits ? 0 : x >> y; break;
                                    default: expected = (uint64_t)(y >= bits ? (sx < 0 ? -1 : 0) : sx >> y); break;
                                }
                                expected &= mask_bits;
                            }
                            valid = ir_simd_model_test_lane(&output, bits, k) == expected;
                        }
                        ir_simd_model_expect(test, valid, "each lanes' arithmetic is C's operator lane by lane");
                    }
                }
                // A NaN lane among the comparisons' operands: unordered, so that only != holds there.
                IrSimdValue left = a;
                if (floating)
                {
                    const uint64_t nan = bits == 32 ? UINT64_C(0x7fc00001) : UINT64_C(0x7ff8000000000001);
                    memcpy((uint8_t*)left.words + (sample % lanes) * (bits / 8), &nan, bits / 8);
                }
                for (uint32_t p = 0; p < (floating ? (uint32_t)IR_SIMD_GE + 1 : (uint32_t)IR_SIMD_PREDICATE_COUNT); p += 1)
                {
                    const IrSimdAction compare = { .operation = IR_SIMD_COMPARE, .result = mask, .operands = { shape, shape }, .predicate = (IrSimdPredicate)p, .lane = lane };
                    const IrSimdValue* values[3] = { &left, &b, NULL };
                    IrSimdValue output = { 0 };
                    bool valid = ir_simd_evaluate(&compare, values, NULL, NULL, 0, &output);
                    uint64_t expected = 0;
                    for (uint32_t k = 0; k < lanes; k += 1)
                    {
                        const uint64_t x = ir_simd_model_test_lane(&left, bits, k);
                        const uint64_t y = ir_simd_model_test_lane(&b, bits, k);
                        bool set = false;
                        if (floating)
                        {
                            double fx = 0;
                            double fy = 0;
                            if (bits == 32)
                            {
                                float sx = 0;
                                float sy = 0;
                                uint32_t wx = (uint32_t)x;
                                uint32_t wy = (uint32_t)y;
                                memcpy(&sx, &wx, sizeof(sx));
                                memcpy(&sy, &wy, sizeof(sy));
                                fx = sx;
                                fy = sy;
                            }
                            else
                            {
                                memcpy(&fx, &x, sizeof(fx));
                                memcpy(&fy, &y, sizeof(fy));
                            }
                            const IrSimdPredicate predicate = (IrSimdPredicate)p;
                            set = predicate == IR_SIMD_EQ ? fx == fy : predicate == IR_SIMD_NE ? fx != fy : predicate == IR_SIMD_LT ? fx < fy :
                                  predicate == IR_SIMD_LE ? fx <= fy : predicate == IR_SIMD_GT ? fx > fy : fx >= fy;
                        }
                        else
                        {
                            const int64_t sx = ir_simd_model_test_signed(x, bits);
                            const int64_t sy = ir_simd_model_test_signed(y, bits);
                            switch ((IrSimdPredicate)p)
                            {
                                case IR_SIMD_EQ: set = x == y; break;
                                case IR_SIMD_NE: set = x != y; break;
                                case IR_SIMD_LT: set = sx < sy; break;
                                case IR_SIMD_LE: set = sx <= sy; break;
                                case IR_SIMD_GT: set = sx > sy; break;
                                case IR_SIMD_GE: set = sx >= sy; break;
                                case IR_SIMD_ULT: set = x < y; break;
                                case IR_SIMD_ULE: set = x <= y; break;
                                case IR_SIMD_UGT: set = x > y; break;
                                default: set = x >= y; break;
                            }
                        }
                        expected |= (uint64_t)set << k;
                    }
                    valid = valid && output.words[0] == expected;
                    ir_simd_model_expect(test, valid, "each lanes' comparison is C's lane by lane, a bit per lane");
                    // The selection by that mask at the same lanes.
                    const IrSimdValue mask_value = output;
                    const IrSimdAction select = { .operation = IR_SIMD_SELECT, .result = shape, .operands = { mask, shape, shape }, .lane = lane };
                    const IrSimdValue* choices[3] = { &mask_value, &a, &b };
                    IrSimdValue selected = { 0 };
                    bool chosen = ir_simd_evaluate(&select, choices, NULL, NULL, 0, &selected);
                    for (uint32_t k = 0; chosen && k < lanes; k += 1)
                    {
                        chosen = ir_simd_model_test_lane(&selected, bits, k) == ir_simd_model_test_lane((expected >> k) & 1 ? &a : &b, bits, k);
                    }
                    ir_simd_model_expect(test, chosen, "a selection takes each lane by its own mask bit");
                }
                if (floating)
                {
                    const IrSimdAction unsigned_compare = { .operation = IR_SIMD_COMPARE, .result = mask, .operands = { shape, shape }, .predicate = IR_SIMD_ULT, .lane = lane };
                    ir_simd_model_expect(test, !ir_simd_action_valid(&unsigned_compare), "floating lanes have no unsigned comparison");
                }
            }
        }
    }
    // x86's NaN rule: the first operand's NaN quieted, else the second's.
    IrSimdValue a = { .words = { UINT64_C(0x7f8000013f800000) } };
    IrSimdValue b = { .words = { UINT64_C(0x3f8000007f800002) } };
    const IrSimdAction add = { .operation = IR_SIMD_FADD, .result = IR_SIMD_V4I32, .operands = { IR_SIMD_V4I32, IR_SIMD_V4I32 }, .lane = IR_SIMD_LANE_F32 };
    const IrSimdValue* values[3] = { &a, &b, NULL };
    IrSimdValue output = { 0 };
    ir_simd_model_expect(test, ir_simd_evaluate(&add, values, NULL, NULL, 0, &output) && output.words[0] == UINT64_C(0x7fc000017fc00002),
                         "a NaN operand's quieted bits are the sum, the first's before the second's");
}

static IrSimdModelTest ir_simd_model_test(FILE* diagnostics)
{
    IrSimdModelTest test = { .diagnostics = diagnostics };
    ir_simd_model_arithmetic(&test);
    ir_simd_model_lanes(&test);
    ir_simd_model_masks(&test);
    ir_simd_model_memory(&test);
    IrSimdAction action = { .operation = IR_SIMD_SELECT, .result = IR_SIMD_V4I32, .operands = { IR_SIMD_M16, IR_SIMD_V4I32, IR_SIMD_V4I32 } };
    ir_simd_model_expect(&test, !ir_simd_action_valid(&action), "a wrong-width predicate is rejected even if all lanes would fit");
    action = (IrSimdAction){ .operation = IR_SIMD_ADD, .result = IR_SIMD_M32, .operands = { IR_SIMD_M32, IR_SIMD_M32 } };
    ir_simd_model_expect(&test, !ir_simd_action_valid(&action), "integer arithmetic cannot silently interpret an opmask as an ordinary integer");
    action = (IrSimdAction){ .operation = IR_SIMD_ADD, .result = IR_SIMD_V4I32, .operands = { IR_SIMD_V4I32, IR_SIMD_V8I32 } };
    ir_simd_model_expect(&test, !ir_simd_action_valid(&action), "different vector shapes do not silently truncate or broadcast");
    action = (IrSimdAction){ .operation = IR_SIMD_COMPARE, .result = IR_SIMD_M8, .operands = { IR_SIMD_V4I32, IR_SIMD_V4I32 }, .predicate = IR_SIMD_PREDICATE_COUNT };
    ir_simd_model_expect(&test, !ir_simd_action_valid(&action), "unknown comparison predicates fail closed");
    action = (IrSimdAction){ .operation = IR_SIMD_COPY, .result = IR_SIMD_V4I32, .operands = { IR_SIMD_V4I32 }, .volatile_memory = true };
    ir_simd_model_expect(&test, !ir_simd_action_valid(&action), "memory effects cannot be attached to a pure operation");
    IrSimdValue malformed = { .words = { 1, 1 } };
    ir_simd_model_expect(&test, !ir_simd_value_valid(&malformed, IR_SIMD_M64) && !ir_simd_value_valid(NULL, IR_SIMD_V4I32) &&
                        ir_simd_bits((IrSimdShape)255) == 0, "invalid shape IDs and noncanonical mask payload tails are rejected");
    return test;
}
