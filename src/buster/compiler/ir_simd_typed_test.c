// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Actual #717 producer rows, independent decoded metadata and mutation controls. Include after the producer,
// c_types_test.c, ir_test.c and ir_simd_typed.c. No scalar corpus-adapter function is changed or emulated here.

// The front end's opmask rows (#179): every rule of the families whose rows, moved into Tier 0's mask class
// (check_x86_lower_masks), take and give masks alone (KBINARY's, ktestzdi's among them, KSHIFT's at counts around
// every width, KUNARY's and KMOV's) or take two and give an int (KTEST's), at a target of every feature. Each decodes,
// its masks' bytes the rule's, a step per record (none for KMOV's), its result in its first operand's register (a
// test's in the result's), its features exactly the union of x86_target_requirement's for its records' forms (written
// from Intel's manual in x86_target_rows.c, not the decoder's table), and refuses as TARGET without each of them, the
// result untouched; a step's kind the one its form names, its count the shift's (a flags test's its condition: setb's
// 1, sete's 0). Then each row changed one way at a time: a decoration, a displacement, memory, a destination by number
// (k3, not a role), another width's form, a narrower kmov, an operand of another width or class, a test's other condition
// (setne) or result move, a record read before it is written and one too many: none the front end's operation
// (ir_simd_typed_test_undecoded).
static void ir_simd_typed_test_masks(IrTestTally* tally);

// Whether a changed row isn't decoded as the front end's operation: the IR's own operations refuse it, so that it is at
// most the IR's machine row (#141, the row itself, ir_simd_typed_machine_row), or refused, by its classes or the target.
static bool ir_simd_typed_test_undecoded(IrSimdTypedStatus status, const IrSimdTypedOperation* decoded)
{
    const bool machine = status == IR_SIMD_TYPED_OK && decoded->machine && decoded->mask_bytes == 0 && decoded->intrinsic == 0 && decoded->operation == IR_SIMD_OPERATION_COUNT;
    return status == IR_SIMD_TYPED_UNSUPPORTED || status == IR_SIMD_TYPED_TARGET || machine;
}

