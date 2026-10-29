// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Decode only the initial SIMD slice of #717's actual Tier0Operation contract. Include after tier0.c, ir.c and
// x86_target.c. This is an abstract semantic admission module, not a native selector: output-target features are
// explicit inputs and no compiler-host capability authorizes a form. Exact record roles/decorations/address fields
// distinguish an admitted operation from an arbitrary X86 sequence. All unsupported forms fail transactionally.
// The scalar corpus adapter remains independently owned under #131; this module does not modify that adapter.

typedef enum IrSimdTypedStatus
{
    IR_SIMD_TYPED_OK,
    IR_SIMD_TYPED_UNSUPPORTED,
    IR_SIMD_TYPED_TARGET,
    IR_SIMD_TYPED_INVALID,
} IrSimdTypedStatus;

typedef struct IrSimdTypedOperation IrSimdTypedOperation;
struct IrSimdTypedOperation
{
    IrSimdOperation operation;
    IrSimdShape shape;
    IrSimdPredicate predicate;
    uint64_t required_features;
    uint8_t operand_count;
    uint8_t operand_classes[BUSTER_TIER0_OPERATION_OPERANDS];
    uint8_t operand_bytes[BUSTER_TIER0_OPERATION_OPERANDS];
    // #717 represents a C comparison as a mask followed by -1/0 vector lanes, not the raw mask alone.
    bool expand_mask;
};

typedef struct IrSimdTypedForm IrSimdTypedForm;
struct IrSimdTypedForm
{
    uint16_t forms[3];
    IrSimdOperation operation;
};

