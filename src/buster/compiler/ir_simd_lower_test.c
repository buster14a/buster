// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Actual #717 producer rows through whole typed functions, both IR lowerers, shared interpretation, both Tier 0
// compilers and both encoders. Native execution is gated independently of the compiler host build. The expected
// lanes below are ordinary unsigned C arithmetic and signed mathematical comparisons, not the IR action oracle.

static __attribute__((no_sanitize("function"))) void ir_simd_lower_native(const void* code, Tier0TestVectorShared* shared)
{
    typedef void (*Body)(void*);
    Body body = 0;
    memcpy(&body, &code, sizeof(body));
    body(shared->out[0] + 8);
}

static uint32_t ir_simd_lower_expected(CheckOp op, uint32_t a, uint32_t b, bool unsigned_lanes)
{
    const int64_t sa = a < UINT32_C(0x80000000) ? (int64_t)a : (int64_t)a - INT64_C(4294967296);
    const int64_t sb = b < UINT32_C(0x80000000) ? (int64_t)b : (int64_t)b - INT64_C(4294967296);
    uint32_t value = 0;
    switch (op)
    {
        case CHECK_OP_ADD: value = a + b; break;
        case CHECK_OP_SUBTRACT: value = a - b; break;
        case CHECK_OP_MULTIPLY: value = a * b; break;
        case CHECK_OP_BIT_AND: value = a & b; break;
        case CHECK_OP_BIT_OR: value = a | b; break;
        case CHECK_OP_BIT_XOR: value = a ^ b; break;
        case CHECK_OP_BIT_NOT: value = ~a; break;
        case CHECK_OP_LESS: value = 0u - (unsigned_lanes ? a < b : sa < sb); break;
        case CHECK_OP_LESS_EQUAL: value = 0u - (unsigned_lanes ? a <= b : sa <= sb); break;
        case CHECK_OP_GREATER: value = 0u - (unsigned_lanes ? a > b : sa > sb); break;
        case CHECK_OP_GREATER_EQUAL: value = 0u - (unsigned_lanes ? a >= b : sa >= sb); break;
        case CHECK_OP_EQUAL: value = 0u - (a == b); break;
        case CHECK_OP_NOT_EQUAL: value = 0u - (a != b); break;
        default: break;
    }
    return value;
}