static void ir_simd_typed_test_masks(IrTestTally* tally)
{
    const X86Target every = { .features = UINT64_MAX, .extensions = UINT64_MAX };
    const uint64_t avx512 = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ);
    static const uint64_t counts[] = { 0, 1, 3, 7, 8, 15, 16, 31, 32, 63, 64, 255 };
    // Each form's step kind, written out: the four widths of each instruction, a kunpck's of the result's width.
    static const uint16_t kinds[][5] = {
        { IR_SIMD_MASK_AND, X86_FORM_KANDB_K_K_K, X86_FORM_KANDW_K_K_K, X86_FORM_KANDD_K_K_K, X86_FORM_KANDQ_K_K_K },
        { IR_SIMD_MASK_OR, X86_FORM_KORB_K_K_K, X86_FORM_KORW_K_K_K, X86_FORM_KORD_K_K_K, X86_FORM_KORQ_K_K_K },
        { IR_SIMD_MASK_XOR, X86_FORM_KXORB_K_K_K, X86_FORM_KXORW_K_K_K, X86_FORM_KXORD_K_K_K, X86_FORM_KXORQ_K_K_K },
        { IR_SIMD_MASK_XNOR, X86_FORM_KXNORB_K_K_K, X86_FORM_KXNORW_K_K_K, X86_FORM_KXNORD_K_K_K, X86_FORM_KXNORQ_K_K_K },
        { IR_SIMD_MASK_ANDN, X86_FORM_KANDNB_K_K_K, X86_FORM_KANDNW_K_K_K, X86_FORM_KANDND_K_K_K, X86_FORM_KANDNQ_K_K_K },
        { IR_SIMD_MASK_ADD, X86_FORM_KADDB_K_K_K, X86_FORM_KADDW_K_K_K, X86_FORM_KADDD_K_K_K, X86_FORM_KADDQ_K_K_K },
        { IR_SIMD_MASK_UNPACK, X86_FORM_NONE, X86_FORM_KUNPCKBW_K_K_K, X86_FORM_KUNPCKWD_K_K_K, X86_FORM_KUNPCKDQ_K_K_K },
        { IR_SIMD_MASK_NOT, X86_FORM_KNOTB_K_K, X86_FORM_KNOTW_K_K, X86_FORM_KNOTD_K_K, X86_FORM_KNOTQ_K_K },
        { IR_SIMD_MASK_SHIFT_LEFT, X86_FORM_KSHIFTLB_K_K_IMM8, X86_FORM_KSHIFTLW_K_K_IMM8, X86_FORM_KSHIFTLD_K_K_IMM8, X86_FORM_KSHIFTLQ_K_K_IMM8 },
        { IR_SIMD_MASK_SHIFT_RIGHT, X86_FORM_KSHIFTRB_K_K_IMM8, X86_FORM_KSHIFTRW_K_K_IMM8, X86_FORM_KSHIFTRD_K_K_IMM8, X86_FORM_KSHIFTRQ_K_K_IMM8 },
        { IR_SIMD_MASK_TEST, X86_FORM_KTESTB_K_K, X86_FORM_KTESTW_K_K, X86_FORM_KTESTD_K_K, X86_FORM_KTESTQ_K_K },
        { IR_SIMD_MASK_ORTEST, X86_FORM_KORTESTB_K_K, X86_FORM_KORTESTW_K_K, X86_FORM_KORTESTD_K_K, X86_FORM_KORTESTQ_K_K },
    };
    uint32_t families[5] = { 0 };
    uint32_t rows = 0;
    // check_x86_lower_masks validates the rows it makes (tier0_operation_valid asks the effects tables, #760).
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t i = 0; tables && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const uint8_t family_list[5] = { CHECK_X86_LOWER_KBINARY, CHECK_X86_LOWER_KSHIFT, CHECK_X86_LOWER_KUNARY, CHECK_X86_LOWER_KMOV, CHECK_X86_LOWER_KTEST };
        uint32_t family = 5;
        for (uint32_t f = 0; f < 5; f += 1)
        {
            family = rule->family == family_list[f] ? f : family;
        }
        const bool shift = rule->family == CHECK_X86_LOWER_KSHIFT;
        for (uint32_t c = 0; family < 5 && c < (shift ? BUSTER_ARRAY_LENGTH(counts) : 1); c += 1)
        {
            Tier0Operation row;
            char description[192];
            const char* reason = check_x86_lower_row_of(&every, rule, counts[c], 4, 0, false, &row);
            const bool moved = reason == NULL && check_x86_lower_masks(&row, &every);
            snprintf(description, sizeof(description), "%s: its row of masks is the front end's at every feature", rule->name);
            ir_test_expect(tally, moved, description);
            if (!moved)
            {
                continue;
            }
            rows += 1;
            families[family] += 1;
            const bool test = rule->family == CHECK_X86_LOWER_KTEST;
            IrSimdTypedOperation decoded;
            memset(&decoded, 0xa5, sizeof(decoded));
            const IrSimdTypedStatus status = ir_simd_typed_row(&row, avx512, &decoded);
            uint64_t expected = BUSTER_X86_FEATURE(AVX512F);
            bool kinds_right = status == IR_SIMD_TYPED_OK && decoded.mask_step_count == (test ? 1 : row.record_count);
            for (uint32_t r = 0; r < row.record_count; r += 1)
            {
                expected |= x86_target_requirement(row.records[r].form) & avx512;
                uint32_t kind = IR_SIMD_MASK_KINDS + 2;
                for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(kinds); k += 1)
                {
                    for (uint32_t w = 1; w < 5; w += 1)
                    {
                        kind = kinds[k][w] != X86_FORM_NONE && kinds[k][w] == row.records[r].form ? kinds[k][0] : kind;
                    }
                }
                const bool step = !test || r == 0;
                kinds_right &= !step || (r < decoded.mask_step_count && decoded.mask_steps[r].kind == kind &&
                                         decoded.mask_steps[r].count == (shift ? (uint8_t)row.records[r].immediate : test ? (row.records[1].form == X86_FORM_SETB_RM8 ? 1 : 0) : 0));
            }
            snprintf(description, sizeof(description), "%s (count %" PRIu64 "): the row decodes, a step per record of its form's kind, at the rule's bytes", rule->name, counts[c]);
            ir_test_expect(tally, kinds_right && decoded.mask_bytes == rule->bytes && decoded.operand_count == row.operand_count &&
                                  decoded.mask_result == (test ? BUSTER_IR_SIMD_MASK_RESULT : 0),
                           description);
            snprintf(description, sizeof(description), "%s: the row's features are its forms' (x86_target_rows.c's)", rule->name);
            ir_test_expect(tally, status == IR_SIMD_TYPED_OK && decoded.required_features == expected, description);
            const uint64_t features[4] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ) };
            for (uint32_t f = 0; f < 4; f += 1)
            {
                IrSimdTypedOperation without;
                memset(&without, 0x5a, sizeof(without));
                const IrSimdTypedOperation before = without;
                const IrSimdTypedStatus refused = ir_simd_typed_row(&row, avx512 & ~features[f], &without);
                const bool needed = (expected & features[f]) != 0;
                snprintf(description, sizeof(description), "%s: each of its features is needed, the result untouched without it, and no other", rule->name);
                ir_test_expect(tally, needed ? refused == IR_SIMD_TYPED_TARGET && memcmp(&without, &before, sizeof(without)) == 0 : refused == IR_SIMD_TYPED_OK, description);
            }
            // The mutations, each of one record of the row (its first, or a test's), and of its operands.
            const uint32_t mutations = 12;
            for (uint32_t m = 0; m < mutations; m += 1)
            {
                Tier0Operation changed = row;
                X86Instruction* first = &changed.records[0];
                bool applies = row.record_count != 0;
                switch (m)
                {
                case 0: first->decorations = 1; break;
                case 1: first->displacement = 8; break;
                case 2: first->mode = BUSTER_X86_MEMORY_BASE; break;
                case 3: first->reg = 3; break;
                case 4:
                {
                    // Another width's form of the same instruction (a kunpck's or a kmov's past the masks' none).
                    uint16_t other = X86_FORM_NONE;
                    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(kinds); k += 1)
                    {
                        for (uint32_t w = 1; w < 5; w += 1)
                        {
                            const uint32_t next = w == 4 ? 1 : w + 1;
                            other = kinds[k][w] == first->form && kinds[k][next] != X86_FORM_NONE ? kinds[k][next] : other;
                        }
                    }
                    applies = applies && other != X86_FORM_NONE;
                    first->form = other;
                } break;
                case 5:
                    // An operand of another width.
                    applies = row.operand_count != 0;
                    changed.operand_bytes[0] = (uint8_t)(rule->bytes == 8 ? 4 : rule->bytes * 2);
                    break;
                case 6:
                    applies = row.operand_count != 0;
                    changed.operand_classes[0] = BUSTER_TYPED_CLASS_U64;
                    break;
                case 7:
                    // A test's other condition, or a narrower kmov in place of its first record.
                    applies = test || (row.record_count != 0 && rule->bytes > 1);
                    if (test)
                    {
                        changed.records[1].form = X86_FORM_SETNE_RM8;
                    }
                    else
                    {
                        static const uint16_t moves[4] = { X86_FORM_KMOVB_K_K, X86_FORM_KMOVW_K_K, X86_FORM_KMOVD_K_K, X86_FORM_KMOVQ_K_K };
                        const uint32_t width = rule->bytes == 2 ? 0 : rule->bytes == 4 ? 1 : 2;
                        *first = (X86Instruction){ .form = moves[width], .reg = first->reg, .rm = BUSTER_TIER0_ROLE_MASK(0), .mode = BUSTER_X86_REGISTER_DIRECT };
                    }
                    break;
                case 8:
                    // A test's result moved from another register, or a record that reads a scratch register no record wrote.
                    if (test)
                    {
                        changed.records[2].rm = BUSTER_TIER0_ROLE_SCRATCH(1);
                    }
                    else if (row.record_count != 0)
                    {
                        first->rm = BUSTER_TIER0_ROLE_MASK_SCRATCH(1);
                    }
                    break;
                case 9:
                    // One record too many: a kmov of the result's register after a test's, a kmov from an unwritten scratch.
                    applies = row.record_count < BUSTER_TIER0_OPERATION_RECORDS;
                    changed.records[row.record_count] = (X86Instruction){ .form = X86_FORM_KMOVQ_K_K, .reg = BUSTER_TIER0_ROLE_MASK_RESULT, .rm = BUSTER_TIER0_ROLE_MASK_SCRATCH(2), .mode = BUSTER_X86_REGISTER_DIRECT };
                    changed.record_count += 1;
                    break;
                case 10: first->immediate = shift ? 256 : 1; break;
                default:
                    // A result of another class.
                    changed.result_class = test ? BUSTER_TYPED_CLASS_VECTOR : BUSTER_TYPED_CLASS_U64;
                    break;
                }
                IrSimdTypedOperation mutated;
                memset(&mutated, 0x3c, sizeof(mutated));
                const IrSimdTypedStatus refused = applies ? ir_simd_typed_row(&changed, avx512, &mutated) : IR_SIMD_TYPED_UNSUPPORTED;
                snprintf(description, sizeof(description), "%s: its row changed one way (%u) isn't the front end's operation", rule->name, m);
                ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &mutated), description);
                // The mask decoder itself refuses it (or the target does, for a form of a width it lacks).
                const IrSimdTypedStatus own = applies ? ir_simd_typed_mask_row(&changed, avx512, &mutated) : IR_SIMD_TYPED_UNSUPPORTED;
                const IrSimdTypedStatus own_test = applies ? ir_simd_typed_mask_test(&changed, avx512, &mutated) : IR_SIMD_TYPED_UNSUPPORTED;
                ir_test_expect(tally, (test ? own_test : own) != IR_SIMD_TYPED_OK, description);
            }
        }
    }
    for (uint32_t f = 0; f < 5; f += 1)
    {
        ir_test_expect(tally, families[f] != 0, "every opmask family's rules decode");
    }
    printf("  IR SIMD rows: %u of the front end's opmask rows decode, KBINARY %u, KSHIFT %u, KUNARY %u, KMOV %u, KTEST %u\n", rows, families[0], families[1], families[2], families[3],
           families[4]);
}

