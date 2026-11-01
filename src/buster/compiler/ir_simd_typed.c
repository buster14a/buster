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

// A mask row's step (#179): its registers are indices, mask operand j's j, the result's the first mask operand's, which
// Tier 0 places MASK_RESULT in (tier0_operation_mask_plan), or BUSTER_IR_SIMD_MASK_RESULT in a row of no operand, and
// MASK_SCRATCH(k)'s BUSTER_IR_SIMD_MASK_SCRATCH + k. A step's kind is its record's instruction: and, or, xor, xnor
// (~(left ^ right)), andn (~left & right), not and move of `left`, add (modulo the mask's bits), shifts of `left` by
// `count` (0 from the mask's bits on, as kshiftl and kshiftr give), and unpack (kunpck: left's low half above right's).
#define BUSTER_IR_SIMD_MASK_RESULT 8
#define BUSTER_IR_SIMD_MASK_SCRATCH 9
#define BUSTER_IR_SIMD_MASK_REGISTERS (BUSTER_IR_SIMD_MASK_SCRATCH + BUSTER_TIER0_SCRATCH)

typedef enum IrSimdMaskStepKind
{
    IR_SIMD_MASK_AND,
    IR_SIMD_MASK_OR,
    IR_SIMD_MASK_XOR,
    IR_SIMD_MASK_XNOR,
    IR_SIMD_MASK_ANDN,
    IR_SIMD_MASK_ADD,
    IR_SIMD_MASK_UNPACK,
    IR_SIMD_MASK_NOT,
    IR_SIMD_MASK_MOVE,
    IR_SIMD_MASK_SHIFT_LEFT,
    IR_SIMD_MASK_SHIFT_RIGHT,
    IR_SIMD_MASK_KINDS,
    // A flags test's step (ir_simd_typed_mask_test), a row's only one: its int, 1 where the flag `count` names is set (0
    // ZF, 1 CF) and 0 where it isn't, into the result's register. ktest's ZF is set where left & right is 0, its CF where
    // ~left & right is; kortest's ZF where left | right is 0, its CF where it has every one of the mask's bits.
    IR_SIMD_MASK_TEST = IR_SIMD_MASK_KINDS,
    IR_SIMD_MASK_ORTEST,
} IrSimdMaskStepKind;

typedef struct IrSimdTypedMaskStep IrSimdTypedMaskStep;
struct IrSimdTypedMaskStep
{
    uint8_t kind;
    uint8_t destination;
    uint8_t left;
    uint8_t right;
    uint8_t count;
};

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
    // A row of opmask instructions alone (#179, ir_simd_typed_mask_row): its masks' bytes (0 for a vector operation), its
    // records' steps in order, and the register its result is in when they end.
    uint8_t mask_bytes;
    uint8_t mask_step_count;
    uint8_t mask_result;
    IrSimdTypedMaskStep mask_steps[BUSTER_TIER0_OPERATION_RECORDS];
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

// A mask row's register for a role (BUSTER_IR_SIMD_MASK_* indices), or BUSTER_IR_SIMD_MASK_REGISTERS for a field that
// isn't one of its roles: an opmask register by number among them, which a row with masks doesn't name.
static uint32_t ir_simd_typed_mask_register(const Tier0Operation* source, uint8_t role)
{
    uint32_t reg = BUSTER_IR_SIMD_MASK_REGISTERS;
    if (role >= BUSTER_TIER0_ROLE_MASK(0) && role < BUSTER_TIER0_ROLE_MASK(source->operand_count))
    {
        reg = role & 7;
    }
    else if (role == BUSTER_TIER0_ROLE_MASK_RESULT)
    {
        reg = source->operand_count != 0 ? 0 : BUSTER_IR_SIMD_MASK_RESULT;
    }
    else if (role >= BUSTER_TIER0_ROLE_MASK_SCRATCH(0) && role < BUSTER_TIER0_ROLE_MASK_SCRATCH(BUSTER_TIER0_SCRATCH))
    {
        reg = BUSTER_IR_SIMD_MASK_SCRATCH + (role & 3u);
    }
    return reg;
}

