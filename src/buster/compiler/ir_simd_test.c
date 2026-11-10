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

static void ir_test_simd(IrTestTally* test, Arena* output, Arena* scratch)
{
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
