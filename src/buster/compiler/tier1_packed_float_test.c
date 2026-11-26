// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Packed FMA and integer/float conversions (#2806), owned by tier1_select_test.c. The selected instructions run against
// the shared IR's lane model, across register aliases, lane widths and target encodings. The model has no instruction
// form or allocated register, so an incorrect destructive move or a dword form chosen for qword lanes cannot agree.

// Load four vector registers from four 64-byte input slots, expand the operation, and store its result. All fixture
// registers are below xmm4; xmm15 remains the emitter's reserved scratch, never an input or destination.
static bool tier1_select_test_packed_code(Tier1SelectTest* test, const Tier1SelectHoles* holes)
{
    const uint32_t encoding = tier1_select_holes_encoding(holes);
    const Tier1SelectFloatForms* moves = tier1_select_class_forms(holes->width, encoding);
    const Tier1SelectRecords selected = tier1_select_test_expand(test, holes);
    X86Instruction records[16];
    uint32_t count = 0;
    for (uint32_t r = 0; r < 4; r += 1)
    {
        records[count++] = (X86Instruction)
        {
            .form = moves->load,
            .reg = (uint8_t)r,
            .rm = X86_REGISTER_RDI,
            .mode = BUSTER_X86_MEMORY_BASE,
            .displacement = (int32_t)(r * 64),
        };
    }
    const uint32_t first = count;
    memcpy(records + count, selected.records, selected.count * sizeof(*records));
    count += selected.count;
    records[count++] = (X86Instruction)
    {
        .form = moves->store,
        .reg = holes->destination & 15,
        .rm = X86_REGISTER_RSI,
        .mode = BUSTER_X86_MEMORY_BASE,
    };
    if (encoding != BUSTER_TIER1_ENCODING_SSE)
    {
        records[count++] = (X86Instruction)
        {
            .form = X86_FORM_VZEROUPPER,
        };
    }
    records[count++] = (X86Instruction)
    {
        .form = X86_FORM_RET,
    };
    return tier1_select_test_load_records(test, records, count, first, first + selected.count, false);
}