static IrSimdTypedStatus ir_simd_typed_operation(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const IrSimdTypedForm binary[] = {
        { { X86_FORM_VPADDD_X_X_XM128_EVEX, X86_FORM_VPADDD_Y_Y_YM256_EVEX, X86_FORM_VPADDD_Z_Z_ZM512 }, IR_SIMD_ADD },
        { { X86_FORM_VPSUBD_X_X_XM128_EVEX, X86_FORM_VPSUBD_Y_Y_YM256_EVEX, X86_FORM_VPSUBD_Z_Z_ZM512 }, IR_SIMD_SUB },
        { { X86_FORM_VPMULLD_X_X_XM128_EVEX, X86_FORM_VPMULLD_Y_Y_YM256_EVEX, X86_FORM_VPMULLD_Z_Z_ZM512 }, IR_SIMD_MUL },
        { { X86_FORM_VPANDQ_X_X_XM128, X86_FORM_VPANDQ_Y_Y_YM256, X86_FORM_VPANDQ_Z_Z_ZM512 }, IR_SIMD_AND },
        { { X86_FORM_VPORQ_X_X_XM128, X86_FORM_VPORQ_Y_Y_YM256, X86_FORM_VPORQ_Z_Z_ZM512 }, IR_SIMD_OR },
        { { X86_FORM_VPXORQ_X_X_XM128, X86_FORM_VPXORQ_Y_Y_YM256, X86_FORM_VPXORQ_Z_Z_ZM512 }, IR_SIMD_XOR },
    };
    static const uint16_t complement[3] = { X86_FORM_VPTERNLOGQ_X_X_XM128_IMM8, X86_FORM_VPTERNLOGQ_Y_Y_YM256_IMM8, X86_FORM_VPTERNLOGQ_Z_Z_ZM512_IMM8 };
    static const uint16_t comparisons[2][3] = {
        { X86_FORM_VPCMPD_K_X_XM128_IMM8, X86_FORM_VPCMPD_K_Y_YM256_IMM8, X86_FORM_VPCMPD_K_Z_ZM512_IMM8 },
        { X86_FORM_VPCMPUD_K_X_XM128_IMM8, X86_FORM_VPCMPUD_K_Y_YM256_IMM8, X86_FORM_VPCMPUD_K_Z_ZM512_IMM8 },
    };
    static const uint16_t expansions[3] = { X86_FORM_VPMOVM2D_X_K, X86_FORM_VPMOVM2D_Y_K, X86_FORM_VPMOVM2D_Z_K };
    IrSimdTypedStatus status = source != NULL && result != NULL ? IR_SIMD_TYPED_UNSUPPORTED : IR_SIMD_TYPED_INVALID;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    uint32_t width = 0;
    if (status != IR_SIMD_TYPED_INVALID && source->result_class == BUSTER_TYPED_CLASS_VECTOR &&
        (source->result_bytes == 16 || source->result_bytes == 32 || source->result_bytes == 64) &&
        (source->operand_count == 1 || source->operand_count == 2) && source->record_count != 0 && source->record_count <= 2)
    {
        width = source->result_bytes == 16 ? 0 : source->result_bytes == 32 ? 1 : 2;
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        // Every form here is EVEX's: AVX512F, and AVX512VL for its 128- and 256-bit forms alone; VPMOVM2D adds AVX512DQ
        // (below).
        candidate.required_features = BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0);
        bool operands = true;
        for (uint32_t operand = 0; operand < source->operand_count; operand += 1)
        {
            operands &= source->operand_classes[operand] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[operand] == source->result_bytes;
        }
        if (operands)
        {
            const X86Instruction* record = source->records;
            const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
            const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
            X86Instruction expected = { .form = record->form, .reg = v0, .vvvv = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT };
            if (source->record_count == 1 && source->operand_count == 2 && memcmp(record, &expected, sizeof(expected)) == 0)
            {
                for (uint32_t form = 0; form < sizeof(binary) / sizeof(*binary); form += 1)
                {
                    if (record->form == binary[form].forms[width])
                    {
                        candidate.operation = binary[form].operation;
                    }
                }
            }
            else if (source->record_count == 1 && source->operand_count == 1)
            {
                expected = (X86Instruction){ .form = complement[width], .reg = v0, .vvvv = v0, .rm = v0, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = 0x55 };
                if (memcmp(record, &expected, sizeof(expected)) == 0)
                {
                    candidate.operation = IR_SIMD_NOT;
                }
            }
            else if (source->record_count == 2 && source->operand_count == 2)
            {
                expected = (X86Instruction){ .form = expansions[width], .reg = v0, .rm = 1, .mode = BUSTER_X86_REGISTER_DIRECT };
                const bool expansion = memcmp(record + 1, &expected, sizeof(expected)) == 0;
                for (uint32_t unsigned_lanes = 0; expansion && unsigned_lanes < 2; unsigned_lanes += 1)
                {
                    expected = (X86Instruction){ .form = comparisons[unsigned_lanes][width], .reg = 1, .vvvv = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = record->immediate };
                    if (memcmp(record, &expected, sizeof(expected)) == 0)
                    {
                        bool predicate = true;
                        switch (record->immediate)
                        {
                            case 0: candidate.predicate = IR_SIMD_EQ; break;
                            case 4: candidate.predicate = IR_SIMD_NE; break;
                            case 1: candidate.predicate = unsigned_lanes != 0 ? IR_SIMD_ULT : IR_SIMD_LT; break;
                            case 2: candidate.predicate = unsigned_lanes != 0 ? IR_SIMD_ULE : IR_SIMD_LE; break;
                            case 6: candidate.predicate = unsigned_lanes != 0 ? IR_SIMD_UGT : IR_SIMD_GT; break;
                            case 5: candidate.predicate = unsigned_lanes != 0 ? IR_SIMD_UGE : IR_SIMD_GE; break;
                            default: predicate = false; break;
                        }
                        if (predicate)
                        {
                            candidate.operation = IR_SIMD_COMPARE;
                            candidate.expand_mask = true;
                            candidate.required_features |= BUSTER_X86_FEATURE(AVX512DQ);
                        }
                    }
                }
            }
        }
        if (candidate.operation != IR_SIMD_OPERATION_COUNT)
        {
            status = (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_TARGET;
        }
    }
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}
