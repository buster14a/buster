// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Actual #717 producer rows, independent decoded metadata and mutation controls. Include after the producer,
// c_types_test.c, ir_test.c and ir_simd_typed.c. No scalar corpus-adapter function is changed or emulated here.

static void ir_simd_typed_test(IrTestTally* tally)
{
    TypeTestTable table = { 0 };
    const bool ready = type_test_table_open(&table, 256);
    ir_test_expect(tally, ready, "typed SIMD operation fixtures initialize a real C type table");
    const uint64_t target = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512DQ);
    static const CheckOp operations[] = {
        CHECK_OP_ADD, CHECK_OP_SUBTRACT, CHECK_OP_MULTIPLY, CHECK_OP_BIT_AND, CHECK_OP_BIT_OR, CHECK_OP_BIT_XOR, CHECK_OP_BIT_NOT,
        CHECK_OP_LESS, CHECK_OP_LESS_EQUAL, CHECK_OP_GREATER, CHECK_OP_GREATER_EQUAL, CHECK_OP_EQUAL, CHECK_OP_NOT_EQUAL,
    };
    static const IrSimdOperation semantics[] = {
        IR_SIMD_ADD, IR_SIMD_SUB, IR_SIMD_MUL, IR_SIMD_AND, IR_SIMD_OR, IR_SIMD_XOR, IR_SIMD_NOT,
        IR_SIMD_COMPARE, IR_SIMD_COMPARE, IR_SIMD_COMPARE, IR_SIMD_COMPARE, IR_SIMD_COMPARE, IR_SIMD_COMPARE,
    };
    static const IrSimdPredicate predicates[2][6] = {
        { IR_SIMD_LT, IR_SIMD_LE, IR_SIMD_GT, IR_SIMD_GE, IR_SIMD_EQ, IR_SIMD_NE },
        { IR_SIMD_ULT, IR_SIMD_ULE, IR_SIMD_UGT, IR_SIMD_UGE, IR_SIMD_EQ, IR_SIMD_NE },
    };
    for (uint32_t width = 0; ready && width < 3; width += 1)
    {
        for (uint32_t unsigned_lanes = 0; unsigned_lanes < 2; unsigned_lanes += 1)
        {
            const TypeId type = type_vector(&table.table, unsigned_lanes != 0 ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT, 4u << width, false);
            for (uint32_t operation = 0; operation < sizeof(operations) / sizeof(*operations); operation += 1)
            {
                Tier0Operation row = { 0 };
                const char* reason = check_vector_lower_row(&table.table, target, operations[operation], type, BUSTER_TYPED_CLASS_VOID, &row);
                IrSimdTypedOperation decoded = { 0 };
                const IrSimdTypedStatus admitted = ir_simd_typed_operation(&row, target, &decoded);
                const bool compare = operation >= 7;
                ir_test_expect(tally, reason == NULL && admitted == IR_SIMD_TYPED_OK && decoded.operation == semantics[operation] &&
                               decoded.shape == (IrSimdShape)(IR_SIMD_V4I32 + width) && decoded.expand_mask == compare &&
                               (!compare || decoded.predicate == predicates[unsigned_lanes][operation - 7]),
                               "actual vector producer rows decode into independent arithmetic/bitwise/comparison semantics at every width");
                ir_test_expect(tally, decoded.operand_count == row.operand_count &&
                               memcmp(decoded.operand_classes, row.operand_classes, sizeof(row.operand_classes)) == 0 &&
                               memcmp(decoded.operand_bytes, row.operand_bytes, sizeof(row.operand_bytes)) == 0,
                               "typed argument metadata is retained exactly rather than inferred from a host register class");
                memset(&decoded, 0xa5, sizeof(decoded));
                const IrSimdTypedOperation before = decoded;
                ir_test_expect(tally, ir_simd_typed_operation(&row, 0, &decoded) == IR_SIMD_TYPED_TARGET &&
                               memcmp(&decoded, &before, sizeof(decoded)) == 0,
                               "a capable compiler host cannot authorize an unsupported baseline output target");
                const uint64_t missing = compare ? target & ~BUSTER_X86_FEATURE(AVX512DQ) : target & ~BUSTER_X86_FEATURE(AVX512VL);
                ir_test_expect(tally, ir_simd_typed_operation(&row, missing, &decoded) == IR_SIMD_TYPED_TARGET &&
                               memcmp(&decoded, &before, sizeof(decoded)) == 0,
                               "each operation's exact producer feature requirement is checked before publication");
                Tier0Operation altered = row;
                altered.records[0].decorations = 1;
                ir_test_expect(tally, ir_simd_typed_operation(&altered, target, &decoded) == IR_SIMD_TYPED_UNSUPPORTED &&
                               memcmp(&decoded, &before, sizeof(decoded)) == 0,
                               "masked register decorations are refused until their independent merge/zeroing contract is modeled");
                altered = row;
                altered.records[0].displacement = 4;
                ir_test_expect(tally, ir_simd_typed_operation(&altered, target, &decoded) == IR_SIMD_TYPED_UNSUPPORTED,
                               "unmodeled address or displacement fields cannot be discarded from X86 semantics");
                altered = row;
                altered.records[0].reg ^= 2;
                ir_test_expect(tally, ir_simd_typed_operation(&altered, target, &decoded) == IR_SIMD_TYPED_UNSUPPORTED,
                               "a changed destination or temporary-mask role is not the recognized producer operation");
                altered = row;
                altered.operand_bytes[0] /= 2;
                ir_test_expect(tally, ir_simd_typed_operation(&altered, target, &decoded) == IR_SIMD_TYPED_UNSUPPORTED,
                               "operand widths cannot be silently truncated to the result shape");
            }
            Tier0Operation unsupported = { 0 };
            const TypeId floating = type_vector(&table.table, BUSTER_TYPE_FLOAT, 4u << width, false);
            const char* reason = check_vector_lower_row(&table.table, target, CHECK_OP_ADD, floating, BUSTER_TYPED_CLASS_VOID, &unsupported);
            IrSimdTypedOperation decoded = { 0 };
            ir_test_expect(tally, reason == NULL && ir_simd_typed_operation(&unsupported, target, &decoded) == IR_SIMD_TYPED_UNSUPPORTED,
                           "an otherwise valid floating producer operation remains explicitly outside the integer lane subset");
        }
    }
    IrSimdTypedOperation decoded = { 0 };
    ir_test_expect(tally, ir_simd_typed_operation(NULL, target, &decoded) == IR_SIMD_TYPED_INVALID,
                   "missing typed operation input is rejected before indexing a record");
    type_test_table_close(&table);
}