typedef struct IrSimdTypedTestIntrinsic IrSimdTypedTestIntrinsic;
struct IrSimdTypedTestIntrinsic
{
    const char* name;
    uint8_t intrinsic;
};

// The front end's scalar BMI rows (#141): every GENERAL rule, pdep, pext, bzhi and bextr of unsigned ints and longs, at a
// target of every feature. Each decodes to its intrinsic (written out here by the rule's name, not the decoder's table),
// its source the builtin's first argument and its other operand the second, its features exactly
// x86_target_requirement's for its form (x86_target_rows.c's), and refuses as TARGET without it, the result untouched.
// Then each row changed one way at a time: a decoration, a displacement, an immediate, memory, a destination by number
// (not a role), the other width's form, a result of a signed class or the other width's, an operand of the other
// width's class, both registers one operand, a second record, and a third operand: each refused by the intrinsics'
// decoder, so at most the IR's machine row (ir_simd_typed_test_undecoded).
static void ir_simd_typed_test_intrinsics(IrTestTally* tally)
{
    const X86Target every = { .features = UINT64_MAX, .extensions = UINT64_MAX };
    static const IrSimdTypedTestIntrinsic expected_intrinsics[] = {
        { "__builtin_ia32_pdep_si", BUSTER_IR_INTRINSIC_PDEP },
        { "__builtin_ia32_pdep_di", BUSTER_IR_INTRINSIC_PDEP },
        { "__builtin_ia32_pext_si", BUSTER_IR_INTRINSIC_PEXT },
        { "__builtin_ia32_pext_di", BUSTER_IR_INTRINSIC_PEXT },
        { "__builtin_ia32_bzhi_si", BUSTER_IR_INTRINSIC_BZHI },
        { "__builtin_ia32_bzhi_di", BUSTER_IR_INTRINSIC_BZHI },
        { "__builtin_ia32_bextr_u32", BUSTER_IR_INTRINSIC_BEXTR },
        { "__builtin_ia32_bextr_u64", BUSTER_IR_INTRINSIC_BEXTR },
    };
    uint32_t rows = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t i = 0; tables && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        if (rule->family != CHECK_X86_LOWER_GENERAL)
        {
            continue;
        }
        char description[192];
        uint32_t intrinsic = BUSTER_IR_INTRINSIC_COUNT;
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(expected_intrinsics); k += 1)
        {
            intrinsic = strcmp(rule->name, expected_intrinsics[k].name) == 0 ? expected_intrinsics[k].intrinsic : intrinsic;
        }
        snprintf(description, sizeof(description), "%s: a GENERAL rule of the BMI builtins", rule->name);
        ir_test_expect(tally, intrinsic != BUSTER_IR_INTRINSIC_COUNT, description);
        Tier0Operation row;
        const char* reason = check_x86_lower_row_of(&every, rule, 0, 4, 0, false, &row);
        snprintf(description, sizeof(description), "%s: its row is the front end's at every feature", rule->name);
        ir_test_expect(tally, reason == NULL, description);
        if (reason != NULL || intrinsic == BUSTER_IR_INTRINSIC_COUNT)
        {
            continue;
        }
        rows += 1;
        const uint64_t features = x86_target_requirement(row.records[0].form) & (BUSTER_X86_FEATURE(BMI) | BUSTER_X86_FEATURE(BMI2));
        IrSimdTypedOperation decoded;
        memset(&decoded, 0xa5, sizeof(decoded));
        const IrSimdTypedStatus status = ir_simd_typed_row(&row, features, &decoded);
        snprintf(description, sizeof(description), "%s: the row decodes to its intrinsic of the builtin's arguments in order, its features its form's", rule->name);
        ir_test_expect(tally, status == IR_SIMD_TYPED_OK && decoded.intrinsic == intrinsic + 1 && decoded.intrinsic_operands[0] == 0 && decoded.intrinsic_operands[1] == 1 &&
                                  decoded.mask_bytes == 0 && decoded.operand_count == 2 && decoded.required_features == features && features != 0,
                       description);
        IrSimdTypedOperation without;
        memset(&without, 0x5a, sizeof(without));
        const IrSimdTypedOperation before = without;
        const IrSimdTypedStatus refused = ir_simd_typed_row(&row, ~features, &without);
        snprintf(description, sizeof(description), "%s: refused as the target's without its feature, the result untouched", rule->name);
        ir_test_expect(tally, refused == IR_SIMD_TYPED_TARGET && memcmp(&without, &before, sizeof(without)) == 0, description);
        const bool wide = rule->bytes == 8;
        for (uint32_t m = 0; m < 11; m += 1)
        {
            Tier0Operation changed = row;
            X86Instruction* first = &changed.records[0];
            switch (m)
            {
            case 0: first->decorations = 1; break;
            case 1: first->displacement = 8; break;
            case 2: first->immediate = 1; break;
            case 3: first->mode = BUSTER_X86_MEMORY_BASE; break;
            case 4: first->reg = X86_REGISTER_RAX; break;
            case 5:
            {
                // The other width's form of the same instruction: its other GENERAL rule's.
                for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); k += 1)
                {
                    const CheckX86LowerRule* other = check_x86_lower_rules + k;
                    const bool same = other->family == CHECK_X86_LOWER_GENERAL && other->bytes != rule->bytes && strncmp(other->name, rule->name, strlen(rule->name) - 2) == 0;
                    first->form = same ? other->form : first->form;
                }
            } break;
            case 6: changed.result_class = wide ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_I32; break;
            case 7: changed.result_class = wide ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64; break;
            case 8: changed.operand_classes[1] = wide ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64; break;
            case 9: first->rm = first->vvvv; break;
            default:
                changed.records[1] = changed.records[0];
                changed.record_count = 2;
                break;
            }
            IrSimdTypedOperation mutated;
            memset(&mutated, 0x3c, sizeof(mutated));
            const IrSimdTypedStatus result = ir_simd_typed_row(&changed, UINT64_MAX, &mutated);
            snprintf(description, sizeof(description), "%s: its row changed one way (%u) isn't the front end's operation", rule->name, m);
            ir_test_expect(tally, ir_simd_typed_test_undecoded(result, &mutated) && ir_simd_typed_intrinsic(&changed, UINT64_MAX, &mutated) != IR_SIMD_TYPED_OK, description);
        }
        Tier0Operation third = row;
        third.operand_count = 3;
        third.operand_classes[2] = row.operand_classes[0];
        IrSimdTypedOperation mutated;
        snprintf(description, sizeof(description), "%s: a row of a third operand isn't the front end's operation", rule->name);
        const IrSimdTypedStatus third_status = ir_simd_typed_row(&third, UINT64_MAX, &mutated);
        ir_test_expect(tally, ir_simd_typed_test_undecoded(third_status, &mutated) && ir_simd_typed_intrinsic(&third, UINT64_MAX, &mutated) != IR_SIMD_TYPED_OK, description);
    }
    ir_test_expect(tally, rows == BUSTER_ARRAY_LENGTH(expected_intrinsics), "every BMI builtin's rule decodes");
    printf("  IR SIMD rows: %u of the front end's scalar BMI rows decode to intrinsics\n", rows);
}