static void tier1_select_test_packed_conversions(Tier1SelectTest* test, const uint8_t* baseline, const uint8_t* v3, const uint8_t* no_dq, const uint8_t* no_vl)
{
    static const uint8_t operations[] =
    {
        IR_SITOFP,
        IR_UITOFP,
        IR_FPTOSI,
        IR_FPTOUI,
    };
    static const IrSimdOperation modeled[] =
    {
        IR_SIMD_SITOFP,
        IR_SIMD_UITOFP,
        IR_SIMD_FPTOSI,
        IR_SIMD_FPTOUI,
    };
    static const uint64_t integers[2][8] =
    {
        {
            0,
            1,
            UINT32_MAX,
            0x80000000u,
            0x7fffffffu,
            0x1000001u,
            0xffffffu,
            0x12345678u,
        },
        {
            0,
            1,
            UINT64_MAX,
            UINT64_C(0x8000000000000000),
            UINT64_C(0x7fffffffffffffff),
            UINT64_C(0x20000000000001),
            UINT64_C(0x1fffffffffffff),
            UINT64_C(0x123456789abcdef0),
        },
    };
    // Finite lanes inside the conversion's domain, including both signed endpoints, negative fractions that truncate
    // to unsigned zero, and the last representable input below each unsigned upper bound. Integer inputs above include
    // inexact conversions.
    static const uint64_t floating[2][2][8] =
    {
        {
            {
                0,
                0x80000000u,
                0x3fe00000u,
                0xbfe00000u,
                0x4effffffu,
                0xcf000000u,
                0x3f000000u,
                0xbf000000u,
            },
            {
                0,
                0x80000000u,
                0x3fe00000u,
                0x4f7fffffu,
                0x4f000000u,
                0x4b800001u,
                0xbf000000u,
                0xbf7fffffu,
            },
        },
        {
            {
                0,
                UINT64_C(0x8000000000000000),
                UINT64_C(0x3ffc000000000000),
                UINT64_C(0xbffc000000000000),
                UINT64_C(0x43dfffffffffffff),
                UINT64_C(0xc3e0000000000000),
                UINT64_C(0x3fe0000000000000),
                UINT64_C(0xbfe0000000000000),
            },
            {
                0,
                UINT64_C(0x8000000000000000),
                UINT64_C(0x3ffc000000000000),
                UINT64_C(0x43efffffffffffff),
                UINT64_C(0x43e0000000000000),
                UINT64_C(0x4340000000000001),
                UINT64_C(0xbfe0000000000000),
                UINT64_C(0xbfefffffffffffff),
            },
        },
    };
    for (uint32_t d = 0; d < 2; d += 1)
    {
        const IrSimdLane lane = d == 0 ? IR_SIMD_LANE_F32 : IR_SIMD_LANE_F64;
        const uint32_t bits = d == 0 ? 32 : 64;
        for (uint32_t width = 16; width <= 64; width *= 2)
        {
            const IrSimdShape shape = width == 16 ? IR_SIMD_V4I32 : width == 32 ? IR_SIMD_V8I32 : IR_SIMD_V16I32;
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(operations); k += 1)
            {
                for (uint32_t encoding = 0; encoding < 3; encoding += 1)
                {
                    // A 64-byte register has EVEX alone; the helper also treats it as EVEX when a consumer requests
                    // VEX for its narrower registers, as the AVX512F-without-VL target does.
                    if (width == 64 && encoding != BUSTER_TIER1_ENCODING_EVEX)
                    {
                        continue;
                    }
                    const bool evex = encoding == BUSTER_TIER1_ENCODING_EVEX;
                    const bool signed_dword = d == 0 && (k == 0 || k == 2);
                    const bool exists = evex || (signed_dword && (encoding == BUSTER_TIER1_ENCODING_VEX || width == 16));
                    const uint16_t form = tier1_select_packed_convert_form(operations[k], lane, width, encoding);
                    tier1_select_test_expect(test, (form != X86_FORM_NONE) == exists, "packed conversions have exactly their SSE, VEX or EVEX form");
                    if (form == X86_FORM_NONE)
                    {
                        continue;
                    }
                    tier1_select_test_expect(test, x86_effects(form)->operation != X86_OPERATION_NONE, "a packed conversion has certificate effects");
                    tier1_select_test_expect(test, x86_target_form_allowed(baseline, form) == (signed_dword && encoding == BUSTER_TIER1_ENCODING_SSE && width == 16),
                                             "the baseline admits only signed dword SSE conversions");
                    tier1_select_test_expect(test, x86_target_form_allowed(v3, form) == (signed_dword && !evex), "x86-64-v3 admits signed dword SSE and VEX conversions");
                    tier1_select_test_expect(test, x86_target_form_allowed(no_dq, form) == (d == 0), "qword lane conversions require AVX512DQ");
                    tier1_select_test_expect(test, x86_target_form_allowed(no_vl, form) == (!evex || width == 64), "narrow EVEX conversions require AVX512VL");
                    for (uint32_t same = 0; same < 2; same += 1)
                    {
                        const Tier1SelectHoles holes =
                        {
                            .rule = TIER1_RULE_VECTOR_CONVERT,
                            .operation = (X86Operation)operations[k],
                            .width = (uint8_t)width,
                            .source_width = (uint8_t)lane,
                            .destination = (uint8_t)(same ? 33 : 34),
                            .left = 33,
                            .vex = encoding == BUSTER_TIER1_ENCODING_VEX,
                            .sse = encoding == BUSTER_TIER1_ENCODING_SSE,
                        };
                        if (tier1_select_test_packed_code(test, &holes))
                        {
                            const IrSimdAction action =
                            {
                                .operation = modeled[k],
                                .result = shape,
                                .operands =
                                {
                                    shape,
                                    IR_SIMD_NONE,
                                    IR_SIMD_NONE,
                                },
                                .lane = lane,
                            };
                            for (uint32_t sample = 0; sample < 8; sample += 1)
                            {
                                IrSimdValue inputs[4] =
                                {
                                    0,
                                };
                                for (uint32_t i = 0; i < width * 8 / bits; i += 1)
                                {
                                    const uint32_t index = (sample + i) % 8;
                                    const uint64_t value = k < 2 ? integers[d][index] : floating[d][k - 2][index];
                                    ir_simd_lane_write(&inputs[1], bits, i, value);
                                }
                                IrSimdValue expected =
                                {
                                    0,
                                };
                                const IrSimdValue* values[3] =
                                {
                                    &inputs[1],
                                    0,
                                    0,
                                };
                                const bool modeled_ok = ir_simd_evaluate(&action, values, 0, 0, 0, &expected);
                                tier1_select_test_expect(test, modeled_ok, "packed conversion inputs are in the IR model's domain");
                                uint8_t actual[80];
                                memset(actual, 0xa5, sizeof(actual));
                                tier1_select_test_call(test, (uintptr_t)inputs, (uintptr_t)actual, 0);
                                bool same_bits = modeled_ok && memcmp(actual, &expected, width) == 0;
                                for (uint32_t byte = width; byte < sizeof(actual); byte += 1)
                                {
                                    same_bits &= actual[byte] == 0xa5;
                                }
                                tier1_select_test_expect(test, same_bits, "packed signed and unsigned conversion lanes agree with the IR model");
                            }
                        }
                    }
                }
            }
        }
    }
}

