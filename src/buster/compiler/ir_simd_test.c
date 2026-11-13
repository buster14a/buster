// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original SIMD IR admission controls, included after ir_test.c's fixture helpers.

// A mask's bitcasts (#179): to and from the integer of its bits, and to itself, which is the boundary a mask's typed-stream
// conversions lower through (ir_typed_convert_mask), but no other: not a narrower or wider integer, a float, a pointer
// or another mask. The pairs written out, each way, beside the scalar bitcasts the rule kept.
static void ir_test_simd_bitcasts(IrTestTally* test, Arena* output, Arena* scratch)
{
    static const uint8_t pairs[][3] = {
        { IR_I8, IR_M8, true }, { IR_M8, IR_I8, true }, { IR_I16, IR_M16, true }, { IR_M16, IR_I16, true },
        { IR_I32, IR_M32, true }, { IR_M32, IR_I32, true }, { IR_I64, IR_M64, true }, { IR_M64, IR_I64, true },
        { IR_M8, IR_M8, true }, { IR_M64, IR_M64, true }, { IR_I32, IR_F32, true }, { IR_F64, IR_I64, true },
        { IR_I16, IR_M8, false }, { IR_M8, IR_I16, false }, { IR_I8, IR_M16, false }, { IR_M32, IR_I64, false },
        { IR_M16, IR_M8, false }, { IR_M32, IR_M16, false }, { IR_M8, IR_M64, false }, { IR_F32, IR_M32, false },
        { IR_M32, IR_F32, false }, { IR_F64, IR_M64, false }, { IR_M64, IR_F64, false }, { IR_PTR, IR_M64, false },
        { IR_M64, IR_PTR, false }, { IR_I1, IR_M8, false }, { IR_I128, IR_M64, false }, { IR_M16, IR_V4I32, false },
    };
    for (uint32_t p = 0; p < sizeof(pairs) / sizeof(*pairs); p += 1)
    {
        const uint64_t mark = output->length;
        IrFunction function = { 0 };
        const IrSizes sizes = { .instruction_count = 3, .block_count = 1 };
        if (ir_create(output, &sizes, &function) == IR_OK)
        {
            function.result_type = IR_VOID;
            function.block_offsets[1] = 3;
            ir_test_instruction(&function, 0, IR_UNDEF, (IrType)pairs[p][0], BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 1, IR_BITCAST, (IrType)pairs[p][1], 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 2, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_pair(test, &function, scratch, NULL, pairs[p][2] != 0,
                         pairs[p][2] != 0 ? "a mask's bitcast to or from the integer of its bits, or a scalar's of its width, is refused" :
                                            "a bitcast between a mask and anything but the integer of its bits or itself is accepted");
        }
        else
        {
            ir_test_expect(test, false, "a bitcast fixture allocates");
        }
        arena_pop_to(output, mark);
    }
}

// A row call (#141, IrCall.row): a masked byte addition's row of two vectors and a mask, its result a vector, and a
// masked store's of an address, a vector and a mask, of no result, each a call of no symbol whose types are its row's
// classes' (ir_row_type). Both verify, both scans find SIMD values, and the interpreter refuses them. Then each changed
// one way, each refused: a symbol, a row past the column, an argument or the result of another type, an argument too
// few, a variadic call, an indirect one, an SSE carrier's operand (#1280) of a vector's type, and a row's operand of a
// class no row call takes (a vector of 12 bytes); an SSE carrier's of an f64 or an f32 verifies.
static void ir_test_simd_rows(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    IrFunction function = { 0 };
    const IrSizes sizes = { .instruction_count = 7, .block_count = 1, .extra_count = 6, .call_count = 2, .call_type_count = 6, .parameter_count = 4, .row_count = 2 };
    const bool ready = ir_create(output, &sizes, &function) == IR_OK;
    ir_test_expect(test, ready, "a row call fixture allocates its rows");
    if (ready)
    {
        function.result_type = IR_V16I32;
        function.block_offsets[1] = 7;
        const uint8_t parameters[4] = { IR_V16I32, IR_V16I32, IR_M64, IR_I64 };
        memcpy(function.parameter_types, parameters, sizeof(parameters));
        Tier0Operation* add = &function.rows[0];
        add->records[0] = (X86Instruction){ .form = X86_FORM_VPADDB_Z_Z_ZM512, .reg = BUSTER_TIER0_ROLE_VECTOR(0), .vvvv = BUSTER_TIER0_ROLE_VECTOR(0), .rm = BUSTER_TIER0_ROLE_VECTOR(1),
                                            .mode = BUSTER_X86_REGISTER_DIRECT, .decorations = BUSTER_TIER0_DECORATION_MASK(2) };
        add->record_count = 1;
        add->operand_count = 3;
        add->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        add->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
        add->operand_classes[2] = BUSTER_TYPED_CLASS_MASK;
        add->operand_bytes[0] = 64;
        add->operand_bytes[1] = 64;
        add->operand_bytes[2] = 8;
        add->result_class = BUSTER_TYPED_CLASS_VECTOR;
        add->result_bytes = 64;
        Tier0Operation* store = &function.rows[1];
        store->records[0] = (X86Instruction){ .form = X86_FORM_VMOVDQU8_ZM512_Z, .reg = BUSTER_TIER0_ROLE_VECTOR(1), .rm = BUSTER_TIER0_ROLE_INTEGER(0), .mode = BUSTER_X86_MEMORY_BASE,
                                              .decorations = BUSTER_TIER0_DECORATION_MASK(2) };
        store->record_count = 1;
        store->operand_count = 3;
        store->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
        store->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
        store->operand_classes[2] = BUSTER_TYPED_CLASS_MASK;
        store->operand_bytes[1] = 64;
        store->operand_bytes[2] = 8;
        store->result_class = BUSTER_TYPED_CLASS_VOID;
        const uint32_t extra[6] = { 0, 1, 2, 3, 4, 2 };
        memcpy(function.extra, extra, sizeof(extra));
        const uint8_t types[6] = { IR_V16I32, IR_V16I32, IR_M64, IR_I64, IR_V16I32, IR_M64 };
        memcpy(function.call_types, types, sizeof(types));
        function.calls[0] = (IrCall){ .argument_first = 0, .argument_count = 3, .type_first = 0, .parameter_count = 3, .result_type = IR_V16I32, .row = 1 };
        function.calls[1] = (IrCall){ .argument_first = 3, .argument_count = 3, .type_first = 3, .parameter_count = 3, .result_type = IR_VOID, .row = 2 };
        for (uint32_t p = 0; p < 4; p += 1)
        {
            ir_test_instruction(&function, p, IR_PARAM, (IrType)parameters[p], p, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        ir_test_instruction(&function, 4, IR_CALL, IR_V16I32, 0, 0, BUSTER_IR_NONE);
        ir_test_instruction(&function, 5, IR_CALL, IR_VOID, 0, 1, BUSTER_IR_NONE);
        ir_test_instruction(&function, 6, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_pair(test, &function, scratch, NULL, true, "row calls of a vector result and of none verify");
        ir_test_expect(test, ir_function_has_simd_path(&function, true) && ir_function_has_simd_path(&function, false) && ir_function_has_vectors_path(&function, true) &&
                       ir_function_has_vectors_path(&function, false), "a function with machine rows has SIMD values on both scans' paths");
        ir_test_expect(test, !ir_interpreter_simd_supported(&function), "the interpreter refuses a machine row, which it has no model of");
        ir_test_printer(test, &function, output, scratch);
        function.a[4] = 1;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call names no symbol");
        function.a[4] = 0;
        function.calls[0].row = 3;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call's row is in the rows' column");
        function.calls[0].row = 1;
        function.call_types[2] = IR_M32;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call's argument is of its operand's class's type");
        function.call_types[2] = IR_M64;
        function.type[4] = IR_V8I32;
        function.calls[0].result_type = IR_V8I32;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call's result is of its row's result's type");
        function.type[4] = IR_V16I32;
        function.calls[0].result_type = IR_V16I32;
        function.calls[0].argument_count = 2;
        function.calls[0].parameter_count = 2;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call has an argument per operand of its row");
        function.calls[0].argument_count = 3;
        function.calls[0].parameter_count = 3;
        function.calls[0].variadic = true;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call isn't variadic");
        function.calls[0].variadic = false;
        function.op[4] = IR_CALL_INDIRECT;
        ir_test_pair(test, &function, scratch, NULL, false, "a row call is a direct call's");
        function.op[4] = IR_CALL;
        // An SSE carrier (#1280): a vector operand of 8 bytes is an f64 argument, of 4 an f32, and not a v16i32; one
        // of 12 bytes is no row call's.
        add->operand_bytes[0] = 8;
        ir_test_pair(test, &function, scratch, NULL, false, "an 8-byte vector's operand is no v16i32");
        function.parameter_types[0] = IR_F64;
        function.type[0] = IR_F64;
        function.call_types[0] = IR_F64;
        ir_test_pair(test, &function, scratch, NULL, true, "an 8-byte vector's operand is an f64");
        add->operand_bytes[0] = 4;
        ir_test_pair(test, &function, scratch, NULL, false, "a 4-byte vector's operand is no f64");
        function.parameter_types[0] = IR_F32;
        function.type[0] = IR_F32;
        function.call_types[0] = IR_F32;
        ir_test_pair(test, &function, scratch, NULL, true, "a 4-byte vector's operand is an f32");
        add->operand_bytes[0] = 12;
        ir_test_pair(test, &function, scratch, NULL, false, "a vector of 12 bytes is no row call's operand");
        function.parameter_types[0] = IR_V16I32;
        function.type[0] = IR_V16I32;
        function.call_types[0] = IR_V16I32;
        add->operand_bytes[0] = 64;
        ir_test_pair(test, &function, scratch, NULL, true, "the row calls verify again once restored");
    }
    arena_pop_to(output, mark);
}

// A vector operation's lanes (#141's element types, its flags): each shape accepted and its neighbours refused, by both
// verifiers. Two vector parameters of `type` (v4i32, v8i32 or v16i32), then the operation under test on them, of
// `result` with flags `lane` and predicate `c` (vcmp's), and a ret.
typedef struct IrTestLaneCase IrTestLaneCase;
struct IrTestLaneCase
{
    uint8_t op;
    uint8_t type;
    uint8_t result;
    uint8_t lane;
    uint32_t c;
    // A shift's count of an i32 rather than a vector (`scalar_count`), and a vselect's mask (`mask`, else its result's).
    bool scalar_count;
    uint8_t mask;
    bool valid;
    const char* message;
};

static void ir_test_simd_lanes(IrTestTally* test, Arena* output, Arena* scratch)
{
    static const IrTestLaneCase cases[] = {
        { IR_ADD, IR_V4I32, IR_V4I32, IR_SIMD_LANE_I8, BUSTER_IR_NONE, false, 0, true, "add.v16i8 is refused" },
        { IR_SUB, IR_V16I32, IR_V16I32, IR_SIMD_LANE_I16, BUSTER_IR_NONE, false, 0, true, "sub.v32i16 is refused" },
        { IR_MUL, IR_V8I32, IR_V8I32, IR_SIMD_LANE_I64, BUSTER_IR_NONE, false, 0, true, "mul.v4i64 is refused" },
        { IR_SHL, IR_V4I32, IR_V4I32, IR_SIMD_LANE_I16, BUSTER_IR_NONE, false, 0, true, "shl.v8i16 by a vector of counts is refused" },
        { IR_ASHR, IR_V16I32, IR_V16I32, IR_SIMD_LANE_I32, BUSTER_IR_NONE, false, 0, true, "ashr.v16i32 by a vector of counts is refused" },
        { IR_FADD, IR_V4I32, IR_V4I32, IR_SIMD_LANE_F32, BUSTER_IR_NONE, false, 0, true, "fadd.v4f32 is refused" },
        { IR_FDIV, IR_V16I32, IR_V16I32, IR_SIMD_LANE_F64, BUSTER_IR_NONE, false, 0, true, "fdiv.v8f64 is refused" },
        { IR_VCMP, IR_V4I32, IR_M16, IR_SIMD_LANE_I8, IR_ULT, false, 0, true, "a comparison of 16 bytes into m16 is refused" },
        { IR_VCMP, IR_V16I32, IR_M64, IR_SIMD_LANE_I8, IR_EQ, false, 0, true, "a comparison of 64 bytes into m64 is refused" },
        { IR_VCMP, IR_V8I32, IR_M8, IR_SIMD_LANE_F64, IR_GE, false, 0, true, "a comparison of 4 doubles into m8 is refused" },
        { IR_VSELECT, IR_V8I32, IR_V8I32, IR_SIMD_LANE_I16, BUSTER_IR_NONE, false, IR_M16, true, "a selection of 16 words by m16 is refused" },
        { IR_FADD, IR_V4I32, IR_V4I32, IR_SIMD_LANE_I32, BUSTER_IR_NONE, false, 0, false, "fadd of a vector's default integer lanes is accepted" },
        { IR_ADD, IR_V4I32, IR_V4I32, IR_SIMD_LANE_F32, BUSTER_IR_NONE, false, 0, false, "an integer add of floating lanes is accepted" },
        { IR_ADD, IR_V4I32, IR_V4I32, IR_SIMD_LANE_COUNT, BUSTER_IR_NONE, false, 0, false, "lanes past the last kind are accepted" },
        { IR_XOR, IR_V4I32, IR_V4I32, IR_SIMD_LANE_I8, BUSTER_IR_NONE, false, 0, false, "a bitwise operation's lanes are accepted" },
        { IR_SHL, IR_V4I32, IR_V4I32, IR_SIMD_LANE_I16, BUSTER_IR_NONE, true, 0, false, "a vector's shift by a scalar count is accepted" },
        { IR_VCMP, IR_V4I32, IR_M8, IR_SIMD_LANE_I8, IR_EQ, false, 0, false, "a comparison of 16 bytes into m8 is accepted" },
        { IR_VCMP, IR_V4I32, IR_M8, IR_SIMD_LANE_F32, IR_ULT, false, 0, false, "an unsigned comparison of floats is accepted" },
        { IR_VSELECT, IR_V4I32, IR_V4I32, IR_SIMD_LANE_I8, BUSTER_IR_NONE, false, IR_M8, false, "a selection of 16 bytes by m8 is accepted" },
    };
    for (uint32_t k = 0; k < sizeof(cases) / sizeof(*cases); k += 1)
    {
        const IrTestLaneCase lane = cases[k];
        const uint64_t mark = output->length;
        IrFunction function = { 0 };
        const IrSizes sizes = { .instruction_count = 5, .block_count = 1, .parameter_count = 3 };
        if (ir_create(output, &sizes, &function) == IR_OK)
        {
            const uint8_t mask = lane.mask != 0 ? lane.mask : (uint8_t)ir_lane_mask(lane.result, lane.lane);
            function.parameter_types[0] = lane.type;
            function.parameter_types[1] = lane.scalar_count ? IR_I32 : lane.type;
            function.parameter_types[2] = mask != IR_VOID ? mask : IR_M8;
            function.result_type = IR_VOID;
            function.block_offsets[1] = 5;
            ir_test_instruction(&function, 0, IR_PARAM, lane.type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 1, IR_PARAM, (IrType)function.parameter_types[1], 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 2, IR_PARAM, (IrType)function.parameter_types[2], 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            if (lane.op == IR_VSELECT)
            {
                ir_test_instruction(&function, 3, IR_VSELECT, (IrType)lane.result, 2, 0, 0);
            }
            else
            {
                ir_test_instruction(&function, 3, (IrOp)lane.op, (IrType)lane.result, 0, 1, lane.op == IR_VCMP ? lane.c : BUSTER_IR_NONE);
            }
            function.flags[3] = lane.lane;
            ir_test_instruction(&function, 4, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_pair(test, &function, scratch, NULL, lane.valid, lane.message);
        }
        else
        {
            ir_test_expect(test, false, "a lanes fixture allocates");
        }
        arena_pop_to(output, mark);
    }
    // A scalar's arithmetic names no lanes: an i32 add of flags I8 is refused.
    const uint64_t mark = output->length;
    IrFunction scalar = { 0 };
    const IrSizes sizes = { .instruction_count = 3, .block_count = 1, .parameter_count = 1 };
    if (ir_create(output, &sizes, &scalar) == IR_OK)
    {
        scalar.parameter_types[0] = IR_I32;
        scalar.result_type = IR_VOID;
        scalar.block_offsets[1] = 3;
        ir_test_instruction(&scalar, 0, IR_PARAM, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&scalar, 1, IR_ADD, IR_I32, 0, 0, BUSTER_IR_NONE);
        scalar.flags[1] = IR_SIMD_LANE_I8;
        ir_test_instruction(&scalar, 2, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_pair(test, &scalar, scratch, NULL, false, "a scalar add's lanes are accepted");
    }
    arena_pop_to(output, mark);
}

// One lane case for the executor: op (of lanes op_lane) on A and B, VCMP lanes select_lane A < B, VSELECT of the op where it holds, B elsewhere.
typedef struct IrTestLaneExecution
{
    IrOp op;
    IrSimdLane op_lane;
    IrSimdLane select_lane;
    uint64_t a[2];
    uint64_t b[2];
    const char* message;
} IrTestLaneExecution;

// The expected result, from C's own arithmetic on the lanes rather than the SIMD model's.
static void ir_test_lane_expected(const IrTestLaneExecution* lane_case, uint8_t expected[16])
{
    uint8_t a[16];
    uint8_t b[16];
    uint8_t operation[16];
    memcpy(a, lane_case->a, sizeof(a));
    memcpy(b, lane_case->b, sizeof(b));
    for (uint32_t lane = 0; lane < 16; lane += 1)
    {
        operation[lane] = (uint8_t)(a[lane] + b[lane]);
    }
    if (lane_case->op_lane == IR_SIMD_LANE_F32)
    {
        for (uint32_t lane = 0; lane < 4; lane += 1)
        {
            float x;
            float y;
            memcpy(&x, a + lane * 4, sizeof(x));
            memcpy(&y, b + lane * 4, sizeof(y));
            const float sum = x + y;
            memcpy(operation + lane * 4, &sum, sizeof(sum));
        }
    }
    const uint32_t bytes = lane_case->select_lane == IR_SIMD_LANE_F32 ? 4 : 2;
    for (uint32_t lane = 0; lane < 16 / bytes; lane += 1)
    {
        bool less = false;
        if (lane_case->select_lane == IR_SIMD_LANE_F32)
        {
            float x;
            float y;
            memcpy(&x, a + lane * 4, sizeof(x));
            memcpy(&y, b + lane * 4, sizeof(y));
            less = x < y;
        }
        else
        {
            int16_t x;
            int16_t y;
            memcpy(&x, a + lane * 2, sizeof(x));
            memcpy(&y, b + lane * 2, sizeof(y));
            less = x < y;
        }
        memcpy(expected + lane * bytes, (less ? operation : b) + lane * bytes, bytes);
    }
}

// Lane-typed operations run through ir_simd_execute as well as the model and the shared interpreter (#1413's review):
// their flags name lanes, so they must not read as a volatile access.
static void ir_test_simd_lane_execution(IrTestTally* test, Arena* output, Arena* scratch)
{
    static const IrTestLaneExecution cases[] = {
        {
            .op = IR_ADD,
            .op_lane = IR_SIMD_LANE_I8,
            .select_lane = IR_SIMD_LANE_I16,
            .a = { UINT64_C(0x80017fff00ff01fe), UINT64_C(0xfffe000100020003) },
            .b = { UINT64_C(0x7fff800100010102), UINT64_C(0x0001fffe00030002) },
            .message = "add.v16i8 wraps each byte and vcmp lanes=i16 selects its words through ir_simd_execute",
        },
        {
            .op = IR_FADD,
            .op_lane = IR_SIMD_LANE_F32,
            .select_lane = IR_SIMD_LANE_F32,
            // 1.5, -2.0, 3.25, 0.5 against 2.0, -3.0, 1.0, 0.75.
            .a = { UINT64_C(0xc00000003fc00000), UINT64_C(0x3f00000040500000) },
            .b = { UINT64_C(0xc040000040000000), UINT64_C(0x3f4000003f800000) },
            .message = "fadd.v4f32 and an ordered vcmp lanes=f32 select their floats through ir_simd_execute",
        },
    };
    const uint64_t mark = output->length;
    for (uint32_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index += 1)
    {
        const IrTestLaneExecution* lane_case = cases + index;
        IrFunction function = { 0 };
        const IrSizes sizes = { .instruction_count = 6, .block_count = 1, .vector_constant_count = 2 };
        if (ir_create(output, &sizes, &function) == IR_OK)
        {
            function.result_type = IR_V4I32;
            function.block_offsets[1] = 6;
            memcpy(function.vector_constants[0].words, lane_case->a, sizeof(lane_case->a));
            memcpy(function.vector_constants[1].words, lane_case->b, sizeof(lane_case->b));
            ir_test_instruction(&function, 0, IR_VCONST, IR_V4I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 1, IR_VCONST, IR_V4I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 2, lane_case->op, IR_V4I32, 0, 1, BUSTER_IR_NONE);
            ir_test_instruction(&function, 3, IR_VCMP, IR_M8, 0, 1, IR_LT);
            ir_test_instruction(&function, 4, IR_VSELECT, IR_V4I32, 3, 2, 1);
            ir_test_instruction(&function, 5, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function.flags[2] = lane_case->op_lane;
            function.flags[3] = lane_case->select_lane;
            function.flags[4] = lane_case->select_lane;
            ir_test_pair(test, &function, scratch, NULL, true, lane_case->message);
            uint8_t expected[16];
            ir_test_lane_expected(lane_case, expected);
            const IrSimdExecution executed = ir_simd_execute(&function, NULL, 0, NULL, scratch);
            ir_test_expect(test, executed.status == IR_SIMD_EXECUTION_OK && executed.type == IR_V4I32 && memcmp(executed.value.words, expected, sizeof(expected)) == 0,
                           lane_case->message);
        }
        arena_pop_to(output, mark);
    }
}

// The bit operations' intrinsics (#1374): one operand, an I32 or an I64 of the result's type, and no second one.
static void ir_test_bit_intrinsics(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    for (uint32_t operation = BUSTER_IR_INTRINSIC_TZCNT; operation <= BUSTER_IR_INTRINSIC_BSWAP; operation += 1)
    {
        for (uint32_t type = IR_I1; type < IR_TYPE_COUNT; type += 1)
        {
            IrFunction function = {0};
            const IrSizes sizes = {
                .instruction_count = 3,
                .block_count = 1,
                .parameter_count = 1,
            };
            const bool created = ir_create(output, &sizes, &function) == IR_OK;
            ir_test_expect(test, created, "bit intrinsic verifier fixture allocation");
            if (created)
            {
                const bool admitted = type == IR_I32 || type == IR_I64;
                function.result_type = IR_VOID;
                function.parameter_types[0] = (uint8_t)type;
                function.block_offsets[1] = 3;
                ir_test_instruction(&function, 0, IR_PARAM, (IrType)type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 1, IR_INTRINSIC, (IrType)type, 0, BUSTER_IR_NONE, operation);
                ir_test_instruction(&function, 2, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_pair(test, &function, scratch, NULL, admitted, "a bit intrinsic takes an I32 or an I64 operand of its result's type");
                if (admitted)
                {
                    ir_test_expect(test, ir_operand_count(&function, 1) == 1 && ir_value_mask(&function, 1) == 1, "a bit intrinsic has one SSA use");
                    function.b[1] = 0;
                    ir_test_pair(test, &function, scratch, NULL, false, "a bit intrinsic refuses a second operand");
                    function.b[1] = BUSTER_IR_NONE;
                    function.type[1] = type == IR_I32 ? IR_I64 : IR_I32;
                    ir_test_pair(test, &function, scratch, NULL, false, "a bit intrinsic's result has its operand's type");
                    function.type[1] = (uint8_t)type;
                    function.c[1] = BUSTER_IR_INTRINSIC_COUNT;
                    ir_test_pair(test, &function, scratch, NULL, false, "an intrinsic past the operations is refused");
                }
            }
            arena_pop_to(output, mark);
        }
    }
}

// The signed overflows' intrinsics (#1385): two operands, I32s or I64s of the result's type, both present.
static void ir_test_overflow_intrinsics(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    for (uint32_t operation = BUSTER_IR_INTRINSIC_SADDO; operation <= BUSTER_IR_INTRINSIC_SMULO; operation += 1)
    {
        for (uint32_t type = IR_I1; type < IR_TYPE_COUNT; type += 1)
        {
            IrFunction function = {0};
            const IrSizes sizes = {
                .instruction_count = 4,
                .block_count = 1,
                .parameter_count = 2,
            };
            const bool created = ir_create(output, &sizes, &function) == IR_OK;
            ir_test_expect(test, created, "overflow intrinsic verifier fixture allocation");
            if (created)
            {
                const bool admitted = type == IR_I32 || type == IR_I64;
                function.result_type = IR_VOID;
                function.parameter_types[0] = (uint8_t)type;
                function.parameter_types[1] = (uint8_t)type;
                function.block_offsets[1] = 4;
                ir_test_instruction(&function, 0, IR_PARAM, (IrType)type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 1, IR_PARAM, (IrType)type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 2, IR_INTRINSIC, (IrType)type, 0, 1, operation);
                ir_test_instruction(&function, 3, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_pair(test, &function, scratch, NULL, admitted, "an overflow intrinsic takes two I32 or I64 operands of its result's type");
                if (admitted)
                {
                    ir_test_expect(test, ir_operand_count(&function, 2) == 2 && ir_value_mask(&function, 2) == 3, "an overflow intrinsic has two SSA uses");
                    function.b[2] = BUSTER_IR_NONE;
                    ir_test_pair(test, &function, scratch, NULL, false, "an overflow intrinsic refuses a missing second operand");
                    function.b[2] = 1;
                    function.type[1] = type == IR_I32 ? IR_I64 : IR_I32;
                    function.parameter_types[1] = function.type[1];
                    ir_test_pair(test, &function, scratch, NULL, false, "an overflow intrinsic's operands have one type");
                    function.type[1] = (uint8_t)type;
                    function.parameter_types[1] = (uint8_t)type;
                    function.type[2] = type == IR_I32 ? IR_I64 : IR_I32;
                    ir_test_pair(test, &function, scratch, NULL, false, "an overflow intrinsic's result has its operands' type");
                }
            }
            arena_pop_to(output, mark);
        }
    }
}

static void ir_test_splats(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    for (uint32_t width = 0; width < 3; width += 1)
    {
        for (uint32_t scalar = IR_I1; scalar < IR_TYPE_COUNT; scalar += 1)
        {
            IrFunction function = {0};
            const IrSizes sizes = {
                .instruction_count = 3,
                .block_count = 1,
                .parameter_count = 1,
            };
            const bool created = ir_create(output, &sizes, &function) == IR_OK;
            ir_test_expect(test, created, "splat verifier fixture allocation");
            if (created)
            {
                const IrType type = (IrType)(IR_V4I32 + width);
                const bool admitted = (scalar >= IR_I8 && scalar <= IR_I64) || scalar == IR_F32 || scalar == IR_F64;
                function.result_type = IR_VOID;
                function.parameter_types[0] = (uint8_t)scalar;
                function.block_offsets[1] = 3;
                ir_test_instruction(&function, 0, IR_PARAM, (IrType)scalar, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 1, IR_INTRINSIC, type, 0, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_SPLAT);
                ir_test_instruction(&function, 2, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_pair(test, &function, scratch, NULL, admitted, "splat admission follows its scalar element type at every vector width");
                if (admitted)
                {
                    ir_test_expect(test, ir_operand_count(&function, 1) == 1 && ir_value_mask(&function, 1) == 1, "splat has one SSA use and no phantom second input");
                    IrSimdValue value;
                    memset(&value, 0xa5, sizeof(value));
                    const uint64_t bits = UINT64_C(0x7ff0000080000081);
                    bool exact = ir_splat_value(type, scalar, bits, &value);
                    const uint32_t bytes = ir_type_bits[scalar] / 8;
                    for (uint32_t byte = 0; exact && byte < sizeof(value); byte += 1)
                    {
                        const uint8_t expected = byte < (16u << width) ? (uint8_t)(bits >> (8 * (byte % bytes))) : 0;
                        exact = ((const uint8_t*)&value)[byte] == expected;
                    }
                    ir_test_expect(test, exact, "splat repeats raw bits at each element width and zeroes its sparse tail");
                    function.b[1] = 0;
                    ir_test_pair(test, &function, scratch, NULL, false, "splat refuses a second operand");
                    function.b[1] = BUSTER_IR_NONE;
                    function.flags[1] = 1;
                    ir_test_pair(test, &function, scratch, NULL, false, "splat's scalar type fixes its element width; lane flags are refused");
                    function.flags[1] = 0;
                    function.c[1] = BUSTER_IR_INTRINSIC_PDEP;
                    function.b[1] = 0;
                    ir_test_pair(test, &function, scratch, NULL, false, "a scalar BMI intrinsic cannot acquire a vector result");
                    function.c[1] = BUSTER_IR_INTRINSIC_SPLAT;
                    function.type[1] = IR_I32;
                    function.b[1] = BUSTER_IR_NONE;
                    ir_test_pair(test, &function, scratch, NULL, false, "splat requires a vector result");
                }
            }
            arena_pop_to(output, mark);
        }
    }
}

// The width changes (#1840): extract, widen and insert of every pair of vector widths and every part to 4, admitted by
// both verifiers exactly where the result's, the operands' and the part's widths agree (an extract's narrower part of
// its wider operand, a widen's wider result, an insert's narrower part into its own type), every other kind's flags
// refused; and the model's values against bytes written out: an extract's the operand's part, a widen's the operand and
// zeros past it, an insert's the first operand with its part the second's.
static void ir_test_width_changes(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    uint32_t admitted_count = 0;
    uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
    for (uint32_t kind = BUSTER_IR_INTRINSIC_EXTRACT; kind <= BUSTER_IR_INTRINSIC_INSERT; kind += 1)
    {
        for (uint32_t result = 0; result < 3; result += 1)
        {
            for (uint32_t first = 0; first < 3; first += 1)
            {
                for (uint32_t second = 0; second < 3; second += 1)
                {
                    for (uint32_t part = 0; part < 5; part += 1)
                    {
                        const bool insert = kind == BUSTER_IR_INTRINSIC_INSERT;
                        if (!insert && second != 0)
                        {
                            continue;
                        }
                        IrFunction function = {0};
                        const IrSizes sizes = { .instruction_count = 4, .block_count = 1 };
                        const bool created = ir_create(output, &sizes, &function) == IR_OK;
                        ir_test_expect(test, created, "width change verifier fixture allocation");
                        if (!created)
                        {
                            continue;
                        }
                        const IrType type = (IrType)(IR_V4I32 + result);
                        const IrType a = (IrType)(IR_V4I32 + first);
                        const IrType b = (IrType)(IR_V4I32 + second);
                        const uint32_t result_bytes = 16u << result;
                        const uint32_t a_bytes = 16u << first;
                        const uint32_t b_bytes = 16u << second;
                        const bool admitted = kind == BUSTER_IR_INTRINSIC_EXTRACT ? result_bytes < a_bytes && part < a_bytes / result_bytes :
                                              kind == BUSTER_IR_INTRINSIC_WIDEN   ? a_bytes < result_bytes && part == 0 :
                                                                                    a == type && b_bytes < result_bytes && part < result_bytes / b_bytes;
                        admitted_count += admitted;
                        function.result_type = IR_VOID;
                        function.block_offsets[1] = 4;
                        ir_test_instruction(&function, 0, IR_UNDEF, a, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        ir_test_instruction(&function, 1, IR_UNDEF, b, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        ir_test_instruction(&function, 2, IR_INTRINSIC, type, 0, insert ? 1 : BUSTER_IR_NONE, kind);
                        ir_test_instruction(&function, 3, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        function.flags[2] = (uint8_t)part;
                        ir_test_pair(test, &function, scratch, NULL, admitted, "a width change is admitted exactly where its widths and part agree");
                        if (admitted)
                        {
                            ir_test_expect(test, ir_value_mask(&function, 2) == (insert ? 3u : 1u), "a width change uses its operands alone, its part in flags");
                            function.b[2] = insert ? BUSTER_IR_NONE : 0;
                            ir_test_pair(test, &function, scratch, NULL, false, "an insert needs its second operand and the others refuse one");
                            function.b[2] = insert ? 1 : BUSTER_IR_NONE;
                            function.c[2] = BUSTER_IR_INTRINSIC_SPLAT;
                            ir_test_pair(test, &function, scratch, NULL, false, "another kind's flags are refused");
                            function.c[2] = kind;
                            // The model, against bytes written out.
                            IrSimdValue values[2];
                            for (uint32_t w = 0; w < 8; w += 1)
                            {
                                state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                                values[0].words[w] = w < a_bytes / 8 ? state : 0;
                                values[1].words[w] = w < b_bytes / 8 ? ~state * 3 : 0;
                            }
                            const IrSimdValue* operands[3] = { &values[0], insert ? &values[1] : 0, 0 };
                            const IrSimdAction action = {
                                .operation = kind == BUSTER_IR_INTRINSIC_EXTRACT ? IR_SIMD_EXTRACT : kind == BUSTER_IR_INTRINSIC_WIDEN ? IR_SIMD_WIDEN : IR_SIMD_INSERT,
                                .result = ir_simd_shape(type),
                                .operands = { ir_simd_shape(a), insert ? ir_simd_shape(b) : IR_SIMD_NONE, IR_SIMD_NONE },
                                .index = (uint8_t)part,
                            };
                            IrSimdValue value;
                            memset(&value, 0xa5, sizeof(value));
                            bool exact = ir_simd_evaluate(&action, operands, 0, 0, 0, &value);
                            const uint8_t* bytes_a = (const uint8_t*)values[0].words;
                            const uint8_t* bytes_b = (const uint8_t*)values[1].words;
                            for (uint32_t byte = 0; exact && byte < 64; byte += 1)
                            {
                                const uint32_t start = insert ? part * b_bytes : 0;
                                const uint8_t expected = byte >= result_bytes                                 ? 0 :
                                                         kind == BUSTER_IR_INTRINSIC_EXTRACT                  ? bytes_a[part * result_bytes + byte] :
                                                         kind == BUSTER_IR_INTRINSIC_WIDEN                    ? (byte < a_bytes ? bytes_a[byte] : 0) :
                                                         byte >= start && byte < start + b_bytes             ? bytes_b[byte - start] :
                                                                                                                bytes_a[byte];
                                exact = ((const uint8_t*)value.words)[byte] == expected;
                            }
                            ir_test_expect(test, exact, "the model moves a width change's bytes whole and zeroes its sparse tail");
                            IrSimdAction wrong = action;
                            wrong.lane = IR_SIMD_LANE_I8;
                            ir_test_expect(test, !ir_simd_evaluate(&wrong, operands, 0, 0, 0, &value), "a width change reads no lanes");
                        }
                        arena_pop_to(output, mark);
                    }
                }
            }
        }
    }
    // Extracts: 16 of 32 (2 parts), 16 of 64 (4), 32 of 64 (2); widens: 3 pairs; inserts: the same 8 parts as extracts.
    ir_test_expect(test, admitted_count == 8 + 3 + 8, "every width change of the IR's vector widths is admitted, and no other");
}

// An independent byte model of the permutes (#1840) for the model's check: byte `byte` of a permute of a and b of
// `bytes` bytes, kind past BUSTER_IR_INTRINSIC_PERMUTE, by the IR's flags.
static uint8_t ir_test_permute_byte(uint32_t kind, uint32_t flags, uint32_t bytes, const uint8_t* a, const uint8_t* b, uint32_t byte)
{
    uint8_t value = 0;
    if (kind == 0)
    {
        const uint32_t size = 1u << flags;
        uint64_t index = 0;
        for (uint32_t k = 0; k < size; k += 1)
        {
            index |= (uint64_t)b[byte / size * size + k] << (8 * k);
        }
        value = a[(uint32_t)(index % (bytes / size)) * size + byte % size];
    }
    else if (kind == 1)
    {
        value = b[byte] >= 0x80 ? 0 : a[byte / 16 * 16 + b[byte] % 16];
    }
    else if (kind == 2)
    {
        // b's elements, then a's: the 2 * bytes of their concatenation from the count's elements on.
        const uint32_t size = 1u << (flags >> 4);
        const uint32_t at = byte + (flags & 15) * size;
        value = at < bytes ? b[at] : a[at - bytes];
    }
    else
    {
        const uint32_t lanes = bytes / 16;
        const uint32_t lane = byte / 16;
        const uint32_t bits = lanes == 2 ? 1 : 2;
        const uint32_t selected = (flags >> (bits * lane)) % lanes;
        value = (lane < lanes / 2 ? a : b)[selected * 16 + byte % 16];
    }
    return value;
}

// The permutes (#1840): permute, shuffle, align and shuffle_lanes of two vectors at every width and every flags byte,
// admitted by both verifiers exactly where ir_permute_flags_valid's rule, written out again here, says (permute's lanes'
// sizes 0 to 3; shuffle's 0; align's sizes 2 and 3 in bits 4 and 5 with a count below the lanes; shuffle_lanes' 2 bits of a
// 32-byte vector's, any byte of a 64-byte one's, none of 16), refused with operands of another type or a missing b; and
// the model's bytes against ir_test_permute_byte on random vectors.
static void ir_test_permutes(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    uint32_t admitted_count = 0;
    uint64_t state = UINT64_C(0xd1b54a32d192ed03);
    for (uint32_t kind = 0; kind < 4; kind += 1)
    {
        for (uint32_t width = 0; width < 3; width += 1)
        {
            const uint32_t bytes = 16u << width;
            for (uint32_t flags = 0; flags < 256; flags += 1)
            {
                const uint32_t size = flags >> 4;
                const bool admitted = kind == 0 ? flags <= 3 : kind == 1 ? flags == 0 :
                                      kind == 2 ? (size == 2 || size == 3) && (flags & 15) < bytes >> size : (bytes == 32 && flags < 4) || bytes == 64;
                admitted_count += admitted;
                // Every admitted flags byte, and a sample of the refused ones.
                if (!admitted && (flags % 17) != 0)
                {
                    continue;
                }
                IrFunction function = {0};
                const IrSizes sizes = { .instruction_count = 4, .block_count = 1 };
                if (ir_create(output, &sizes, &function) != IR_OK)
                {
                    ir_test_expect(test, false, "permute verifier fixture allocation");
                    continue;
                }
                const IrType type = (IrType)(IR_V4I32 + width);
                function.result_type = IR_VOID;
                function.block_offsets[1] = 4;
                ir_test_instruction(&function, 0, IR_UNDEF, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 1, IR_UNDEF, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 2, IR_INTRINSIC, type, 0, 1, BUSTER_IR_INTRINSIC_PERMUTE + kind);
                ir_test_instruction(&function, 3, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function.flags[2] = (uint8_t)flags;
                ir_test_pair(test, &function, scratch, NULL, admitted, "a permute is admitted exactly where its kind's flags are valid at its width");
                if (admitted)
                {
                    function.type[1] = (uint8_t)(IR_V4I32 + (width + 1) % 3);
                    ir_test_pair(test, &function, scratch, NULL, false, "a permute's operands have its type");
                    function.type[1] = (uint8_t)type;
                    function.b[2] = BUSTER_IR_NONE;
                    ir_test_pair(test, &function, scratch, NULL, false, "a permute has two operands");
                    function.b[2] = 1;
                    IrSimdValue values[2];
                    for (uint32_t w = 0; w < 8; w += 1)
                    {
                        state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                        values[0].words[w] = w < bytes / 8 ? state : 0;
                        state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                        values[1].words[w] = w < bytes / 8 ? state : 0;
                    }
                    const IrSimdValue* operands[3] = { &values[0], &values[1], 0 };
                    const IrSimdAction action = {
                        .operation = (IrSimdOperation)(IR_SIMD_PERMUTE + kind),
                        .result = ir_simd_shape(type),
                        .operands = { ir_simd_shape(type), ir_simd_shape(type), IR_SIMD_NONE },
                        .index = (uint8_t)flags,
                    };
                    IrSimdValue value;
                    memset(&value, 0xa5, sizeof(value));
                    bool exact = ir_simd_evaluate(&action, operands, 0, 0, 0, &value);
                    for (uint32_t byte = 0; exact && byte < 64; byte += 1)
                    {
                        const uint8_t expected = byte < bytes ? ir_test_permute_byte(kind, flags, bytes, (const uint8_t*)values[0].words, (const uint8_t*)values[1].words, byte) : 0;
                        exact = ((const uint8_t*)value.words)[byte] == expected;
                    }
                    ir_test_expect(test, exact, "the model's permutes move each byte as written out, and zero the sparse tail");
                }
                arena_pop_to(output, mark);
            }
        }
    }
    // Permute 4 sizes at 3 widths, shuffle 3, align (4 + 2) + (8 + 4) + (16 + 8), shuffle_lanes 4 + 256.
    ir_test_expect(test, admitted_count == 12 + 3 + 42 + 260, "every permute's valid flags are admitted, and no other");
}

// The conversions (#1840): extend and truncate between every pair of vector widths, and an extension of an SSE carrier
// (an F32 or F64 of 4 or 8 bytes) into each width, at every flags byte below 64, admitted by both verifiers exactly where
// the lanes' sizes, mode and widths agree as written out here, and the model's lanes against C's conversions of each lane
// on random vectors.
static void ir_test_conversions(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    uint32_t admitted_count = 0;
    uint64_t state = UINT64_C(0x2545f4914f6cdd1d);
    for (uint32_t kind = BUSTER_IR_INTRINSIC_EXTEND; kind <= BUSTER_IR_INTRINSIC_TRUNCATE; kind += 1)
    {
        const bool extend = kind == BUSTER_IR_INTRINSIC_EXTEND;
        // The source: a vector of 16, 32 or 64 bytes, or a carrier (from 3, an F32; 4, an F64), which only an extension takes.
        for (uint32_t from = 0; from < 5; from += 1)
        {
            for (uint32_t to = 0; to < 3; to += 1)
            {
                for (uint32_t flags = 0; flags < 64; flags += 1)
                {
                    const uint32_t source = 1u << (flags & 3);
                    const uint32_t result = 1u << ((flags >> 2) & 3);
                    const uint32_t mode = flags >> 4;
                    const bool carrier = from >= 3;
                    const uint32_t from_bytes = carrier ? 4u << (from - 3) : 16u << from;
                    const uint32_t to_bytes = 16u << to;
                    const uint32_t lanes = extend ? to_bytes / result : from_bytes / source;
                    const uint32_t used = lanes * result;
                    const bool admitted = extend ? result > source && mode <= 1 && lanes * source <= from_bytes :
                                                   !carrier && result < source && mode <= 2 && to_bytes == (used > 16 ? used : 16);
                    admitted_count += admitted;
                    IrFunction function = {0};
                    const IrSizes sizes = { .instruction_count = 3, .block_count = 1 };
                    if (ir_create(output, &sizes, &function) != IR_OK)
                    {
                        ir_test_expect(test, false, "conversion verifier fixture allocation");
                        continue;
                    }
                    const IrType a = carrier ? (from == 3 ? IR_F32 : IR_F64) : (IrType)(IR_V4I32 + from);
                    const IrType type = (IrType)(IR_V4I32 + to);
                    function.result_type = IR_VOID;
                    function.block_offsets[1] = 3;
                    ir_test_instruction(&function, 0, IR_UNDEF, a, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    ir_test_instruction(&function, 1, IR_INTRINSIC, type, 0, BUSTER_IR_NONE, kind);
                    ir_test_instruction(&function, 2, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    function.flags[1] = (uint8_t)flags;
                    ir_test_pair(test, &function, scratch, NULL, admitted, "a conversion is admitted exactly where its lanes, mode and widths agree");
                    if (admitted)
                    {
                        IrSimdValue value_a;
                        for (uint32_t w = 0; w < 8; w += 1)
                        {
                            state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                            // Now and then a word of extreme lanes, at the saturations' edges.
                            value_a.words[w] = 8 * w >= from_bytes ? 0 : (state & 3) == 0 ? UINT64_C(0x807f80017fff8000) : (state & 3) == 1 ? UINT64_MAX : state;
                        }
                        // A carrier's value is its type's bits alone, as the interpreter holds an F32's.
                        value_a.words[0] = from_bytes == 4 ? value_a.words[0] & UINT32_MAX : value_a.words[0];
                        const IrSimdValue* operands[3] = { &value_a, 0, 0 };
                        const IrSimdAction action = {
                            .operation = extend ? IR_SIMD_EXTEND : IR_SIMD_TRUNCATE,
                            .result = ir_simd_shape(type),
                            .operands = { ir_simd_shape(a), IR_SIMD_NONE, IR_SIMD_NONE },
                            .index = (uint8_t)flags,
                        };
                        IrSimdValue value;
                        memset(&value, 0xa5, sizeof(value));
                        bool exact = ir_simd_evaluate(&action, operands, 0, 0, 0, &value);
                        const uint8_t* in = (const uint8_t*)value_a.words;
                        const uint8_t* out = (const uint8_t*)value.words;
                        for (uint32_t lane = 0; exact && lane < 64 / result; lane += 1)
                        {
                            uint64_t raw = 0;
                            if (lane < lanes)
                            {
                                memcpy(&raw, in + lane * source, source);
                            }
                            // C's conversions: the source lane as a signed or unsigned integer of its size.
                            const int64_t signed_value = source == 1 ? (int8_t)raw : source == 2 ? (int16_t)raw : source == 4 ? (int32_t)raw : (int64_t)raw;
                            const int64_t high = result == 1 ? INT8_MAX : result == 2 ? INT16_MAX : INT32_MAX;
                            const uint64_t unsigned_high = result == 1 ? UINT8_MAX : result == 2 ? UINT16_MAX : UINT32_MAX;
                            uint64_t expected = 0;
                            if (lane < lanes)
                            {
                                expected = extend && mode == 1                         ? (uint64_t)signed_value :
                                           extend                                      ? raw :
                                           mode == 1 && signed_value > high            ? (uint64_t)high :
                                           mode == 1 && signed_value < -high - 1       ? (uint64_t)(-high - 1) :
                                           mode == 2 && raw > unsigned_high            ? unsigned_high :
                                                                                         raw;
                            }
                            uint64_t actual = 0;
                            memcpy(&actual, out + lane * result, result);
                            exact = actual == (result == 8 ? expected : expected & ((UINT64_C(1) << (8 * result)) - 1));
                        }
                        ir_test_expect(test, exact, "the model converts each lane as C does, its tail zero");
                    }
                    arena_pop_to(output, mark);
                }
            }
        }
    }
    // Extends of each of the 6 pairs, zero and signed, to each width from each width wide enough, and truncates of each
    // pair and mode from each width into the one its lanes fill (counted apart, by the conditions above: 156); and the
    // carriers' extends, zero and signed, into each width whose lanes they hold: an F64's 10 pairs and widths, an F32's 4
    // (2 * (10 + 4)).
    ir_test_expect(test, admitted_count == 156 + 28, "every conversion of the IR's vector widths and carriers is admitted, and no other");
}

// The shifts by a scalar count (#1840): shift at every vector width, every flags byte below 32 and a count of each of
// I32, I64, I16 and the vector's own type, admitted by both verifiers exactly where the flags name lanes of 2, 4 or 8
// bytes and one of the three shifts and the count is an I32; and the model's lanes against C's shifts of each lane, at
// counts around every lane's bits and past 16 and 32 bits, read unsigned.
static void ir_test_scalar_shifts(IrTestTally* test, Arena* output, Arena* scratch)
{
    static const IrType counts[4] = { IR_I32, IR_I64, IR_I16, IR_V4I32 };
    static const uint32_t amounts[] = { 0, 1, 7, 15, 16, 17, 31, 32, 33, 63, 64, 65, 255, 256, 65535, 65536, 65537, 0x7fffffffu, 0x80000000u, UINT32_MAX };
    const uint64_t mark = output->length;
    uint32_t admitted_count = 0;
    uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
    for (uint32_t width = 0; width < 3; width += 1)
    {
        for (uint32_t count = 0; count < 4; count += 1)
        {
            for (uint32_t flags = 0; flags < 32; flags += 1)
            {
                const IrType type = (IrType)(IR_V4I32 + width);
                const IrType count_type = count == 3 ? type : counts[count];
                const bool admitted = (flags & 3) != 0 && ((flags >> 2) & 3) <= 2 && flags < 16 && count == 0;
                admitted_count += admitted;
                IrFunction function = {0};
                const IrSizes sizes = { .instruction_count = 4, .block_count = 1 };
                if (ir_create(output, &sizes, &function) != IR_OK)
                {
                    ir_test_expect(test, false, "scalar shift verifier fixture allocation");
                    continue;
                }
                function.result_type = IR_VOID;
                function.block_offsets[1] = 4;
                ir_test_instruction(&function, 0, IR_UNDEF, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 1, IR_UNDEF, count_type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 2, IR_INTRINSIC, type, 0, 1, BUSTER_IR_INTRINSIC_SHIFT);
                ir_test_instruction(&function, 3, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function.flags[2] = (uint8_t)flags;
                ir_test_pair(test, &function, scratch, NULL, admitted, "a scalar shift is admitted exactly where its lanes, shift and count's type agree");
                arena_pop_to(output, mark);
                for (uint32_t amount = 0; admitted && amount < BUSTER_ARRAY_LENGTH(amounts); amount += 1)
                {
                    const uint32_t bytes = 16u << width;
                    const uint32_t size = 1u << (flags & 3);
                    const uint32_t kind = (flags >> 2) & 3;
                    IrSimdValue value_a = { 0 };
                    for (uint32_t w = 0; w < bytes / 8; w += 1)
                    {
                        state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                        value_a.words[w] = (state & 3) == 0 ? UINT64_C(0x8000800080008000) : state;
                    }
                    // A count's upper 32 bits, which the shift doesn't read.
                    const IrSimdValue value_b = { { (uint64_t)amounts[amount] | UINT64_C(0xdead000000000000) } };
                    const IrSimdValue* operands[3] = { &value_a, &value_b, 0 };
                    const IrSimdAction action = {
                        .operation = IR_SIMD_SHIFT,
                        .result = ir_simd_shape(type),
                        .operands = { ir_simd_shape(type), IR_SIMD_NONE, IR_SIMD_NONE },
                        .index = (uint8_t)flags,
                    };
                    IrSimdValue value;
                    memset(&value, 0xa5, sizeof(value));
                    bool exact = ir_simd_evaluate(&action, operands, 0, 0, 0, &value);
                    const uint8_t* in = (const uint8_t*)value_a.words;
                    const uint8_t* out = (const uint8_t*)value.words;
                    const uint32_t c = amounts[amount];
                    for (uint32_t lane = 0; exact && lane < bytes / size; lane += 1)
                    {
                        uint64_t raw = 0;
                        memcpy(&raw, in + lane * size, size);
                        const uint32_t bits = 8 * size;
                        // C's shifts of the lane as an unsigned or signed integer of its size, past its bits written out.
                        const int64_t signed_value = size == 2 ? (int16_t)raw : size == 4 ? (int32_t)raw : (int64_t)raw;
                        const uint64_t expected = kind == 0 ? (c >= bits ? 0 : raw << c) :
                                                  kind == 1 ? (c >= bits ? 0 : raw >> c) :
                                                              (uint64_t)(c >= bits ? (signed_value < 0 ? -1 : 0) : signed_value >> c);
                        uint64_t actual = 0;
                        memcpy(&actual, out + lane * size, size);
                        exact = actual == (size == 8 ? expected : expected & ((UINT64_C(1) << bits) - 1));
                    }
                    for (uint32_t byte = bytes; exact && byte < 64; byte += 1)
                    {
                        exact = out[byte] == 0;
                    }
                    ir_test_expect(test, exact, "the model shifts each lane as C does, by the count read unsigned, its tail zero");
                }
            }
        }
    }
    // Three lanes' sizes and three shifts at each width.
    ir_test_expect(test, admitted_count == 3 * 3 * 3, "every scalar shift of the IR's vector widths is admitted, and no other");
}

// The permutes of two tables (#1840), whose third operand the extra array holds: both verifiers admit permute2 exactly
// where its lanes (flags 0 to 3) and its three operands' types agree, its third operand in a slot of the extra array,
// and refuse a missing slot, a slot naming no value and a binary kind whose `c` has a slot; the model is checked
// against each lane picked from the two tables by its index's low bits, on random vectors.
static void ir_test_permute2s(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    uint32_t admitted_count = 0;
    uint64_t state = UINT64_C(0xbb67ae8584caa73b);
    for (uint32_t width = 0; width < 3; width += 1)
    {
        for (uint32_t variant = 0; variant < 5; variant += 1)
        {
            for (uint32_t flags = 0; flags < 8; flags += 1)
            {
                const IrType type = (IrType)(IR_V4I32 + width);
                const IrType third_type = variant == 1 ? (IrType)(IR_V4I32 + (width + 1) % 3) : type;
                const bool admitted = (variant == 0 || variant == 4) && flags <= 3;
                admitted_count += admitted;
                IrFunction function = {0};
                const IrSizes sizes = { .instruction_count = 5, .block_count = 1, .extra_count = variant == 2 ? 0 : 1 };
                if (ir_create(output, &sizes, &function) != IR_OK)
                {
                    ir_test_expect(test, false, "permute2 verifier fixture allocation");
                    continue;
                }
                function.result_type = IR_VOID;
                function.block_offsets[1] = 5;
                ir_test_instruction(&function, 0, IR_UNDEF, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 1, IR_UNDEF, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 2, IR_UNDEF, third_type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                // 0: admitted; 1: a third operand of another width; 2: no slot; 3: a slot naming no value; 4: as 0, whose
                // `c` the checks below then change to a binary kind's with a slot and to past the slots' limit.
                const uint32_t c = ir_intrinsic_ternary_c(BUSTER_IR_INTRINSIC_PERMUTE2, 0);
                ir_test_instruction(&function, 3, IR_INTRINSIC, type, 0, 1, c);
                ir_test_instruction(&function, 4, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                if (variant != 2)
                {
                    function.extra[0] = variant == 3 ? 7 : 2;
                }
                function.flags[3] = (uint8_t)flags;
                ir_test_pair(test, &function, scratch, NULL, admitted, "a permute2 is admitted exactly where its lanes, its operands and its slot agree");
                if (variant == 4 && flags == 0)
                {
                    // A binary kind whose `c` names a slot is no kind.
                    function.c[3] = BUSTER_IR_INTRINSIC_LANE_OP | 1u << BUSTER_IR_INTRINSIC_EXTRA_SHIFT;
                    ir_test_pair(test, &function, scratch, NULL, false, "a binary intrinsic's c names no slot");
                    function.c[3] = UINT32_MAX;
                    ir_test_pair(test, &function, scratch, NULL, false, "a ternary intrinsic past the slots' limit is refused");
                }
                arena_pop_to(output, mark);
                for (uint32_t round = 0; admitted && variant == 0 && round < 4; round += 1)
                {
                    const uint32_t bytes = 16u << width;
                    const uint32_t size = 1u << flags;
                    const uint32_t lanes = bytes / size;
                    IrSimdValue values[3] = { 0 };
                    for (uint32_t v = 0; v < 3; v += 1)
                    {
                        for (uint32_t w = 0; w < bytes / 8; w += 1)
                        {
                            state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                            values[v].words[w] = state;
                        }
                    }
                    const IrSimdValue* operands[3] = { &values[0], &values[1], &values[2] };
                    const IrSimdAction action = {
                        .operation = IR_SIMD_PERMUTE2,
                        .result = ir_simd_shape(type),
                        .operands = { ir_simd_shape(type), ir_simd_shape(type), ir_simd_shape(type) },
                        .index = (uint8_t)flags,
                    };
                    IrSimdValue value;
                    memset(&value, 0xa5, sizeof(value));
                    bool exact = ir_simd_evaluate(&action, operands, 0, 0, 0, &value);
                    const uint8_t* first = (const uint8_t*)values[0].words;
                    const uint8_t* indices = (const uint8_t*)values[1].words;
                    const uint8_t* table = (const uint8_t*)values[2].words;
                    for (uint32_t lane = 0; exact && lane < lanes; lane += 1)
                    {
                        uint64_t index = 0;
                        memcpy(&index, indices + lane * size, size);
                        const uint32_t selected = (uint32_t)(index % (2 * lanes));
                        exact = memcmp((const uint8_t*)value.words + lane * size, (selected < lanes ? first + selected * size : table + (selected - lanes) * size), size) == 0;
                    }
                    ir_test_expect(test, exact, "the model's permute2 picks each lane from the two tables by its index");
                }
            }
        }
    }
    ir_test_expect(test, admitted_count == 3 * 2 * 4, "permute2 is admitted at every width and lanes, its slot first or not");
}

// The bitwise functions of three vectors (#1840): both verifiers admit ternlog of three vectors of the result's type with
// any truth table, and refuse a third operand of another width; the model is checked against each bit's table entry.
static void ir_test_ternlogs(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    uint64_t state = UINT64_C(0x3c6ef372fe94f82b);
    for (uint32_t width = 0; width < 3; width += 1)
    {
        for (uint32_t table = 0; table < 256; table += 15)
        {
            for (uint32_t other = 0; other < 2; other += 1)
            {
                const IrType type = (IrType)(IR_V4I32 + width);
                IrFunction function = {0};
                const IrSizes sizes = { .instruction_count = 5, .block_count = 1, .extra_count = 1 };
                if (ir_create(output, &sizes, &function) != IR_OK)
                {
                    ir_test_expect(test, false, "ternlog verifier fixture allocation");
                    continue;
                }
                function.result_type = IR_VOID;
                function.block_offsets[1] = 5;
                ir_test_instruction(&function, 0, IR_UNDEF, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 1, IR_UNDEF, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 2, IR_UNDEF, other == 0 ? type : (IrType)(IR_V4I32 + (width + 1) % 3), BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 3, IR_INTRINSIC, type, 0, 1, ir_intrinsic_ternary_c(BUSTER_IR_INTRINSIC_TERNLOG, 0));
                ir_test_instruction(&function, 4, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function.extra[0] = 2;
                function.flags[3] = (uint8_t)table;
                ir_test_pair(test, &function, scratch, NULL, other == 0, "a ternlog is admitted with any table, its three operands of its type");
                arena_pop_to(output, mark);
            }
            IrSimdValue values[3] = { 0 };
            for (uint32_t v = 0; v < 3; v += 1)
            {
                for (uint32_t w = 0; w < 2u << width; w += 1)
                {
                    state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                    values[v].words[w] = state;
                }
            }
            const IrSimdValue* operands[3] = { &values[0], &values[1], &values[2] };
            const IrSimdShape shape = ir_simd_shape((IrType)(IR_V4I32 + width));
            const IrSimdAction action = { .operation = IR_SIMD_TERNLOG, .result = shape, .operands = { shape, shape, shape }, .index = (uint8_t)table };
            IrSimdValue value;
            memset(&value, 0xa5, sizeof(value));
            bool exact = ir_simd_evaluate(&action, operands, 0, 0, 0, &value);
            for (uint32_t bit = 0; exact && bit < (128u << width); bit += 1)
            {
                const uint32_t w = bit / 64;
                const uint32_t index = (uint32_t)((values[0].words[w] >> (bit % 64) & 1) << 2 | (values[1].words[w] >> (bit % 64) & 1) << 1 | (values[2].words[w] >> (bit % 64) & 1));
                exact = (value.words[w] >> (bit % 64) & 1) == (table >> index & 1);
            }
            ir_test_expect(test, exact, "the model's ternlog gives each bit its table's entry");
        }
    }
}

// One lane's moves and the lanes' sign bits (#1840): both verifiers admit lane_extract, lane_insert and movemask exactly
// where the flags' lane is below the vector's lanes, the scalar an I32 for a lane of 1, 2 or 4 bytes and an I64 for one of
// 8, or an extraction's lane 0 of doublewords or quadwords into an SSE carrier, an F32 or an F64 (movemask an I32 of a
// vector of 16 or 32 bytes, of bytes, doublewords or quadwords); and the model gives C's lane, C's replaced lane and C's
// sign bits on random vectors.
static void ir_test_lane_moves(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    uint64_t state = UINT64_C(0x3c6ef372fe94f82b);
    uint32_t admitted_count = 0;
    uint32_t expected_count = 0;
    for (uint32_t kind = 0; kind < 3; kind += 1)
    {
        for (uint32_t width = 0; width < 3; width += 1)
        {
            for (uint32_t flags = 0; flags < 256; flags += 1)
            {
                // The scalar: the lane's integer, the other integer, or an SSE carrier (#1840: an F32 for lanes of 4 bytes or
                // less, an F64 for 8), which lane 0 of doublewords or quadwords alone extracts into.
                for (uint32_t scalar = 0; scalar < 3; scalar += 1)
                {
                    const IrType vector = (IrType)(IR_V4I32 + width);
                    const uint32_t size = flags & 3;
                    const uint32_t lanes = (16u << width) >> size;
                    const IrType right = size == 3 ? IR_I64 : IR_I32;
                    const IrType given = scalar == 0 ? right : scalar == 1 ? (right == IR_I64 ? IR_I32 : IR_I64) : size == 3 ? IR_F64 : IR_F32;
                    const bool admitted = kind == 2 ? scalar == 0 && flags <= 3 && flags != 1 && width < 2 :
                                          kind == 0 ? (scalar == 0 && (flags >> 2) < lanes) || (scalar == 2 && (flags == 2 || flags == 3)) :
                                                      scalar == 0 && (flags >> 2) < lanes;
                    expected_count += admitted;
                    IrFunction function = {0};
                    const IrSizes sizes = { .instruction_count = 4, .block_count = 1 };
                    if (ir_create(output, &sizes, &function) != IR_OK)
                    {
                        ir_test_expect(test, false, "lane move verifier fixture allocation");
                        continue;
                    }
                    function.result_type = IR_VOID;
                    function.block_offsets[1] = 4;
                    ir_test_instruction(&function, 0, IR_UNDEF, vector, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    ir_test_instruction(&function, 1, IR_UNDEF, kind == 1 ? given : IR_I32, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    if (kind == 1)
                    {
                        ir_test_instruction(&function, 2, IR_INTRINSIC, vector, 0, 1, BUSTER_IR_INTRINSIC_LANE_INSERT);
                    }
                    else
                    {
                        const IrType result = kind == 2 ? (scalar == 0 ? IR_I32 : scalar == 1 ? IR_I64 : IR_F32) : given;
                        ir_test_instruction(&function, 2, IR_INTRINSIC, result, 0, BUSTER_IR_NONE, kind == 0 ? BUSTER_IR_INTRINSIC_LANE_EXTRACT : BUSTER_IR_INTRINSIC_MOVEMASK);
                    }
                    ir_test_instruction(&function, 3, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    function.flags[2] = (uint8_t)flags;
                    ir_test_pair(test, &function, scratch, NULL, admitted, "a lane move or movemask is admitted exactly where its lane and scalar are its vector's");
                    admitted_count += admitted;
                    arena_pop_to(output, mark);
                }
            }
        }
    }
    ir_test_expect(test, admitted_count == expected_count && expected_count != 0, "lane moves and movemasks are admitted at every width and lane");
    // The model against C's lanes.
    bool exact = true;
    for (uint32_t trial = 0; trial < 64; trial += 1)
    {
        const uint32_t width = trial % 3;
        const uint32_t size = (trial / 3) % 4;
        const IrSimdShape shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        const uint32_t bytes = 16u << width;
        const uint32_t lanes = bytes >> size;
        IrSimdValue a = {0};
        for (uint32_t w = 0; w < bytes / 8; w += 1)
        {
            a.words[w] = ir_test_random(&state);
        }
        const uint64_t scalar = ir_test_random(&state);
        const uint32_t lane = (uint32_t)(ir_test_random(&state) % lanes);
        const uint32_t element = 1u << size;
        const uint8_t* source = (const uint8_t*)a.words;
        uint64_t wanted_lane = 0;
        memcpy(&wanted_lane, source + lane * element, element);
        IrSimdValue wanted_insert = a;
        memcpy((uint8_t*)wanted_insert.words + lane * element, &scalar, element);
        uint64_t wanted_mask = 0;
        for (uint32_t l = 0; l < lanes; l += 1)
        {
            wanted_mask |= (uint64_t)(source[l * element + element - 1] >> 7) << l;
        }
        const IrSimdValue scalar_value = { .words = { scalar } };
        const IrSimdValue* operands[3] = { &a, &scalar_value, 0 };
        const uint8_t index = (uint8_t)(size | (lane << 2));
        const IrSimdAction extract = { .operation = IR_SIMD_LANE_EXTRACT, .result = IR_SIMD_NONE, .operands = { shape, IR_SIMD_NONE, IR_SIMD_NONE }, .index = index };
        const IrSimdAction insert = { .operation = IR_SIMD_LANE_INSERT, .result = shape, .operands = { shape, IR_SIMD_NONE, IR_SIMD_NONE }, .index = index };
        const IrSimdAction movemask = { .operation = IR_SIMD_MOVEMASK, .result = IR_SIMD_NONE, .operands = { shape, IR_SIMD_NONE, IR_SIMD_NONE }, .index = (uint8_t)size };
        IrSimdValue got = {0};
        exact = exact && ir_simd_evaluate(&extract, operands, 0, 0, 0, &got) && got.words[0] == wanted_lane;
        exact = exact && ir_simd_evaluate(&insert, operands, 0, 0, 0, &got) && memcmp(&got, &wanted_insert, sizeof(got)) == 0;
        const bool masked = width < 2 && size != 1;
        const bool evaluated = ir_simd_evaluate(&movemask, operands, 0, 0, 0, &got);
        exact = exact && (masked ? evaluated && got.words[0] == wanted_mask : !evaluated);
    }
    ir_test_expect(test, exact, "the model's lane moves and movemasks are C's");
}

// The reductions (#1840): both verifiers admit reduce exactly where its result is a V4I32, its operand a vector of any
// width, its lanes doublewords or quadwords and its operation one of the nine, for every flags byte; and the model gives
// C's minimum, maximum, sum, product, and, or and xor of all of the lanes in each of the result's lanes, on random
// vectors with lanes at the signed and unsigned edges.
static void ir_test_reductions(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    uint64_t state = UINT64_C(0x9b05688c2b3e6c1f);
    uint32_t admitted_count = 0;
    uint32_t expected_count = 0;
    for (uint32_t width = 0; width < 3; width += 1)
    {
        for (uint32_t flags = 0; flags < 256; flags += 1)
        {
            for (uint32_t result = 0; result < 3; result += 1)
            {
                const IrType vector = (IrType)(IR_V4I32 + width);
                const IrType type = result == 0 ? IR_V4I32 : result == 1 ? IR_V8I32 : IR_I32;
                const bool admitted = result == 0 && (flags & 3) >= 2 && (flags >> 2) < BUSTER_IR_REDUCE_OPERATIONS;
                expected_count += admitted;
                IrFunction function = {0};
                const IrSizes sizes = { .instruction_count = 3, .block_count = 1 };
                if (ir_create(output, &sizes, &function) != IR_OK)
                {
                    ir_test_expect(test, false, "reduction verifier fixture allocation");
                    continue;
                }
                function.result_type = IR_VOID;
                function.block_offsets[1] = 3;
                ir_test_instruction(&function, 0, IR_UNDEF, vector, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 1, IR_INTRINSIC, type, 0, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_REDUCE);
                ir_test_instruction(&function, 2, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function.flags[1] = (uint8_t)flags;
                ir_test_pair(test, &function, scratch, NULL, admitted, "a reduction is admitted exactly at its lanes, operation and V4I32 result");
                admitted_count += admitted;
                arena_pop_to(output, mark);
            }
        }
    }
    ir_test_expect(test, admitted_count == expected_count && expected_count == 3 * 2 * BUSTER_IR_REDUCE_OPERATIONS, "reductions are admitted at every width, lanes and operation");
    // The model against C's operations, lane by lane.
    static const uint64_t edges[6] = { 0, 1, UINT64_MAX, UINT64_C(0x7fffffffffffffff), UINT64_C(0x8000000000000000), UINT64_C(0x80000000) };
    bool exact = true;
    for (uint32_t trial = 0; trial < 3 * 2 * BUSTER_IR_REDUCE_OPERATIONS * 4; trial += 1)
    {
        const uint32_t width = trial % 3;
        const uint32_t size = 2 + (trial / 3) % 2;
        const uint32_t operation = (trial / 6) % BUSTER_IR_REDUCE_OPERATIONS;
        const uint32_t bytes = 16u << width;
        const uint32_t element = 1u << size;
        const uint32_t lanes = bytes / element;
        IrSimdValue a = {0};
        for (uint32_t w = 0; w < bytes / 8; w += 1)
        {
            const uint64_t random = ir_test_random(&state);
            a.words[w] = (random & 3) == 0 ? edges[(random >> 2) % 6] : random;
        }
        uint64_t wanted = 0;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            uint64_t x = 0;
            memcpy(&x, (const uint8_t*)a.words + lane * element, element);
            const int64_t sx = size == 2 ? (int64_t)(int32_t)(uint32_t)x : (int64_t)x;
            const int64_t sw = size == 2 ? (int64_t)(int32_t)(uint32_t)wanted : (int64_t)wanted;
            const uint64_t combined[9] = { x < wanted ? x : wanted, sx < sw ? x : wanted, x > wanted ? x : wanted, sx > sw ? x : wanted, wanted + x, wanted * x, wanted & x, wanted | x, wanted ^ x };
            wanted = lane == 0 ? x : combined[operation];
            wanted = size == 2 ? wanted & UINT32_MAX : wanted;
        }
        IrSimdValue expected = {0};
        for (uint32_t lane = 0; lane < 16 / element; lane += 1)
        {
            memcpy((uint8_t*)expected.words + lane * element, &wanted, element);
        }
        const IrSimdValue* operands[3] = { &a, 0, 0 };
        const IrSimdAction action = {
            .operation = IR_SIMD_REDUCE,
            .result = IR_SIMD_V4I32,
            .operands = { (IrSimdShape)(IR_SIMD_V4I32 + width), IR_SIMD_NONE, IR_SIMD_NONE },
            .index = (uint8_t)(size | operation << 2),
        };
        IrSimdValue got = {0};
        exact = exact && ir_simd_evaluate(&action, operands, 0, 0, 0, &got) && memcmp(&got, &expected, sizeof(got)) == 0;
    }
    ir_test_expect(test, exact, "the model's reductions are C's");
}

// The lanes' unary operations (#1840): both verifiers admit lane_unary exactly where its operand is of its type and its
// flags a population count of any lanes, or a leading zero count or conflicts of doublewords or quadwords, for every
// flags byte at each width; and the model gives C's counts and conflicts of each lane on random vectors whose lanes
// repeat.
static void ir_test_lane_unaries(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    uint64_t state = UINT64_C(0x6a09e667bb67ae85);
    uint32_t admitted_count = 0;
    for (uint32_t width = 0; width < 3; width += 1)
    {
        for (uint32_t flags = 0; flags < 256; flags += 1)
        {
            for (uint32_t other = 0; other < 2; other += 1)
            {
                const IrType vector = (IrType)(IR_V4I32 + width);
                const IrType source = other == 0 ? vector : (IrType)(IR_V4I32 + (width + 1) % 3);
                const bool admitted = other == 0 && (flags >> 2) <= 2 && ((flags >> 2) == 0 || (flags & 3) >= 2);
                IrFunction function = {0};
                const IrSizes sizes = { .instruction_count = 3, .block_count = 1 };
                if (ir_create(output, &sizes, &function) != IR_OK)
                {
                    ir_test_expect(test, false, "lane unary verifier fixture allocation");
                    continue;
                }
                function.result_type = IR_VOID;
                function.block_offsets[1] = 3;
                ir_test_instruction(&function, 0, IR_UNDEF, source, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 1, IR_INTRINSIC, vector, 0, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_LANE_UNARY);
                ir_test_instruction(&function, 2, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function.flags[1] = (uint8_t)flags;
                ir_test_pair(test, &function, scratch, NULL, admitted, "a lane's unary operation is admitted exactly at its lanes and operation, of its type");
                admitted_count += admitted;
                arena_pop_to(output, mark);
            }
        }
    }
    ir_test_expect(test, admitted_count == 3 * (4 + 2 + 2), "lane unary operations are admitted at every width, lanes and operation");
    // The model against C's counts and conflicts, lane by lane.
    bool exact = true;
    for (uint32_t trial = 0; trial < 3 * 8 * 4; trial += 1)
    {
        const uint32_t width = trial % 3;
        const uint32_t pick = (trial / 3) % 8;
        const uint32_t operation = pick < 4 ? 0 : pick < 6 ? 1 : 2;
        const uint32_t size = pick < 4 ? pick : 2 + pick % 2;
        const uint32_t bytes = 16u << width;
        const uint32_t element = 1u << size;
        const uint32_t lanes = bytes / element;
        IrSimdValue a = {0};
        for (uint32_t w = 0; w < bytes / 8; w += 1)
        {
            a.words[w] = ir_test_random(&state);
        }
        // Repeated lanes for the conflicts, zeros and narrow values for the counts.
        uint8_t* raw = (uint8_t*)a.words;
        for (uint32_t lane = 1; lane < lanes; lane += 1)
        {
            const uint64_t random = ir_test_random(&state);
            if ((random & 3) == 0)
            {
                memcpy(raw + lane * element, raw + (random >> 8) % lane * element, element);
            }
            else if ((random & 3) == 1)
            {
                memset(raw + lane * element, 0, element);
                raw[lane * element] = (uint8_t)(random >> 8);
            }
        }
        IrSimdValue expected = {0};
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            uint64_t x = 0;
            memcpy(&x, raw + lane * element, element);
            uint64_t value = 0;
            for (uint32_t bit = 0; bit < element * 8; bit += 1)
            {
                value += operation == 0 ? (x >> bit) & 1 : 0;
            }
            for (uint32_t bit = element * 8; operation == 1 && bit > 0 && ((x >> (bit - 1)) & 1) == 0; bit -= 1)
            {
                value += 1;
            }
            for (uint32_t below = 0; operation == 2 && below < lane; below += 1)
            {
                uint64_t y = 0;
                memcpy(&y, raw + below * element, element);
                value |= (uint64_t)(x == y) << below;
            }
            memcpy((uint8_t*)expected.words + lane * element, &value, element);
        }
        const IrSimdShape shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        const IrSimdValue* operands[3] = { &a, 0, 0 };
        const IrSimdAction action = { .operation = IR_SIMD_LANE_UNARY, .result = shape, .operands = { shape, IR_SIMD_NONE, IR_SIMD_NONE }, .index = (uint8_t)(size | operation << 2) };
        IrSimdValue got = {0};
        exact = exact && ir_simd_evaluate(&action, operands, 0, 0, 0, &got) && memcmp(&got, &expected, sizeof(got)) == 0;
    }
    ir_test_expect(test, exact, "the model's lane counts and conflicts are C's");
}

// C's operation of two lanes in the rounding mode the flags name (#1840's fround), by host_fesetround and volatile
// operands, the oracle of the model's MXCSR rounding: the arithmetic happens between the volatile loads and the volatile
// store, which no call reorders. Where both lanes are NaNs, C leaves the result's NaN to the compiler, which may commute
// x + y or x * y (clang does in the TSan build, and the processor then returns y's), so the oracle gives x86's rule
// there: the first operand's NaN, quieted (the Intel SDM's rules for handling NaNs, vol. 1, 4.8.3.5). With one NaN,
// either order gives that NaN, quieted.
static uint64_t ir_test_fround_oracle(uint32_t flags, uint64_t a, uint64_t b)
{
    static const int modes[4] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    const int saved = host_fegetround();
    uint64_t result = 0;
    host_fesetround(modes[(flags >> 3) & 3]);
    if ((flags & 4) == 0)
    {
        volatile float x = 0;
        volatile float y = 0;
        volatile float z = 0;
        const uint32_t xa = (uint32_t)a;
        const uint32_t yb = (uint32_t)b;
        memcpy((void*)&x, &xa, sizeof(xa));
        memcpy((void*)&y, &yb, sizeof(yb));
        const uint32_t operation = flags & 3;
        z = operation == 0 ? x + y : operation == 1 ? x - y : operation == 2 ? x * y : x / y;
        uint32_t word = 0;
        memcpy(&word, (const void*)&z, sizeof(word));
        result = word;
    }
    else
    {
        volatile double x = 0;
        volatile double y = 0;
        volatile double z = 0;
        memcpy((void*)&x, &a, sizeof(a));
        memcpy((void*)&y, &b, sizeof(b));
        const uint32_t operation = flags & 3;
        z = operation == 0 ? x + y : operation == 1 ? x - y : operation == 2 ? x * y : x / y;
        memcpy(&result, (const void*)&z, sizeof(result));
    }
    host_fesetround(saved);
    const bool wide = (flags & 4) != 0;
    const uint64_t width = wide ? UINT64_MAX : UINT32_MAX;
    const uint64_t exponent = wide ? UINT64_C(0x7ff0000000000000) : UINT64_C(0x7f800000);
    const uint64_t fraction = wide ? UINT64_C(0x000fffffffffffff) : UINT64_C(0x007fffff);
    const uint64_t quiet = wide ? UINT64_C(0x0008000000000000) : UINT64_C(0x00400000);
    const uint64_t first = a & width;
    const uint64_t second = b & width;
    const bool first_nan = (first & exponent) == exponent && (first & fraction) != 0;
    const bool second_nan = (second & exponent) == exponent && (second & fraction) != 0;
    return first_nan && second_nan ? first | quiet : result;
}

// The truncating conversions of lane 0 (#1840): both verifiers admit cvtt exactly of a V4I32 into the I32 or I64 its
// flags' bit 1 names, at every flags byte; and the model gives x86's result, C's truncation where the range holds it
// and the smallest integer for NaNs, infinities and values past the range, at the range's edges both sides.
static void ir_test_cvtts(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    static const IrType sources[3] = { IR_V4I32, IR_V8I32, IR_F64 };
    static const IrType results[3] = { IR_I32, IR_I64, IR_I16 };
    uint32_t admitted_count = 0;
    for (uint32_t flags = 0; flags < 256; flags += 1)
    {
        for (uint32_t source = 0; source < 3; source += 1)
        {
            for (uint32_t result = 0; result < 3; result += 1)
            {
                const bool admitted = flags <= 3 && source == 0 && result == ((flags & 2) != 0 ? 1u : 0u);
                IrFunction function = {0};
                const IrSizes sizes = { .instruction_count = 3, .block_count = 1 };
                if (ir_create(output, &sizes, &function) != IR_OK)
                {
                    ir_test_expect(test, false, "cvtt verifier fixture allocation");
                    continue;
                }
                function.result_type = IR_VOID;
                function.block_offsets[1] = 3;
                ir_test_instruction(&function, 0, IR_UNDEF, sources[source], BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 1, IR_INTRINSIC, results[result], 0, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_CVTT);
                ir_test_instruction(&function, 2, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function.flags[1] = (uint8_t)flags;
                ir_test_pair(test, &function, scratch, NULL, admitted, "a truncating conversion is admitted exactly of a V4I32 into its flags' integer");
                admitted_count += admitted;
                arena_pop_to(output, mark);
            }
        }
    }
    ir_test_expect(test, admitted_count == 4, "cvtt is admitted at each lane and result");
    static const double values[] = { 0.0, -0.0, 0.5, -0.99, 1.75, -1.75, 2147483647.0, 2147483647.5, 2147483648.0, -2147483648.0, -2147483648.75, -2147483649.0,
                                     0x1p62, 0x1.fffffffffffffp62, 0x1p63, -0x1p63, -0x1.0000000000001p63, 1e300, -1e300, INFINITY, -INFINITY, NAN };
    bool exact = true;
    for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(values); v += 1)
    {
        for (uint32_t flags = 0; flags < 4; flags += 1)
        {
            const bool wide = (flags & 2) != 0;
            const bool is_double = (flags & 1) != 0;
            const double x = is_double ? values[v] : (double)(float)values[v];
            IrSimdValue a = {0};
            if (is_double)
            {
                memcpy(&a.words[0], &x, sizeof(x));
            }
            else
            {
                const float single = (float)x;
                uint32_t word = 0;
                memcpy(&word, &single, sizeof(word));
                a.words[0] = word;
            }
            // x86's integer indefinite past the range, written out with long double bounds.
            const long double limit = wide ? 0x1p63L : 0x1p31L;
            const bool fits = x == x && (long double)x > -limit - 1.0L && (long double)x < limit;
            const uint64_t wanted = !fits ? (wide ? UINT64_C(0x8000000000000000) : UINT64_C(0x80000000)) : wide ? (uint64_t)(int64_t)x : (uint64_t)(uint32_t)(int32_t)x;
            const IrSimdValue* operands[3] = { &a, 0, 0 };
            const IrSimdAction action = { .operation = IR_SIMD_CVTT, .result = IR_SIMD_NONE, .operands = { IR_SIMD_V4I32, IR_SIMD_NONE, IR_SIMD_NONE }, .index = (uint8_t)flags };
            IrSimdValue got = {0};
            exact = exact && ir_simd_evaluate(&action, operands, 0, 0, 0, &got) && got.words[0] == wanted;
        }
    }
    ir_test_expect(test, exact, "the model's truncating conversions are x86's");
}

// The floating operations at their own rounding (#1840): both verifiers admit fround exactly of two 64-byte vectors of its
// type at every flags byte below 32; and the model gives C's operation in each rounding mode (ir_test_fround_oracle) on
// lanes chosen so that the modes differ (random fractions, ties, overflow, underflow, zeros, infinities and NaNs), and
// leaves MXCSR as it was.
static void ir_test_frounds(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    uint64_t state = UINT64_C(0x510e527f9b05688c);
    uint32_t admitted_count = 0;
    for (uint32_t width = 0; width < 3; width += 1)
    {
        for (uint32_t flags = 0; flags < 256; flags += 1)
        {
            for (uint32_t other = 0; other < 2; other += 1)
            {
                const IrType vector = (IrType)(IR_V4I32 + width);
                const bool admitted = width == 2 && other == 0 && flags < 32;
                IrFunction function = {0};
                const IrSizes sizes = { .instruction_count = 4, .block_count = 1 };
                if (ir_create(output, &sizes, &function) != IR_OK)
                {
                    ir_test_expect(test, false, "fround verifier fixture allocation");
                    continue;
                }
                function.result_type = IR_VOID;
                function.block_offsets[1] = 4;
                ir_test_instruction(&function, 0, IR_UNDEF, vector, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 1, IR_UNDEF, other == 0 ? vector : IR_V8I32, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 2, IR_INTRINSIC, vector, 0, 1, BUSTER_IR_INTRINSIC_FROUND);
                ir_test_instruction(&function, 3, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function.flags[2] = (uint8_t)flags;
                ir_test_pair(test, &function, scratch, NULL, admitted, "a floating operation at its own rounding is admitted exactly of two 64-byte vectors");
                admitted_count += admitted;
                arena_pop_to(output, mark);
            }
        }
    }
    ir_test_expect(test, admitted_count == 32, "fround is admitted at every operation, lanes and rounding");
    static const uint64_t specials[2][8] = {
        { 0, UINT64_C(0x80000000), UINT64_C(0x7f800000), UINT64_C(0xff800000), UINT64_C(0x7fc00001), UINT64_C(0x7f7fffff), UINT64_C(0x00000001), UINT64_C(0x3f800000) },
        { 0, UINT64_C(0x8000000000000000), UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8000000000001), UINT64_C(0x7fefffffffffffff),
          UINT64_C(0x0000000000000001), UINT64_C(0x3ff0000000000000) },
    };
    bool exact = true;
    bool kept = true;
    for (uint32_t trial = 0; trial < 32 * 8; trial += 1)
    {
        const uint32_t flags = trial % 32;
        const uint32_t lane_bits = (flags & 4) != 0 ? 64 : 32;
        IrSimdValue a = {0};
        IrSimdValue b = {0};
        for (uint32_t lane = 0; lane < 512 / lane_bits; lane += 1)
        {
            const uint64_t random = ir_test_random(&state);
            const uint64_t x = (random & 7) == 0 ? specials[lane_bits == 64][(random >> 3) & 7] : ir_test_random(&state);
            const uint64_t y = (random & 0x38) == 0 ? specials[lane_bits == 64][(random >> 6) & 7] : (random & 0x1c0) == 0 ? x : ir_test_random(&state);
            ir_simd_lane_write(&a, lane_bits, lane, lane_bits == 64 ? x : x & UINT32_MAX);
            ir_simd_lane_write(&b, lane_bits, lane, lane_bits == 64 ? y : y & UINT32_MAX);
        }
        const IrSimdValue* operands[3] = { &a, &b, 0 };
        const IrSimdAction action = { .operation = IR_SIMD_FROUND, .result = IR_SIMD_V16I32, .operands = { IR_SIMD_V16I32, IR_SIMD_V16I32, IR_SIMD_NONE }, .index = (uint8_t)flags };
        IrSimdValue got = {0};
        uint32_t before = 0;
        uint32_t after = 0;
        __asm__ volatile("stmxcsr %0" : "=m"(before) : : "memory");
        exact = exact && ir_simd_evaluate(&action, operands, 0, 0, 0, &got);
        __asm__ volatile("stmxcsr %0" : "=m"(after) : : "memory");
        kept = kept && before == after;
        for (uint32_t lane = 0; exact && lane < 512 / lane_bits; lane += 1)
        {
            exact = ir_simd_lane_read(&got, lane_bits, lane) == ir_test_fround_oracle(flags, ir_simd_lane_read(&a, lane_bits, lane), ir_simd_lane_read(&b, lane_bits, lane));
        }
    }
    ir_test_expect(test, exact, "the model's floating operations at their own rounding are C's in that rounding mode");
    ir_test_expect(test, kept, "the model leaves MXCSR, its flags among it, as it was");
}

// The masked loads and stores (#1840): both verifiers admit them exactly where their flags are integer lanes (0 to 3),
// whose mask is the load's b or the store's c, the address a ptr, the passthrough or the stored vector a vector (a load's
// of its type) and the alignment's log2 at most 31; the float lanes, an invalid lane, another mask, a vector address and
// an alignment of 2^32 are refused.
static void ir_test_masked_memory(IrTestTally* test, Arena* output, Arena* scratch)
{
    static const IrSimdLane lane_kinds[4] = { IR_SIMD_LANE_I8, IR_SIMD_LANE_I16, IR_SIMD_LANE_I32, IR_SIMD_LANE_I64 };
    const uint64_t mark = output->length;
    uint32_t admitted_count = 0;
    for (uint32_t store = 0; store < 2; store += 1)
    {
        for (uint32_t width = 0; width < 3; width += 1)
        {
            for (uint32_t flags = 0; flags < 7; flags += 1)
            {
                for (uint32_t variant = 0; variant < 4; variant += 1)
                {
                    const IrType type = (IrType)(IR_V4I32 + width);
                    const uint32_t right_mask = ir_lane_mask(type, lane_kinds[flags & 3]);
                    const uint32_t other_mask = ir_lane_mask(type, lane_kinds[(flags + 1) & 3]) != right_mask ? ir_lane_mask(type, lane_kinds[(flags + 1) & 3]) : (uint32_t)type;
                    const bool admitted = flags <= 3 && variant == 0;
                    admitted_count += admitted;
                    IrFunction function = {0};
                    const IrSizes sizes = { .instruction_count = 5, .block_count = 1 };
                    if (ir_create(output, &sizes, &function) != IR_OK)
                    {
                        ir_test_expect(test, false, "masked access verifier fixture allocation");
                        continue;
                    }
                    function.result_type = IR_VOID;
                    function.block_offsets[1] = 5;
                    ir_test_instruction(&function, 0, IR_UNDEF, variant == 2 ? type : IR_PTR, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    ir_test_instruction(&function, 1, IR_UNDEF, variant == 1 ? other_mask : right_mask, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    ir_test_instruction(&function, 2, IR_UNDEF, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    if (store != 0)
                    {
                        ir_test_instruction(&function, 3, IR_MASKED_STORE, IR_VOID, 0, 2, 1);
                    }
                    else
                    {
                        ir_test_instruction(&function, 3, IR_MASKED_LOAD, type, 0, 1, 2);
                    }
                    ir_test_instruction(&function, 4, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    function.flags[3] = (uint8_t)(flags <= 3 ? lane_kinds[flags] : flags);
                    function.alignment_log2[3] = (uint8_t)(variant == 3 ? 32 : width + 4);
                    ir_test_pair(test, &function, scratch, NULL, admitted, "a masked load or store is admitted exactly where its lanes name its mask");
                    arena_pop_to(output, mark);
                }
            }
        }
    }
    ir_test_expect(test, admitted_count == 2 * 3 * 4, "masked loads and stores are admitted at every width and integer lanes");
}

// The compresses and expands (#1840): both verifiers admit them exactly where their lanes (flags 0 to 3) name b's mask
// type and a and the third operand are of the result's type; the model is checked against C's packing of the selected
// lanes and spreading of the next ones, the rest the third operand's, on random vectors and masks.
static void ir_test_compresses(IrTestTally* test, Arena* output, Arena* scratch)
{
    static const IrSimdLane lane_kinds[4] = { IR_SIMD_LANE_I8, IR_SIMD_LANE_I16, IR_SIMD_LANE_I32, IR_SIMD_LANE_I64 };
    const uint64_t mark = output->length;
    uint64_t state = UINT64_C(0xa54ff53a5f1d36f1);
    uint32_t admitted_count = 0;
    for (uint32_t kind = 0; kind < 2; kind += 1)
    {
        for (uint32_t width = 0; width < 3; width += 1)
        {
            for (uint32_t flags = 0; flags < 5; flags += 1)
            {
                for (uint32_t wrong = 0; wrong < 2; wrong += 1)
                {
                    const IrType type = (IrType)(IR_V4I32 + width);
                    const uint32_t right_mask = ir_lane_mask(type, lane_kinds[flags & 3]);
                    // A mask of other lanes' width: the next lanes' mask where it differs, else another vector's.
                    const uint32_t other_mask = ir_lane_mask(type, lane_kinds[(flags + 1) & 3]) != right_mask ? ir_lane_mask(type, lane_kinds[(flags + 1) & 3]) : (uint32_t)type;
                    const bool admitted = flags <= 3 && wrong == 0;
                    admitted_count += admitted;
                    IrFunction function = {0};
                    const IrSizes sizes = { .instruction_count = 5, .block_count = 1, .extra_count = 1 };
                    if (ir_create(output, &sizes, &function) != IR_OK)
                    {
                        ir_test_expect(test, false, "compress verifier fixture allocation");
                        continue;
                    }
                    function.result_type = IR_VOID;
                    function.block_offsets[1] = 5;
                    ir_test_instruction(&function, 0, IR_UNDEF, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    ir_test_instruction(&function, 1, IR_UNDEF, wrong == 0 ? right_mask : other_mask, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    ir_test_instruction(&function, 2, IR_UNDEF, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    ir_test_instruction(&function, 3, IR_INTRINSIC, type, 0, 1, ir_intrinsic_ternary_c(BUSTER_IR_INTRINSIC_COMPRESS + kind, 0));
                    ir_test_instruction(&function, 4, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    function.extra[0] = 2;
                    function.flags[3] = (uint8_t)flags;
                    ir_test_pair(test, &function, scratch, NULL, admitted, "a compress or expand is admitted exactly where its lanes name its mask");
                    arena_pop_to(output, mark);
                }
                if (flags > 3)
                {
                    continue;
                }
                const IrType type = (IrType)(IR_V4I32 + width);
                const uint32_t size = 1u << flags;
                const uint32_t lanes = (16u << width) / size;
                IrSimdValue values[3] = { 0 };
                for (uint32_t w = 0; w < 2u << width; w += 1)
                {
                    state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                    values[0].words[w] = state;
                    state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                    values[2].words[w] = state;
                }
                state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                values[1].words[0] = lanes >= 64 ? state : state & ((UINT64_C(1) << lanes) - 1);
                const IrSimdValue* operands[3] = { &values[0], &values[1], &values[2] };
                const IrSimdAction action = {
                    .operation = kind == 0 ? IR_SIMD_COMPRESS : IR_SIMD_EXPAND,
                    .result = ir_simd_shape(type),
                    .operands = { ir_simd_shape(type), ir_simd_shape((IrType)ir_lane_mask(type, lane_kinds[flags])), ir_simd_shape(type) },
                    .index = (uint8_t)flags,
                };
                IrSimdValue value;
                memset(&value, 0xa5, sizeof(value));
                bool exact = ir_simd_evaluate(&action, operands, 0, 0, 0, &value);
                uint8_t wanted[64];
                memcpy(wanted, values[2].words, (uint64_t)lanes * size);
                uint32_t next = 0;
                for (uint32_t lane = 0; lane < lanes; lane += 1)
                {
                    if ((values[1].words[0] >> lane & 1) != 0)
                    {
                        memcpy(wanted + (kind == 0 ? next : lane) * size, (const uint8_t*)values[0].words + (kind == 0 ? lane : next) * size, size);
                        next += 1;
                    }
                }
                exact = exact && memcmp(value.words, wanted, (uint64_t)lanes * size) == 0;
                ir_test_expect(test, exact, "the model's compress and expand move the selected lanes as C does");
            }
        }
    }
    ir_test_expect(test, admitted_count == 2 * 3 * 4, "compress and expand are admitted at every width and lanes");
}

// The AES rounds (#1840): both verifiers admit aes of two vectors of the result's type with flags 0 and 1 alone; the
// model gives FIPS 197's Appendix B round 1 (vaesenc of the state after the first AddRoundKey by round key 1 is the state
// starting round 2) and the 9700X's vaesenclast of that appendix's round-10 key, in every 16-byte lane of each width.
static void ir_test_aes(IrTestTally* test, Arena* output, Arena* scratch)
{
    static const uint8_t state[16] = { 0x19, 0x3d, 0xe3, 0xbe, 0xa0, 0xf4, 0xe2, 0x2b, 0x9a, 0xc6, 0x8d, 0x2a, 0xe9, 0xf8, 0x48, 0x08 };
    static const uint8_t key[16] = { 0xa0, 0xfa, 0xfe, 0x17, 0x88, 0x54, 0x2c, 0xb1, 0x23, 0xa3, 0x39, 0x39, 0x2a, 0x6c, 0x76, 0x05 };
    static const uint8_t round[16] = { 0xa4, 0x9c, 0x7f, 0xf2, 0x68, 0x9f, 0x35, 0x2b, 0x6b, 0x5b, 0xea, 0x43, 0x02, 0x6a, 0x50, 0x49 };
    static const uint8_t last_state[16] = { 0xeb, 0x59, 0x8b, 0x1b, 0x40, 0x2e, 0xa1, 0xc3, 0xf2, 0x38, 0x13, 0x42, 0x1e, 0x84, 0xe7, 0xd2 };
    static const uint8_t last_key[16] = { 0xd0, 0x14, 0xf9, 0xa8, 0xc9, 0xee, 0x25, 0x89, 0xe1, 0x3f, 0x0c, 0xc8, 0xb6, 0x63, 0x0c, 0xa6 };
    static const uint8_t last[16] = { 0x39, 0x25, 0x84, 0x1d, 0xc0, 0xe9, 0xb1, 0x26, 0x68, 0x60, 0x31, 0xe6, 0xc4, 0xa8, 0x3e, 0x8a };
    const uint64_t mark = output->length;
    for (uint32_t width = 0; width < 3; width += 1)
    {
        const IrType type = (IrType)(IR_V4I32 + width);
        for (uint32_t flags = 0; flags < 3; flags += 1)
        {
            IrFunction function = {0};
            const IrSizes sizes = { .instruction_count = 4, .block_count = 1 };
            if (ir_create(output, &sizes, &function) != IR_OK)
            {
                ir_test_expect(test, false, "aes verifier fixture allocation");
                continue;
            }
            function.result_type = IR_VOID;
            function.block_offsets[1] = 4;
            ir_test_instruction(&function, 0, IR_UNDEF, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 1, IR_UNDEF, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 2, IR_INTRINSIC, type, 0, 1, BUSTER_IR_INTRINSIC_AES);
            ir_test_instruction(&function, 3, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function.flags[2] = (uint8_t)flags;
            ir_test_pair(test, &function, scratch, NULL, flags <= 1, "an aes round is admitted with flags 0 and 1 alone");
            arena_pop_to(output, mark);
        }
        for (uint32_t which = 0; which < 2; which += 1)
        {
            IrSimdValue values[2] = { 0 };
            for (uint32_t lane = 0; lane < (1u << width); lane += 1)
            {
                memcpy((uint8_t*)values[0].words + lane * 16, which == 0 ? state : last_state, 16);
                memcpy((uint8_t*)values[1].words + lane * 16, which == 0 ? key : last_key, 16);
            }
            const IrSimdValue* operands[3] = { &values[0], &values[1], 0 };
            const IrSimdShape shape = ir_simd_shape(type);
            const IrSimdAction action = { .operation = IR_SIMD_AES, .result = shape, .operands = { shape, shape, IR_SIMD_NONE }, .index = (uint8_t)which };
            IrSimdValue value;
            memset(&value, 0xa5, sizeof(value));
            bool exact = ir_simd_evaluate(&action, operands, 0, 0, 0, &value);
            for (uint32_t lane = 0; exact && lane < (1u << width); lane += 1)
            {
                exact = memcmp((const uint8_t*)value.words + lane * 16, which == 0 ? round : last, 16) == 0;
            }
            ir_test_expect(test, exact, "the model's aes gives FIPS 197's round and the 9700X's last round in every lane");
        }
    }
}

// The lane operations (#1840): lane_op at every vector width, for every flags byte below 64 and a second operand of the
// result's type or another width's, admitted by both verifiers exactly where the operation is a minimum or a maximum, or
// a saturating sum or difference of bytes or words, and the operands are of the result's type; and the model's lanes
// against C's minimum, maximum and clamped sum and difference of each lane, on random vectors with extreme lanes.
static void ir_test_lane_ops(IrTestTally* test, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    uint32_t admitted_count = 0;
    uint64_t state = UINT64_C(0x6a09e667f3bcc909);
    for (uint32_t width = 0; width < 3; width += 1)
    {
        for (uint32_t other = 0; other < 2; other += 1)
        {
            for (uint32_t flags = 0; flags < 64; flags += 1)
            {
                const IrType type = (IrType)(IR_V4I32 + width);
                const IrType b_type = other == 0 ? type : (IrType)(IR_V4I32 + (width + 1) % 3);
                const uint32_t operation = (flags >> 2) & 7;
                const bool admitted = other == 0 && flags < 32 && (operation < 4 || (flags & 3) <= 1);
                admitted_count += admitted;
                IrFunction function = {0};
                const IrSizes sizes = { .instruction_count = 4, .block_count = 1 };
                if (ir_create(output, &sizes, &function) != IR_OK)
                {
                    ir_test_expect(test, false, "lane operation verifier fixture allocation");
                    continue;
                }
                function.result_type = IR_VOID;
                function.block_offsets[1] = 4;
                ir_test_instruction(&function, 0, IR_UNDEF, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 1, IR_UNDEF, b_type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_test_instruction(&function, 2, IR_INTRINSIC, type, 0, 1, BUSTER_IR_INTRINSIC_LANE_OP);
                ir_test_instruction(&function, 3, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function.flags[2] = (uint8_t)flags;
                ir_test_pair(test, &function, scratch, NULL, admitted, "a lane operation is admitted exactly where its lanes, operation and operands agree");
                arena_pop_to(output, mark);
                for (uint32_t round = 0; admitted && round < 4; round += 1)
                {
                    const uint32_t bytes = 16u << width;
                    const uint32_t size = 1u << (flags & 3);
                    const bool is_signed = (operation & 1) != 0;
                    IrSimdValue value_a = { 0 };
                    IrSimdValue value_b = { 0 };
                    for (uint32_t w = 0; w < bytes / 8; w += 1)
                    {
                        state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                        value_a.words[w] = (state & 7) == 0 ? UINT64_C(0x7f7f80807fff8000) : (state & 7) == 1 ? UINT64_MAX : state;
                        state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                        value_b.words[w] = (state & 7) == 0 ? UINT64_C(0x80807f7f80007fff) : (state & 7) == 1 ? 0 : state;
                    }
                    const IrSimdValue* operands[3] = { &value_a, &value_b, 0 };
                    const IrSimdAction action = {
                        .operation = IR_SIMD_LANE_OP,
                        .result = ir_simd_shape(type),
                        .operands = { ir_simd_shape(type), ir_simd_shape(type), IR_SIMD_NONE },
                        .index = (uint8_t)flags,
                    };
                    IrSimdValue value;
                    memset(&value, 0xa5, sizeof(value));
                    bool exact = ir_simd_evaluate(&action, operands, 0, 0, 0, &value);
                    const uint8_t* in_a = (const uint8_t*)value_a.words;
                    const uint8_t* in_b = (const uint8_t*)value_b.words;
                    const uint8_t* out = (const uint8_t*)value.words;
                    for (uint32_t lane = 0; exact && lane < bytes / size; lane += 1)
                    {
                        uint64_t ua = 0;
                        uint64_t ub = 0;
                        memcpy(&ua, in_a + lane * size, size);
                        memcpy(&ub, in_b + lane * size, size);
                        // C's integers of the lane's size, written out per size.
                        const int64_t sa = size == 1 ? (int8_t)ua : size == 2 ? (int16_t)ua : size == 4 ? (int32_t)ua : (int64_t)ua;
                        const int64_t sb = size == 1 ? (int8_t)ub : size == 2 ? (int16_t)ub : size == 4 ? (int32_t)ub : (int64_t)ub;
                        const int64_t smax = size == 1 ? INT8_MAX : INT16_MAX;
                        const int64_t umax = size == 1 ? UINT8_MAX : UINT16_MAX;
                        const int64_t sum = is_signed ? sa + sb : (int64_t)(ua + ub);
                        const int64_t difference = is_signed ? sa - sb : (int64_t)ua - (int64_t)ub;
                        const int64_t exact_value = operation < 6 ? sum : difference;
                        const int64_t high = is_signed ? smax : umax;
                        const int64_t low = is_signed ? -smax - 1 : 0;
                        uint64_t expected = 0;
                        switch (operation)
                        {
                        case 0: expected = ua < ub ? ua : ub; break;
                        case 1: expected = sa < sb ? (uint64_t)sa : (uint64_t)sb; break;
                        case 2: expected = ua > ub ? ua : ub; break;
                        case 3: expected = sa > sb ? (uint64_t)sa : (uint64_t)sb; break;
                        default: expected = (uint64_t)(exact_value > high ? high : exact_value < low ? low : exact_value); break;
                        }
                        uint64_t actual = 0;
                        memcpy(&actual, out + lane * size, size);
                        exact = actual == (size == 8 ? expected : expected & ((UINT64_C(1) << (8 * size)) - 1));
                    }
                    for (uint32_t byte = bytes; exact && byte < 64; byte += 1)
                    {
                        exact = out[byte] == 0;
                    }
                    ir_test_expect(test, exact, "the model computes each lane as C does, its tail zero");
                }
            }
        }
    }
    // Minima and maxima of 4 lane sizes and saturations of 2, at each width.
    ir_test_expect(test, admitted_count == 3 * (4 * 4 + 4 * 2), "every lane operation of the IR's vector widths is admitted, and no other");
}

static void ir_test_simd(IrTestTally* test, Arena* output, Arena* scratch)
{
    ir_test_scalar_shifts(test, output, scratch);
    ir_test_lane_ops(test, output, scratch);
    ir_test_permute2s(test, output, scratch);
    ir_test_ternlogs(test, output, scratch);
    ir_test_compresses(test, output, scratch);
    ir_test_masked_memory(test, output, scratch);
    ir_test_lane_moves(test, output, scratch);
    ir_test_reductions(test, output, scratch);
    ir_test_lane_unaries(test, output, scratch);
    ir_test_frounds(test, output, scratch);
    ir_test_cvtts(test, output, scratch);
    ir_test_aes(test, output, scratch);
    ir_test_conversions(test, output, scratch);
    ir_test_permutes(test, output, scratch);
    ir_test_width_changes(test, output, scratch);
    ir_test_bit_intrinsics(test, output, scratch);
    ir_test_overflow_intrinsics(test, output, scratch);
    ir_test_splats(test, output, scratch);
    ir_test_simd_bitcasts(test, output, scratch);
    ir_test_simd_rows(test, output, scratch);
    ir_test_simd_lanes(test, output, scratch);
    ir_test_simd_lane_execution(test, output, scratch);
    const uint64_t mark = output->length;
    for (uint32_t width = 0; width < 3; width += 1)
    {
        IrFunction function = { 0 };
        const IrSizes sizes = { .instruction_count = 6, .block_count = 1, .vector_constant_count = 2 };
        const bool ready = ir_create(output, &sizes, &function) == IR_OK;
        ir_test_expect(test, ready, "SIMD IR fixture allocates sparse constant payloads");
        if (ready)
        {
            const IrType type = (IrType)(IR_V4I32 + width);
            const IrType mask = width == 2 ? IR_M16 : IR_M8;
            function.result_type = type;
            function.block_offsets[1] = 6;
            function.vector_constants[0].words[0] = UINT64_C(0x0000000200000001);
            function.vector_constants[1].words[0] = UINT64_C(0x0000000100000002);
            ir_test_instruction(&function, 0, IR_VCONST, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 1, IR_VCONST, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 2, IR_ADD, type, 0, 1, BUSTER_IR_NONE);
            ir_test_instruction(&function, 3, IR_VCMP, mask, 0, 1, IR_LT);
            ir_test_instruction(&function, 4, IR_VSELECT, type, 3, 2, 1);
            ir_test_instruction(&function, 5, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_pair(test, &function, scratch, NULL, true, "first-class vector arithmetic, masks and predication verify");
            const IrSimdExecution executed = ir_simd_execute(&function, NULL, 0, NULL, scratch);
            ir_test_expect(test, executed.status == IR_SIMD_EXECUTION_OK && executed.type == type &&
                           executed.value.words[0] == UINT64_C(0x0000000100000003) && ir_simd_value_valid(&executed.value, ir_simd_shape(type)),
                           "real IR arithmetic/compare-to-mask/predication executes against independent lane expectations");
            ir_test_printer(test, &function, output, scratch);
            function.type[3] = mask == IR_M8 ? IR_M16 : IR_M8;
            ir_test_pair(test, &function, scratch, NULL, false, "comparison cannot silently narrow or widen its predicate mask");
            function.type[3] = mask;
            function.type[2] = IR_I128;
            ir_test_pair(test, &function, scratch, NULL, false, "an equal-bit-width scalar is not a vector lane shape");
            function.type[2] = type;
            function.c[3] = IR_PREDICATE_COUNT;
            ir_test_pair(test, &function, scratch, NULL, false, "unknown vector predicates are rejected");
            function.c[3] = IR_LT;
            function.vector_constants[0].words[7] = width == 2 ? 0 : 1;
            ir_test_pair(test, &function, scratch, NULL, width == 2, "constant payload bits outside the exact vector shape are rejected");
            function.vector_constants[0].words[7] = 0;
            function.op[0] = IR_CONST;
            ir_test_pair(test, &function, scratch, NULL, false, "ordinary two-word constants cannot masquerade as wide vector constants");
            function.op[0] = IR_VCONST;
            function.flags[4] = BUSTER_IR_VOLATILE;
            ir_test_pair(test, &function, scratch, NULL, false, "a vector selection's flags name lanes, not memory effects: i8 lanes need their own mask");
        }
        arena_pop_to(output, mark);
    }
    IrFunction scalar = { 0 };
    const IrSizes ordinary = { .instruction_count = 1, .block_count = 1 };
    const bool ready = ir_create(output, &ordinary, &scalar) == IR_OK;
    ir_test_expect(test, ready && scalar.vector_constants == NULL && scalar.vector_constant_count == 0,
                   "ordinary scalar functions allocate no wide payload rows");
    arena_pop_to(output, mark);
    IrFunction preserved = { .instruction_count = 123 };
    const IrSizes huge = { .vector_constant_count = (uint64_t)UINT32_MAX + 1 };
    ir_test_expect(test, ir_create(output, &huge, &preserved) == IR_ERROR_LIMIT && preserved.instruction_count == 123 && output->length == mark,
                   "wide payload count overflow rejects before narrowing or publication");
    IrFunction memory = { 0 };
    const IrSizes memory_sizes = { .instruction_count = 5, .block_count = 1, .parameter_count = 1, .vector_constant_count = 1 };
    if (ir_create(output, &memory_sizes, &memory) == IR_OK)
    {
        memory.result_type = IR_V16I32;
        memory.parameter_types[0] = IR_PTR;
        memory.block_offsets[1] = 5;
        memory.vector_constants[0].words[7] = UINT64_C(0xfedcba9876543210);
        ir_test_instruction(&memory, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&memory, 1, IR_VCONST, IR_V16I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&memory, 2, IR_STORE, IR_VOID, 0, 1, BUSTER_IR_NONE);
        ir_test_instruction(&memory, 3, IR_LOAD, IR_V16I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&memory, 4, IR_RET, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        memory.flags[2] = BUSTER_IR_VOLATILE;
        memory.flags[3] = BUSTER_IR_VOLATILE;
        ir_test_pair(test, &memory, scratch, NULL, true, "vector memory retains explicit pointer operands and volatile effects");
        uint8_t bytes[66];
        memset(bytes, 0xa5, sizeof(bytes));
        IrSimdMemory slice = { .bytes = bytes + 1, .size = 64 };
        IrSimdArgument argument = { 0 };
        IrSimdExecution executed = ir_simd_execute(&memory, &argument, 1, &slice, scratch);
        ir_test_expect(test, executed.status == IR_SIMD_EXECUTION_OK && executed.value.words[7] == UINT64_C(0xfedcba9876543210) &&
                       slice.reads == 64 && slice.writes == 64 && slice.volatile_accesses == 128 && bytes[0] == 0xa5 && bytes[65] == 0xa5,
                       "real IR vector memory roundtrip preserves ordered observable effects and exact source tails");
        argument.offset = 1;
        const IrSimdMemory before = slice;
        executed = ir_simd_execute(&memory, &argument, 1, &slice, scratch);
        ir_test_expect(test, executed.status == IR_SIMD_EXECUTION_ACCESS && executed.instruction == 2 && memcmp(&slice, &before, sizeof(slice)) == 0,
                       "real IR oracle rejects the first invalid vector access without performing it");
        memory.alignment_log2[2] = 6;
        slice.size = 65;
        executed = ir_simd_execute(&memory, &argument, 1, &slice, scratch);
        ir_test_expect(test, executed.status == IR_SIMD_EXECUTION_ACCESS && executed.instruction == 2 && slice.writes == before.writes && bytes[65] == 0xa5,
                       "promised alignment is checked against logical offsets before a vector memory access");
        memory.alignment_log2[2] = 0;
        slice.size = 64;
        argument.offset = 0;
        IrFunction with_argument = memory;
        uint8_t parameter_types[2] = { IR_PTR, IR_M8 };
        with_argument.parameter_count = 2;
        with_argument.parameter_types = parameter_types;
        const IrSimdValue malformed_argument = { .words = { 256 } };
        const IrSimdArgument arguments[2] = { argument, { .value = &malformed_argument } };
        executed = ir_simd_execute(&with_argument, arguments, 2, &slice, scratch);
        ir_test_expect(test, executed.status == IR_SIMD_EXECUTION_INVALID && memcmp(&slice, &before, sizeof(slice)) == 0,
                       "all arguments, including unused ones, are admitted before any vector store can occur");
        _Alignas(64) uint8_t small_bytes[16384];
        uint32_t failures = 0;
        uint32_t successes = 0;
        for (uint64_t capacity = 13; capacity <= sizeof(small_bytes); capacity += 127)
        {
            memset(small_bytes, 0xa5, 13);
            Arena small = { .base = small_bytes, .length = 13, .capacity = capacity };
            IrSimdMemory limited = slice;
            executed = ir_simd_execute(&memory, &argument, 1, &limited, &small);
            const bool complete = executed.status == IR_SIMD_EXECUTION_OK;
            failures += !complete;
            successes += complete;
            bool prefix = true;
            for (uint32_t byte = 0; byte < 13; byte += 1)
            {
                prefix &= small_bytes[byte] == 0xa5;
            }
            ir_test_expect(test, small.length == 13 && prefix && (complete || (executed.status == IR_SIMD_EXECUTION_RESOURCE &&
                           memcmp(&limited, &slice, sizeof(slice)) == 0)), "oracle allocation failure restores scratch/prefix and performs no memory access");
        }
        ir_test_expect(test, failures != 0 && successes != 0, "oracle resource sweep crosses allocation failure and success boundaries");
        memory.op[1] = IR_UNDEF;
        memory.a[1] = BUSTER_IR_NONE;
        argument.offset = 0;
        executed = ir_simd_execute(&memory, &argument, 1, &slice, scratch);
        ir_test_expect(test, executed.status == IR_SIMD_EXECUTION_UNSUPPORTED && executed.instruction == 1 && memcmp(&slice, &before, sizeof(slice)) == 0,
                       "an unsupported semantic form refuses before any memory operation instead of guessing a scalar value");
    }
    else
    {
        ir_test_expect(test, false, "vector memory fixture allocates");
    }
    arena_pop_to(output, mark);
}