// The machine rows (#141, ir_simd_typed_machine_row): rows the IR's own operations don't take decode as the row itself,
// with their operands' classes and bytes and their features: the union of their forms' (x86_target_requirement's) and
// of their vectors' and masks' moves' (AVX512F, VL below 64 bytes, DQ for a byte mask, BW for a doubleword or a
// quadword one), and nothing else: a byte addition's of two vectors, a masked store's of an address, a vector and a
// mask of no result, and a shift's of a count from a general register. Without each feature, TARGET with the result
// untouched. An SSE carrier's (#1280: a vector of 8 or 4 bytes) row decodes with VL's moves. A row of a class no row
// call takes (a vector of 2 or 12 bytes, a long double, a 128-bit integer) is UNSUPPORTED.
static void ir_simd_typed_test_machine(IrTestTally* tally)
{
    const uint64_t avx512 = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ);
    Tier0Operation rows[3];
    memset(rows, 0, sizeof(rows));
    rows[0].records[0] = (X86Instruction){ .form = X86_FORM_VPADDB_Z_Z_ZM512, .reg = BUSTER_TIER0_ROLE_VECTOR(0), .vvvv = BUSTER_TIER0_ROLE_VECTOR(0), .rm = BUSTER_TIER0_ROLE_VECTOR(1), .mode = BUSTER_X86_REGISTER_DIRECT };
    rows[0].record_count = 1;
    rows[0].operand_count = 2;
    rows[0].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    rows[0].operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
    rows[0].operand_bytes[0] = 64;
    rows[0].operand_bytes[1] = 64;
    rows[0].result_class = BUSTER_TYPED_CLASS_VECTOR;
    rows[0].result_bytes = 64;
    rows[1].records[0] = (X86Instruction){ .form = X86_FORM_VMOVDQU8_XM128_X, .reg = BUSTER_TIER0_ROLE_VECTOR(1), .rm = BUSTER_TIER0_ROLE_INTEGER(0), .mode = BUSTER_X86_MEMORY_BASE, .decorations = BUSTER_TIER0_DECORATION_MASK(2) };
    rows[1].record_count = 1;
    rows[1].operand_count = 3;
    rows[1].operand_classes[0] = BUSTER_TYPED_CLASS_U64;
    rows[1].operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
    rows[1].operand_classes[2] = BUSTER_TYPED_CLASS_MASK;
    rows[1].operand_bytes[1] = 16;
    rows[1].operand_bytes[2] = 2;
    rows[1].result_class = BUSTER_TYPED_CLASS_VOID;
    rows[2].records[0] = (X86Instruction){ .form = X86_FORM_VMOVD_X_RM32_EVEX, .reg = BUSTER_TIER0_ROLE_VECTOR(1), .rm = BUSTER_TIER0_ROLE_INTEGER(1), .mode = BUSTER_X86_REGISTER_DIRECT };
    rows[2].records[1] = (X86Instruction){ .form = X86_FORM_VPSRLQ_Y_Y_XM128_EVEX, .reg = BUSTER_TIER0_ROLE_VECTOR(0), .vvvv = BUSTER_TIER0_ROLE_VECTOR(0), .rm = BUSTER_TIER0_ROLE_VECTOR(1), .mode = BUSTER_X86_REGISTER_DIRECT };
    rows[2].record_count = 2;
    rows[2].operand_count = 2;
    rows[2].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    rows[2].operand_classes[1] = BUSTER_TYPED_CLASS_I32;
    rows[2].operand_bytes[0] = 32;
    rows[2].result_class = BUSTER_TYPED_CLASS_VECTOR;
    rows[2].result_bytes = 32;
    // Each row's features, written out: the forms' and the moves'.
    const uint64_t expected[3] = {
        BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW),
        BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512BW),
        BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL),
    };
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ) };
    char description[192];
    for (uint32_t r = 0; r < 3; r += 1)
    {
        IrSimdTypedOperation decoded;
        memset(&decoded, 0x5a, sizeof(decoded));
        const IrSimdTypedStatus status = ir_simd_typed_row(&rows[r], avx512, &decoded);
        snprintf(description, sizeof(description), "machine row %u decodes as the row itself, its classes and its features", r);
        ir_test_expect(tally, status == IR_SIMD_TYPED_OK && decoded.machine && decoded.operation == IR_SIMD_OPERATION_COUNT && decoded.mask_bytes == 0 && decoded.intrinsic == 0 &&
                              decoded.required_features == expected[r] && decoded.operand_count == rows[r].operand_count &&
                              memcmp(decoded.operand_classes, rows[r].operand_classes, rows[r].operand_count) == 0 && memcmp(decoded.operand_bytes, rows[r].operand_bytes, rows[r].operand_count) == 0,
                       description);
        for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
        {
            IrSimdTypedOperation without;
            memset(&without, 0x5a, sizeof(without));
            const IrSimdTypedOperation before = without;
            const bool needed = (expected[r] & features[f]) != 0;
            const IrSimdTypedStatus refused = ir_simd_typed_row(&rows[r], avx512 & ~features[f], &without);
            snprintf(description, sizeof(description), "machine row %u without feature %u is refused exactly where it needs it, the result untouched", r, f);
            ir_test_expect(tally, needed ? refused == IR_SIMD_TYPED_TARGET && memcmp(&without, &before, sizeof(without)) == 0 : refused == IR_SIMD_TYPED_OK, description);
        }
    }
    // An SSE carrier (#1280), an operand of 8 or 4 bytes or a result of 4, is a row call's (an f64 or f32): its moves
    // are a vector's below 64 bytes, VL's. A vector of 2 or 12 bytes is no width a row call takes.
    IrSimdTypedOperation decoded;
    static const uint8_t carriers[3][2] = { { 1, 8 }, { 1, 4 }, { 2, 4 } };
    for (uint32_t c = 0; c < 3; c += 1)
    {
        Tier0Operation narrow = rows[0];
        if (carriers[c][0] == 2)
        {
            narrow.result_bytes = carriers[c][1];
        }
        else
        {
            narrow.operand_bytes[carriers[c][0]] = carriers[c][1];
        }
        memset(&decoded, 0x5a, sizeof(decoded));
        const IrSimdTypedStatus status = ir_simd_typed_row(&narrow, avx512, &decoded);
        snprintf(description, sizeof(description), "a machine row of a %u-byte vector %s decodes, needing VL for its move", carriers[c][1], carriers[c][0] == 2 ? "result" : "operand");
        ir_test_expect(tally, status == IR_SIMD_TYPED_OK && decoded.machine && decoded.required_features == (expected[0] | BUSTER_X86_FEATURE(AVX512VL)) &&
                              ir_simd_typed_row(&narrow, avx512 & ~BUSTER_X86_FEATURE(AVX512VL), &decoded) == IR_SIMD_TYPED_TARGET,
                       description);
    }
    Tier0Operation narrow = rows[0];
    narrow.operand_bytes[1] = 2;
    ir_test_expect(tally, ir_simd_typed_row(&narrow, avx512, &decoded) == IR_SIMD_TYPED_UNSUPPORTED, "a row of a 2-byte vector is no row call's");
    narrow.operand_bytes[1] = 12;
    ir_test_expect(tally, ir_simd_typed_row(&narrow, avx512, &decoded) == IR_SIMD_TYPED_UNSUPPORTED, "a row of a 12-byte vector is no row call's");
    Tier0Operation wide = rows[2];
    wide.operand_classes[1] = BUSTER_TYPED_CLASS_I128;
    ir_test_expect(tally, ir_simd_typed_row(&wide, avx512, &decoded) == IR_SIMD_TYPED_UNSUPPORTED, "a row of a 128-bit integer is no row call's");
    wide.operand_classes[1] = BUSTER_TYPED_CLASS_F80;
    ir_test_expect(tally, ir_simd_typed_row(&wide, avx512, &decoded) == IR_SIMD_TYPED_UNSUPPORTED, "a row of a long double is no row call's");
}