// A row of opmask instructions alone (#179): masks of one width w in and out (1, 2, 4 or 8 bytes), each record one of
// kand, kor, kxor, kxnor, kandn, kadd, kunpck, knot and kshiftl or kshiftr at w, or a kmov between registers at w or
// wider, which copies a mask whole (its bits past w are zero), its fields its roles alone, register-direct, without a
// decoration or another field, every register it reads written before. Its result is its first operand's register's, or
// MASK_RESULT's, at its end. These are the _k* builtins' rows (check_x86_lower_masks's): KBINARY's kandw MASK(0),
// MASK(0), MASK(1), KMOV's with no record. Its features are its forms': AVX-512F, DQ for the byte forms and kaddw, BW
// for the doubleword and quadword ones, kunpckwd and kunpckdq. Any other row, a vector's or a flags test's among them,
// is IR_SIMD_TYPED_UNSUPPORTED.
static IrSimdTypedStatus ir_simd_typed_mask_row(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const uint16_t forms[IR_SIMD_MASK_KINDS][4] = {
        [IR_SIMD_MASK_AND] = { X86_FORM_KANDB_K_K_K, X86_FORM_KANDW_K_K_K, X86_FORM_KANDD_K_K_K, X86_FORM_KANDQ_K_K_K },
        [IR_SIMD_MASK_OR] = { X86_FORM_KORB_K_K_K, X86_FORM_KORW_K_K_K, X86_FORM_KORD_K_K_K, X86_FORM_KORQ_K_K_K },
        [IR_SIMD_MASK_XOR] = { X86_FORM_KXORB_K_K_K, X86_FORM_KXORW_K_K_K, X86_FORM_KXORD_K_K_K, X86_FORM_KXORQ_K_K_K },
        [IR_SIMD_MASK_XNOR] = { X86_FORM_KXNORB_K_K_K, X86_FORM_KXNORW_K_K_K, X86_FORM_KXNORD_K_K_K, X86_FORM_KXNORQ_K_K_K },
        [IR_SIMD_MASK_ANDN] = { X86_FORM_KANDNB_K_K_K, X86_FORM_KANDNW_K_K_K, X86_FORM_KANDND_K_K_K, X86_FORM_KANDNQ_K_K_K },
        [IR_SIMD_MASK_ADD] = { X86_FORM_KADDB_K_K_K, X86_FORM_KADDW_K_K_K, X86_FORM_KADDD_K_K_K, X86_FORM_KADDQ_K_K_K },
        [IR_SIMD_MASK_UNPACK] = { X86_FORM_NONE, X86_FORM_KUNPCKBW_K_K_K, X86_FORM_KUNPCKWD_K_K_K, X86_FORM_KUNPCKDQ_K_K_K },
        [IR_SIMD_MASK_NOT] = { X86_FORM_KNOTB_K_K, X86_FORM_KNOTW_K_K, X86_FORM_KNOTD_K_K, X86_FORM_KNOTQ_K_K },
        [IR_SIMD_MASK_MOVE] = { X86_FORM_KMOVB_K_K, X86_FORM_KMOVW_K_K, X86_FORM_KMOVD_K_K, X86_FORM_KMOVQ_K_K },
        [IR_SIMD_MASK_SHIFT_LEFT] = { X86_FORM_KSHIFTLB_K_K_IMM8, X86_FORM_KSHIFTLW_K_K_IMM8, X86_FORM_KSHIFTLD_K_K_IMM8, X86_FORM_KSHIFTLQ_K_K_IMM8 },
        [IR_SIMD_MASK_SHIFT_RIGHT] = { X86_FORM_KSHIFTRB_K_K_IMM8, X86_FORM_KSHIFTRW_K_K_IMM8, X86_FORM_KSHIFTRD_K_K_IMM8, X86_FORM_KSHIFTRQ_K_K_IMM8 },
    };
    const uint32_t bytes = source != NULL ? source->result_bytes : 0;
    const uint32_t width = bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3;
    IrSimdTypedStatus status = source != NULL && result != NULL ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_INVALID;
    bool admitted = status == IR_SIMD_TYPED_OK && source->result_class == BUSTER_TYPED_CLASS_MASK && (bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8) &&
                    source->operand_count <= BUSTER_TIER0_OPERATION_OPERANDS && source->record_count <= BUSTER_TIER0_OPERATION_RECORDS;
    for (uint32_t j = 0; admitted && j < source->operand_count; j += 1)
    {
        admitted = source->operand_classes[j] == BUSTER_TYPED_CLASS_MASK && source->operand_bytes[j] == bytes;
    }
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT, .mask_bytes = (uint8_t)bytes };
    candidate.required_features = BUSTER_X86_FEATURE(AVX512F);
    uint32_t defined = admitted ? (1u << source->operand_count) - 1u : 0;
    for (uint32_t r = 0; admitted && r < source->record_count; r += 1)
    {
        const X86Instruction* record = &source->records[r];
        uint32_t kind = IR_SIMD_MASK_KINDS;
        uint32_t form_width = 4;
        for (uint32_t k = 0; k < IR_SIMD_MASK_KINDS; k += 1)
        {
            for (uint32_t w = 0; w < 4; w += 1)
            {
                if (forms[k][w] != X86_FORM_NONE && record->form == forms[k][w])
                {
                    kind = k;
                    form_width = w;
                }
            }
        }
        const bool binary = kind <= IR_SIMD_MASK_UNPACK;
        const bool shift = kind == IR_SIMD_MASK_SHIFT_LEFT || kind == IR_SIMD_MASK_SHIFT_RIGHT;
        // A move copies a mask whole at its width or wider; every other record is the masks' width.
        admitted = kind != IR_SIMD_MASK_KINDS && (kind == IR_SIMD_MASK_MOVE ? form_width >= width : form_width == width);
        const X86Instruction expected = {
            .form = record->form,
            .reg = record->reg,
            .vvvv = binary ? record->vvvv : 0,
            .rm = record->rm,
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .immediate = shift ? record->immediate : 0,
        };
        admitted = admitted && memcmp(record, &expected, sizeof(expected)) == 0 && (!shift || (record->immediate >= 0 && record->immediate <= 255));
        const uint32_t destination = ir_simd_typed_mask_register(source, record->reg);
        const uint32_t left = ir_simd_typed_mask_register(source, binary ? record->vvvv : record->rm);
        const uint32_t right = binary ? ir_simd_typed_mask_register(source, record->rm) : left;
        admitted = admitted && destination < BUSTER_IR_SIMD_MASK_REGISTERS && left < BUSTER_IR_SIMD_MASK_REGISTERS && right < BUSTER_IR_SIMD_MASK_REGISTERS &&
                   ((defined >> left) & (defined >> right) & 1u) != 0;
        if (admitted)
        {
            candidate.mask_steps[candidate.mask_step_count] = (IrSimdTypedMaskStep){
                .kind = (uint8_t)kind,
                .destination = (uint8_t)destination,
                .left = (uint8_t)left,
                .right = (uint8_t)right,
                .count = (uint8_t)(shift ? record->immediate : 0),
            };
            candidate.mask_step_count += 1;
            defined |= 1u << destination;
            candidate.required_features |= form_width == 0 || (kind == IR_SIMD_MASK_ADD && form_width == 1) ? BUSTER_X86_FEATURE(AVX512DQ) : 0;
            candidate.required_features |= form_width >= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
        }
    }
    const uint32_t returned = admitted && source->operand_count != 0 ? 0 : BUSTER_IR_SIMD_MASK_RESULT;
    admitted = admitted && ((defined >> returned) & 1u) != 0;
    if (status == IR_SIMD_TYPED_OK)
    {
        status = !admitted ? IR_SIMD_TYPED_UNSUPPORTED : (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_TARGET;
    }
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.mask_result = (uint8_t)returned;
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// A row of two masks of one width w (1, 2, 4 or 8 bytes) and an int (#179): ktest or kortest at w of MASK(0) and MASK(1),
// in either order, register-direct, then sete or setb into SCRATCH(0)'s byte and its zero extension into INTEGER_RESULT,
// each record with no other field. These are the kortestz, kortestc, ktestz and ktestc builtins' rows
// (check_x86_lower_masks's of KTEST's): its step is IR_SIMD_MASK_TEST's or IR_SIMD_MASK_ORTEST's of its flag. Its
// features are its form's: AVX-512F, DQ for ktestb, ktestw and kortestb, BW for the doubleword and quadword forms. Any
// other row of mask operands and an integer result is IR_SIMD_TYPED_UNSUPPORTED.
static IrSimdTypedStatus ir_simd_typed_mask_test(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const uint16_t forms[2][4] = {
        { X86_FORM_KTESTB_K_K, X86_FORM_KTESTW_K_K, X86_FORM_KTESTD_K_K, X86_FORM_KTESTQ_K_K },
        { X86_FORM_KORTESTB_K_K, X86_FORM_KORTESTW_K_K, X86_FORM_KORTESTD_K_K, X86_FORM_KORTESTQ_K_K },
    };
    IrSimdTypedStatus status = source != NULL && result != NULL ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_INVALID;
    const uint32_t bytes = status == IR_SIMD_TYPED_OK ? source->operand_bytes[0] : 0;
    const uint32_t width = bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3;
    bool admitted = status == IR_SIMD_TYPED_OK && source->result_class == BUSTER_TYPED_CLASS_I32 && source->result_bytes == 0 && source->operand_count == 2 &&
                    source->record_count == 3 && (bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8);
    for (uint32_t j = 0; admitted && j < 2; j += 1)
    {
        admitted = source->operand_classes[j] == BUSTER_TYPED_CLASS_MASK && source->operand_bytes[j] == bytes;
    }
    const X86Instruction* records = source != NULL ? source->records : NULL;
    const uint8_t m0 = BUSTER_TIER0_ROLE_MASK(0);
    const uint8_t m1 = BUSTER_TIER0_ROLE_MASK(1);
    const uint32_t kind = admitted && records[0].form == forms[0][width] ? 0 : admitted && records[0].form == forms[1][width] ? 1 : 2;
    const bool swapped = admitted && records[0].reg == m1;
    const X86Instruction test = { .form = records != NULL ? records[0].form : 0, .reg = swapped ? m1 : m0, .rm = swapped ? m0 : m1, .mode = BUSTER_X86_REGISTER_DIRECT };
    const uint16_t set = admitted ? records[1].form : 0;
    const X86Instruction condition = { .form = set, .rm = BUSTER_TIER0_ROLE_SCRATCH(0), .mode = BUSTER_X86_REGISTER_DIRECT };
    const X86Instruction extension = { .form = X86_FORM_MOVZX_R32_RM8, .reg = BUSTER_TIER0_ROLE_INTEGER_RESULT, .rm = BUSTER_TIER0_ROLE_SCRATCH(0), .mode = BUSTER_X86_REGISTER_DIRECT };
    admitted = admitted && kind < 2 && (set == X86_FORM_SETE_RM8 || set == X86_FORM_SETB_RM8) && memcmp(&records[0], &test, sizeof(test)) == 0 &&
               memcmp(&records[1], &condition, sizeof(condition)) == 0 && memcmp(&records[2], &extension, sizeof(extension)) == 0;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT, .mask_bytes = (uint8_t)bytes, .mask_step_count = 1, .mask_result = BUSTER_IR_SIMD_MASK_RESULT };
    candidate.mask_steps[0] = (IrSimdTypedMaskStep){
        .kind = (uint8_t)(kind == 0 ? IR_SIMD_MASK_TEST : IR_SIMD_MASK_ORTEST),
        .destination = BUSTER_IR_SIMD_MASK_RESULT,
        .left = swapped ? 1 : 0,
        .right = swapped ? 0 : 1,
        .count = set == X86_FORM_SETB_RM8 ? 1 : 0,
    };
    const bool dq = kind == 0 ? width <= 1 : width == 0;
    candidate.required_features = BUSTER_X86_FEATURE(AVX512F) | (dq ? BUSTER_X86_FEATURE(AVX512DQ) : 0) | (width >= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
    if (status == IR_SIMD_TYPED_OK)
    {
        status = !admitted ? IR_SIMD_TYPED_UNSUPPORTED : (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_TARGET;
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

// A row's decoding: a mask result's by ir_simd_typed_mask_row, an int's of mask operands by ir_simd_typed_mask_test,
// any other's by ir_simd_typed_operation.
static IrSimdTypedStatus ir_simd_typed_row(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const bool masks = source != NULL && source->operand_count != 0 && source->operand_classes[0] == BUSTER_TYPED_CLASS_MASK && tier0_class_integer(source->result_class);
    return source != NULL && source->result_class == BUSTER_TYPED_CLASS_MASK ? ir_simd_typed_mask_row(source, target_features, result) :
           masks                                                             ? ir_simd_typed_mask_test(source, target_features, result) :
                                                                               ir_simd_typed_operation(source, target_features, result);
}