static void tier1_select_test_packed_fmas(Tier1SelectTest* test)
{
    static const uint32_t fused[4] =
    {
        13,
        14,
        3,
        4,
    };
    static const uint64_t samples[2][16] =
    {
        {
            0,
            0x80000000u,
            0x3f800000u,
            0xbf800000u,
            0x40000000u,
            0x3f000000u,
            0x7f7fffffu,
            1,
            0x7f800000u,
            0xff800000u,
            0x7fc01234u,
            0xff801234u,
            0xffc05678u,
            0x3f800001u,
            0x3f7ffffeu,
            0x00800000u,
        },
        {
            0,
            UINT64_C(0x8000000000000000),
            UINT64_C(0x3ff0000000000000),
            UINT64_C(0xbff0000000000000),
            UINT64_C(0x4000000000000000),
            UINT64_C(0x3fe0000000000000),
            UINT64_C(0x7fefffffffffffff),
            1,
            UINT64_C(0x7ff0000000000000),
            UINT64_C(0xfff0000000000000),
            UINT64_C(0x7ff8000000001234),
            UINT64_C(0xfff0000000001234),
            UINT64_C(0xfff8000000005678),
            UINT64_C(0x3ff0000000000001),
            UINT64_C(0x3feffffffffffffe),
            UINT64_C(0x0010000000000000),
        },
    };
    for (uint32_t d = 0; d < 2; d += 1)
    {
        const IrSimdLane lane = d == 0 ? IR_SIMD_LANE_F32 : IR_SIMD_LANE_F64;
        const uint32_t bits = d == 0 ? 32 : 64;
        for (uint32_t width = 16; width <= 64; width *= 2)
        {
            const IrSimdShape shape = width == 16 ? IR_SIMD_V4I32 : width == 32 ? IR_SIMD_V8I32 : IR_SIMD_V16I32;
            const IrSimdAction action =
            {
                .operation = IR_SIMD_FMA,
                .result = shape,
                .operands =
                {
                    shape,
                    shape,
                    shape,
                },
                .lane = lane,
            };
            for (uint32_t encoding = 0; encoding < (width == 64 ? 1u : 2u); encoding += 1)
            {
                // Every alias partition of destination, a, b and c among four allocatable registers; notably x==a==c
                // with b elsewhere, whose saved a and addend must both use the scratch after b overwrites x.
                for (uint32_t assignment = 0; assignment < 256; assignment += 1)
                {
                    const uint8_t x = assignment & 3;
                    const uint8_t a = (assignment >> 2) & 3;
                    const uint8_t b = (assignment >> 4) & 3;
                    const uint8_t c = (assignment >> 6) & 3;
                    const Tier1SelectHoles holes =
                    {
                        .rule = TIER1_RULE_VECTOR_FMA,
                        .width = (uint8_t)width,
                        .source_width = (uint8_t)lane,
                        .destination = (uint8_t)(32 + x),
                        .left = (uint8_t)(32 + a),
                        .right = (uint8_t)(32 + b),
                        .third = (uint8_t)(32 + c),
                        .vex = encoding == BUSTER_TIER1_ENCODING_VEX,
                    };
                    if (tier1_select_test_packed_code(test, &holes))
                    {
                        for (uint32_t sample = 0; sample <= 16; sample += 1)
                        {
                            IrSimdValue inputs[4] =
                            {
                                0,
                            };
                            for (uint32_t r = 0; r < 4; r += 1)
                            {
                                for (uint32_t i = 0; i < width * 8 / bits; i += 1)
                                {
                                    // The last input explicitly computes (1+ulp)*(1-ulp)-1 for a=0,b=1,c=2:
                                    // its nonzero result distinguishes one fused rounding from multiply then add.
                                    const uint32_t index = sample == 16 ? fused[r] : (sample + r * 5 + i) % 16;
                                    ir_simd_lane_write(&inputs[r], bits, i, samples[d][index]);
                                }
                            }
                            IrSimdValue expected =
                            {
                                0,
                            };
                            const IrSimdValue* values[3] =
                            {
                                &inputs[a],
                                &inputs[b],
                                &inputs[c],
                            };
                            const bool modeled = ir_simd_evaluate(&action, values, 0, 0, 0, &expected);
                            IrSimdValue actual =
                            {
                                0,
                            };
                            tier1_select_test_call(test, (uintptr_t)inputs, (uintptr_t)&actual, 0);
                            tier1_select_test_expect(test, modeled && memcmp(&actual, &expected, width) == 0, "packed FMA preserves aliased operands and the IR's ordered NaN rule");
                        }
                    }
                }
            }
        }
    }
}