static void ir_simd_typed_test(IrTestTally* tally)
{
    ir_simd_typed_test_machine(tally);
    TypeTestTable table = { 0 };
    const bool ready = type_test_table_open(&table, 256);
    ir_test_expect(tally, ready, "typed SIMD operation fixtures initialize a real C type table");
    const uint64_t target = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512BW);
    // The producer's target: the same features, as the lowering's X86Target has them.
    const X86Target producer = { .features = target };
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
                const char* reason = check_vector_lower_row(&table.table, &producer, false, operations[operation], type, BUSTER_TYPED_CLASS_VOID, &row);
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
                // The forms' requirements, written out from the Intel SDM rather than from the decoder: AVX512F for EVEX,
                // AVX512VL for its 128- and 256-bit forms, and AVX512DQ for VPMOVM2D. Without each feature the
                // operation must refuse, and without any other (BW, or VL at 512 bits) it must still decode.
                const uint64_t expected = BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) | (compare ? BUSTER_X86_FEATURE(AVX512DQ) : 0);
                const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512DQ), BUSTER_X86_FEATURE(AVX512BW) };
                for (uint32_t feature = 0; feature < BUSTER_ARRAY_LENGTH(features); feature += 1)
                {
                    IrSimdTypedOperation without = before;
                    const IrSimdTypedStatus status = ir_simd_typed_operation(&row, target & ~features[feature], &without);
                    const bool needed = (expected & features[feature]) != 0;
                    ir_test_expect(tally, needed ? status == IR_SIMD_TYPED_TARGET && memcmp(&without, &before, sizeof(without)) == 0 :
                                                   status == IR_SIMD_TYPED_OK && without.required_features == expected,
                                   "each operation's exact producer feature requirement is checked before publication");
                }
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
            const char* reason = check_vector_lower_row(&table.table, &producer, false, CHECK_OP_ADD, floating, BUSTER_TYPED_CLASS_VOID, &unsupported);
            IrSimdTypedOperation decoded = { 0 };
            ir_test_expect(tally, reason == NULL && ir_simd_typed_operation(&unsupported, target, &decoded) == IR_SIMD_TYPED_UNSUPPORTED,
                           "an otherwise valid floating producer operation remains explicitly outside the integer lane subset");
        }
    }
    // The typed row has no element type. Logical bit operations are identical for all integer element widths;
    // normalize those records as raw bits, while refusing arithmetic/comparison opcodes with different lanes.
    static const TypeId integer_elements[] = { BUSTER_TYPE_SCHAR, BUSTER_TYPE_UCHAR, BUSTER_TYPE_SHORT, BUSTER_TYPE_USHORT, BUSTER_TYPE_LLONG, BUSTER_TYPE_ULLONG };
    static const CheckOp integer_operations[] = { CHECK_OP_BIT_AND, CHECK_OP_BIT_OR, CHECK_OP_BIT_XOR, CHECK_OP_BIT_NOT, CHECK_OP_ADD, CHECK_OP_SUBTRACT, CHECK_OP_LESS };
    for (uint32_t width = 0; ready && width < 3; width += 1)
    {
        const TypeId normalized = type_vector(&table.table, BUSTER_TYPE_INT, 4u << width, false);
        for (uint32_t element = 0; element < sizeof(integer_elements) / sizeof(*integer_elements); element += 1)
        {
            const uint64_t element_bytes = type_size(&table.table, integer_elements[element]);
            const TypeId vector = type_vector(&table.table, integer_elements[element], (uint32_t)((16u << width) / element_bytes), false);
            for (uint32_t operation = 0; operation < sizeof(integer_operations) / sizeof(*integer_operations); operation += 1)
            {
                Tier0Operation row = { 0 };
                Tier0Operation canonical = { 0 };
                const char* reason = check_vector_lower_row(&table.table, &producer, false, integer_operations[operation], vector, BUSTER_TYPED_CLASS_VOID, &row);
                const char* normalized_reason = check_vector_lower_row(&table.table, &producer, false, integer_operations[operation], normalized, BUSTER_TYPED_CLASS_VOID, &canonical);
                IrSimdTypedOperation result = { 0 };
                const IrSimdTypedStatus status = ir_simd_typed_operation(&row, target, &result);
                const bool bitwise = operation < 4;
                ir_test_expect(tally, reason == NULL && normalized_reason == NULL && (bitwise ?
                               status == IR_SIMD_TYPED_OK && result.shape == (IrSimdShape)(IR_SIMD_V4I32 + width) && !result.expand_mask &&
                               memcmp(&row, &canonical, sizeof(row)) == 0 : status == IR_SIMD_TYPED_UNSUPPORTED),
                               "representation-independent integer bitwise rows normalize exactly; other integer lane arithmetic/comparisons refuse");
            }
        }
    }
    IrSimdTypedOperation decoded = { 0 };
    ir_test_expect(tally, ir_simd_typed_operation(NULL, target, &decoded) == IR_SIMD_TYPED_INVALID && ir_simd_typed_row(NULL, target, &decoded) == IR_SIMD_TYPED_INVALID,
                   "missing typed operation input is rejected before indexing a record");
    ir_simd_typed_test_masks(tally);
    ir_simd_typed_test_intrinsics(tally);
    type_test_table_close(&table);
}