static void ir_simd_lower_tests(IrLowerTestTally* tally)
{
    TypeTestTable table = {0};
    Arena output = arena_reserve(UINT64_C(1) << 24);
    Arena twin = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 26);
    Arena execution = arena_reserve(UINT64_C(1) << 26);
    Arena native_arena = arena_reserve(UINT64_C(1) << 26);
    Tier0TestVectorShared* shared = mmap(0, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    uint8_t* images = calloc(2, 4 * BUSTER_TIER0_TEST_PAGE);
    const bool ready = type_test_table_open(&table, 256) && output.base != 0 && twin.base != 0 && scratch.base != 0 && execution.base != 0 && native_arena.base != 0 && shared != MAP_FAILED && images != 0;
    ir_lower_test_expect(tally, ready, "SIMD producer integration storage", 0, 0);
    const uint64_t features = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512DQ);
    const X86Target target = { .features = features };
    const X86Target host = x86_target_native();
    const bool native = (host.features & features) == features;
    static const CheckOp ops[] = {
        CHECK_OP_ADD, CHECK_OP_SUBTRACT, CHECK_OP_MULTIPLY, CHECK_OP_BIT_AND, CHECK_OP_BIT_OR, CHECK_OP_BIT_XOR,
        CHECK_OP_BIT_NOT, CHECK_OP_LESS, CHECK_OP_LESS_EQUAL, CHECK_OP_GREATER, CHECK_OP_GREATER_EQUAL, CHECK_OP_EQUAL, CHECK_OP_NOT_EQUAL,
    };
    const uint8_t local_classes[] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID };
    const uint8_t local_flags[] = { 0, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN };
    // Deliberately not vector-aligned: the typed object's guarantee is one byte, not its value's width.
    const uint8_t local_alignments[] = { 3, 0 };
    const AbiLocation parameter = { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE } };
    uint32_t cases = 0;
    uint32_t native_cases = 0;
    for (uint32_t width = 0; ready && width < 3; width += 1)
    {
        const uint32_t bytes = 16u << width;
        const uint32_t local_sizes[] = { 0, bytes };
        const uint8_t constant_bytes[] = { bytes, bytes };
        uint64_t constants[16];
        memset(constants, 0xcd, sizeof(constants));
        uint32_t lanes[2][16];
        for (uint32_t lane = 0; lane < bytes / 4; lane += 1)
        {
            lanes[0][lane] = UINT32_C(0xffffffff) - lane * UINT32_C(0x12345679);
            lanes[1][lane] = lane % 3 == 0 ? lanes[0][lane] : lane * UINT32_C(0x76543211);
        }
        memcpy(constants, lanes[0], bytes);
        memcpy(constants + 8, lanes[1], bytes);
        for (uint32_t unsigned_lanes = 0; unsigned_lanes < 2; unsigned_lanes += 1)
        {
            const TypeId type = type_vector(&table.table, unsigned_lanes ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT, bytes / 4, false);
            for (uint32_t operation = 0; operation < BUSTER_ARRAY_LENGTH(ops); operation += 1)
            {
                Tier0Operation row = {0};
                const char* reason = check_vector_lower_row(&table.table, &target, false, ops[operation], type, BUSTER_TYPED_CLASS_VOID, &row);
                ir_lower_test_expect(tally, reason == 0, "the actual vector producer builds an initial-subset row", width, operation);
                for (uint32_t condition = 0; reason == 0 && condition < 2; condition += 1)
                {
                    uint8_t kinds[] = {
                        BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONST, BUSTER_TYPED_DUP, BUSTER_TYPED_SET_LOCAL,
                        BUSTER_TYPED_CONST, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CONST, BUSTER_TYPED_JUMP_VALUE,
                        BUSTER_TYPED_LOCAL, BUSTER_TYPED_DISCARD, BUSTER_TYPED_X86, BUSTER_TYPED_STORE_LOCAL,
                        BUSTER_TYPED_DUP, BUSTER_TYPED_DISCARD, BUSTER_TYPED_STORE, BUSTER_TYPED_DISCARD,
                        BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_LOAD, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_RETURN,
                    };
                    uint8_t classes[] = {
                        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR,
                        BUSTER_TYPED_CLASS_U32, BUSTER_TYPED_CLASS_U32, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR,
                        BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR,
                        BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR,
                        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VOID,
                    };
                    uint64_t values[] = { 0, 0, 0, 1, condition, 8, 1, 9, 1, 0, 0, 1, 0, 0, bytes, 0, 1, bytes, 1, 0 };
                    // Binary rows consume the second operand; unary rows discard it at the join first. For a binary
                    // row the join executes X86, then duplicates/stores its result, retaining the same CFG.
                    if (row.operand_count == 2)
                    {
                        kinds[9] = BUSTER_TYPED_X86;
                        kinds[10] = BUSTER_TYPED_DUP;
                        kinds[11] = BUSTER_TYPED_SET_LOCAL;
                    }
                    Tier0Function source = {
                        .kinds = kinds,
                        .classes = classes,
                        .values = values,
                        .count = BUSTER_ARRAY_LENGTH(kinds),
                        .local_classes = local_classes,
                        .local_flags = local_flags,
                        .local_count = 2,
                        .parameter_count = 1,
                        .parameters = &parameter,
                        .result = { .kind = BUSTER_ABI_RESULT_IGNORE },
                        .local_sizes = local_sizes,
                        .local_alignments = local_alignments,
                        .vector_constants = constants,
                        .vector_constant_bytes = constant_bytes,
                        .vector_constant_count = 2,
                        .operations = &row,
                        .operation_count = 1,
                        .vectors = true,
                    };
                    IrFunction a = {0};
                    IrFunction b = {0};
                    const IrCheck ca = ir_lower_typed_target(&source, IR_VOID, 3, features, &output, &scratch, &a);
                    const IrCheck cb = ir_lower_typed_target_scalar(&source, IR_VOID, 3, features, &twin, &scratch, &b);
                    const bool lowered = ca.error == IR_OK && cb.error == IR_OK && ir_lower_test_equal(&a, &b);
                    ir_lower_test_expect(tally, lowered, "whole VECTOR/X86 functions lower identically with width-qualified edge spills", width, operation);
                    if (!lowered)
                    {
                        fprintf(stderr, "  SIMD lowering: errors %u/%u entry %u detail %u; typed %u at %u (%s)\n", ca.error, cb.error, ca.instruction, ca.detail, tier0_validate(&source).error, tier0_validate(&source).node, tier0_validate(&source).rule);
                    }
                    uint8_t expected[80];
                    memset(expected, 0xa5, sizeof(expected));
                    for (uint32_t lane = 0; lane < bytes / 4; lane += 1)
                    {
                        const uint32_t value = ir_simd_lower_expected(ops[operation], lanes[0][lane], lanes[condition][lane], unsigned_lanes != 0);
                        memcpy(expected + 8 + lane * 4, &value, 4);
                    }
                    for (uint32_t path = 0; lowered && path < 2; path += 1)
                    {
                        uint8_t actual[80];
                        memset(actual, 0xa5, sizeof(actual));
                        const InterpreterBinding binding = { .symbol = 2, .bytes = actual, .size = sizeof(actual), .writable = true };
                        const IrInterpreterFunction function = { .symbol = 1, .function = path == 0 ? &a : &b };
                        const IrInterpreterProgram program = { .functions = &function, .function_count = 1, .bindings = &binding, .binding_count = 1 };
                        const InterpreterValue argument = { .low = (uintptr_t)(actual + 8) };
                        const InterpreterResult result = ir_interpret(&program, 1, &argument, 1, 0, &execution);
                        ir_lower_test_expect(tally, result.status == INTERPRETER_OK && memcmp(actual, expected, sizeof(actual)) == 0 && execution.length == 0,
                                             "shared IR arithmetic, C compare expansion, transports and canaries agree with ordinary C", width, operation);
                        if (result.status != INTERPRETER_OK)
                        {
                            fprintf(stderr, "  SIMD interpreter: status %u at %u\n", result.status, result.entry);
                        }
                    }
                    if (lowered)
                    {
                        Tier0TestTally native_tally = {0};
                        Tier0Error error = TIER0_OK;
                        const uint32_t length = tier0_test_vector_encode(&native_tally, "IR SIMD producer integration", &source, &native_arena, images, images + 4 * BUSTER_TIER0_TEST_PAGE, TIER0_OK, &error);
                        ir_lower_test_expect(tally, length != 0 && native_tally.failures == 0, "both Tier 0 compilers and encoders agree on the admitted producer", width, operation);
                        if (native && length != 0)
                        {
                            memset(shared->out, 0xa5, sizeof(shared->out));
                            const bool ran = tier0_test_vector_native(images, length, ir_simd_lower_native, shared);
                            ir_lower_test_expect(tally, ran && memcmp(shared->out, expected, sizeof(expected)) == 0, "native Tier 0 matches shared IR and independent lane expectations", width, operation);
                            native_cases += ran;
                        }
                        cases += 1;
                    }
                    // Baseline output is refused even on AVX-512 hosts; failed publication preserves both arenas and
                    // the caller's result, including after output has already been allocated by the positive case.
                    const uint64_t mark = output.length;
                    const IrFunction before = a;
                    const IrCheck denied = ir_lower_typed_target(&source, IR_VOID, 3, 0, &output, &scratch, &a);
                    ir_lower_test_expect(tally, denied.error == IR_ERROR_UNSUPPORTED && denied.detail == IR_SIMD_TYPED_TARGET &&
                                               memcmp(&a, &before, sizeof(a)) == 0 && output.length == mark && scratch.length == 0,
                                         "bad target features refuse transactionally before execution", width, operation);
                    arena_pop_to(&output, 0);
                    arena_pop_to(&twin, 0);
                    arena_pop_to(&native_arena, 0);
                }
            }
        }
    }
    ir_lower_test_expect(tally, cases == 3 * 2 * BUSTER_ARRAY_LENGTH(ops) * 2, "every initial-subset producer case was lowered and executed", cases, 0);
    printf("IR SIMD lowering: %u cases, %u gated native runs%s\n", cases, native_cases, native ? "" : " (AVX-512 native execution unavailable)");
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(*shared));
    }
    free(images);
    type_test_table_close(&table);
    arena_release(&native_arena);
    arena_release(&execution);
    arena_release(&scratch);
    arena_release(&twin);
    arena_release(&output);
}