// This suite's records include VEX and EVEX forms, so they are outside the baseline manifest, as the BMI fixtures
// are. Keep their semantics and complete-cost coverage in the totals, including forms not yet measured on Zen 5.
static void tier1_select_test_packed_float(Tier1SelectTest* test)
{
    uint8_t* const baseline = test->allowed;
    uint8_t* const tables = malloc(5u * X86_FORM_COUNT);
    X86Target target =
    {
        0,
    };
    X86Target v3 =
    {
        0,
    };
    const X86Target host = x86_target_native();
    const bool ready = tables != 0 && x86_target_parse("x86-64-v4", 9, &target) && x86_target_parse("x86-64-v3", 9, &v3);
    tier1_select_test_expect(test, ready, "packed float target tables");
    const bool manifest = test->manifest;
    uint64_t emitted_records[3];
    uint64_t unknown_costs[3];
    uint64_t missing_columns[3][X86_COST_COLUMN_COUNT];
    memcpy(emitted_records, test->emitted_records, sizeof(emitted_records));
    memcpy(unknown_costs, test->unknown_costs, sizeof(unknown_costs));
    memcpy(missing_columns, test->missing_columns, sizeof(missing_columns));
    const uint32_t saved_mxcsr = _mm_getcsr();
    _mm_setcsr(0x1f80);
    if (ready)
    {
        uint8_t* const allowed = tables;
        uint8_t* const native = tables + X86_FORM_COUNT;
        uint8_t* const v3_forms = tables + 2u * X86_FORM_COUNT;
        uint8_t* const no_dq = tables + 3u * X86_FORM_COUNT;
        uint8_t* const no_vl = tables + 4u * X86_FORM_COUNT;
        x86_target_forms(&target, allowed);
        x86_target_forms(&host, native);
        x86_target_forms(&v3, v3_forms);
        target.features &= ~BUSTER_X86_FEATURE(AVX512DQ);
        x86_target_forms(&target, no_dq);
        target.features |= BUSTER_X86_FEATURE(AVX512DQ);
        target.features &= ~BUSTER_X86_FEATURE(AVX512VL);
        x86_target_forms(&target, no_vl);
        test->allowed = allowed;
        test->native_allowed = native;
        test->manifest = false;
        tier1_select_test_packed_conversions(test, baseline, v3_forms, no_dq, no_vl);
        tier1_select_test_packed_fmas(test);
        test->allowed = baseline;
        test->native_allowed = 0;
    }
    _mm_setcsr(saved_mxcsr);
    test->manifest = manifest;
    memcpy(test->emitted_records, emitted_records, sizeof(emitted_records));
    memcpy(test->unknown_costs, unknown_costs, sizeof(unknown_costs));
    memcpy(test->missing_columns, missing_columns, sizeof(missing_columns));
    free(tables);
}
