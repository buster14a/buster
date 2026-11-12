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

// The bit builtins' rows (#1374, check_lower_bits_row), written out here: tzcnt, bsf, lzcnt, bsr and popcnt of each
// integer class of 32 and 64 bits into an int's result (and of the operand's own class), and a byte swap's move and bswap
// of its class, each decoding to its intrinsic of its operand, its features exactly x86_target_requirement's for its form
// (none for bsf, bsr and bswap), refused as TARGET without them, the result untouched. Then each row changed one way: a
// decoration, a displacement, an immediate, memory, a destination by number, the other width's form, the result's class
// 8 bytes over an operand of 4, a second operand, a second record of a count, a byte swap's move of the other width, and
// its swap of the operand's register: each not the bits' decoder's, so at most the IR's machine row.
static void ir_simd_typed_test_bits(IrTestTally* tally)
{
    static const struct
    {
        uint16_t forms[2];
        uint8_t intrinsic;
        const char* name;
    } counts[] = {
        { { X86_FORM_TZCNT_R32_RM32, X86_FORM_TZCNT_R64_RM64 }, BUSTER_IR_INTRINSIC_TZCNT, "tzcnt" },
        { { X86_FORM_BSF_R32_RM32, X86_FORM_BSF_R64_RM64 }, BUSTER_IR_INTRINSIC_BSF, "bsf" },
        { { X86_FORM_LZCNT_R32_RM32, X86_FORM_LZCNT_R64_RM64 }, BUSTER_IR_INTRINSIC_LZCNT, "lzcnt" },
        { { X86_FORM_BSR_R32_RM32, X86_FORM_BSR_R64_RM64 }, BUSTER_IR_INTRINSIC_BSR, "bsr" },
        { { X86_FORM_POPCNT_R32_RM32, X86_FORM_POPCNT_R64_RM64 }, BUSTER_IR_INTRINSIC_POPCNT, "popcnt" },
        { { X86_FORM_BSWAP_R32, X86_FORM_BSWAP_R64 }, BUSTER_IR_INTRINSIC_BSWAP, "bswap" },
    };
    static const uint8_t classes[] = { BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_U32, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_U64 };
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    uint32_t rows = 0;
    for (uint32_t k = 0; tables && k < BUSTER_ARRAY_LENGTH(counts); k += 1)
    {
        const bool swap = counts[k].intrinsic == BUSTER_IR_INTRINSIC_BSWAP;
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(classes); c += 1)
        {
            for (uint32_t own = 0; own < 2; own += 1)
            {
                const uint8_t operand = classes[c];
                const bool wide = tier0_class_bytes(operand) == 8;
                const uint8_t result = swap || own != 0 ? operand : BUSTER_TYPED_CLASS_I32;
                Tier0Operation row;
                memset(&row, 0, sizeof(row));
                const uint8_t r = BUSTER_TIER0_ROLE_INTEGER_RESULT;
                const uint8_t i0 = BUSTER_TIER0_ROLE_INTEGER(0);
                if (swap)
                {
                    row.records[0] = (X86Instruction){ .form = wide ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32, .reg = r, .rm = i0, .mode = BUSTER_X86_REGISTER_DIRECT };
                    row.records[1] = (X86Instruction){ .form = counts[k].forms[wide], .rm = r, .mode = BUSTER_X86_REGISTER_DIRECT };
                    row.record_count = 2;
                }
                else
                {
                    row.records[0] = (X86Instruction){ .form = counts[k].forms[wide], .reg = r, .rm = i0, .mode = BUSTER_X86_REGISTER_DIRECT };
                    row.record_count = 1;
                }
                row.operand_count = 1;
                row.operand_classes[0] = operand;
                row.result_class = result;
                char description[192];
                snprintf(description, sizeof(description), "%s of a %u-byte class into class %u: a row Tier 0 takes", counts[k].name, tier0_class_bytes(operand), result);
                ir_test_expect(tally, tier0_operation_valid(&row), description);
                const uint64_t features = x86_target_requirement(counts[k].forms[wide]) & ~BUSTER_X86_TARGET_KNOWN;
                IrSimdTypedOperation decoded;
                memset(&decoded, 0xa5, sizeof(decoded));
                const IrSimdTypedStatus status = ir_simd_typed_row(&row, features, &decoded);
                snprintf(description, sizeof(description), "%s of a %u-byte class into class %u: its intrinsic of its operand, its features its form's", counts[k].name,
                         tier0_class_bytes(operand), result);
                ir_test_expect(tally, status == IR_SIMD_TYPED_OK && decoded.intrinsic == counts[k].intrinsic + 1 && decoded.operand_count == 1 &&
                                          decoded.operand_classes[0] == operand && decoded.required_features == features && decoded.mask_bytes == 0 && !decoded.machine,
                               description);
                rows += status == IR_SIMD_TYPED_OK;
                if (features != 0)
                {
                    IrSimdTypedOperation without;
                    memset(&without, 0x5a, sizeof(without));
                    const IrSimdTypedOperation before = without;
                    const IrSimdTypedStatus refused = ir_simd_typed_row(&row, ~features, &without);
                    snprintf(description, sizeof(description), "%s: refused as the target's without its feature, the result untouched", counts[k].name);
                    ir_test_expect(tally, refused == IR_SIMD_TYPED_TARGET && memcmp(&without, &before, sizeof(without)) == 0, description);
                }
                for (uint32_t m = 0; m < 10; m += 1)
                {
                    Tier0Operation changed = row;
                    X86Instruction* last = &changed.records[changed.record_count - 1];
                    switch (m)
                    {
                    case 0: last->decorations = 1; break;
                    case 1: last->displacement = 8; break;
                    case 2: last->immediate = 1; break;
                    case 3: last->mode = BUSTER_X86_MEMORY_BASE; break;
                    case 4: last->rm = X86_REGISTER_RAX; break;
                    case 5: last->form = counts[k].forms[!wide]; break;
                    case 6:
                        changed.result_class = wide ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U64;
                        break;
                    case 7:
                        changed.operand_count = 2;
                        changed.operand_classes[1] = operand;
                        break;
                    case 8:
                        changed.records[changed.record_count] = changed.records[0];
                        changed.record_count += 1;
                        break;
                    default: changed.records[0].form = changed.records[0].form == X86_FORM_MOV_R64_RM64 ? X86_FORM_MOV_R32_RM32 : changed.records[0].form == X86_FORM_MOV_R32_RM32 ? X86_FORM_MOV_R64_RM64 : X86_FORM_NONE; break;
                    }
                    IrSimdTypedOperation mutated;
                    memset(&mutated, 0x3c, sizeof(mutated));
                    // A 64-bit row's count narrowed to an int is the bits' (6 is no change there for a count).
                    const bool same = m == 6 && wide && !swap;
                    const IrSimdTypedStatus mutated_status = ir_simd_typed_bits(&changed, UINT64_MAX, &mutated);
                    snprintf(description, sizeof(description), "%s of a %u-byte class: its row changed one way (%u) isn't the bits' row", counts[k].name, tier0_class_bytes(operand), m);
                    ir_test_expect(tally, same ? mutated_status == IR_SIMD_TYPED_OK : mutated_status == IR_SIMD_TYPED_UNSUPPORTED, description);
                    if (!same)
                    {
                        const IrSimdTypedStatus row_status = ir_simd_typed_row(&changed, UINT64_MAX, &mutated);
                        ir_test_expect(tally, ir_simd_typed_test_undecoded(row_status, &mutated), description);
                    }
                }
            }
        }
    }
    ir_test_expect(tally, rows == BUSTER_ARRAY_LENGTH(counts) * BUSTER_ARRAY_LENGTH(classes) * 2, "every bit builtin's row decodes");
    printf("  IR SIMD rows: %u of the bit builtins' rows decode to intrinsics\n", rows);
}

// The signed overflow builtins' rows (#1385, check_lower_overflow_row's): add, imul and cmp with seto, of two ints or two
// longs into an int, each a row Tier 0 takes, decoded at every target (their forms are baseline) as its intrinsic of its
// operands, in their order. Then each row changed one way: a decoration, a displacement, an immediate, memory, a register
// by number, the other width's operation, another condition's setcc, a result other than an int, unsigned operands,
// a third operand, the operands swapped in the operation, the extension of the result into itself and the setcc's
// record dropped: each not the overflows' decoder's, so at most the IR's machine row.
static void ir_simd_typed_test_overflows(IrTestTally* tally)
{
    static const struct
    {
        uint16_t forms[2];
        uint8_t intrinsic;
        const char* name;
    } operations[] = {
        { { X86_FORM_ADD_R32_RM32, X86_FORM_ADD_R64_RM64 }, BUSTER_IR_INTRINSIC_SADDO, "saddo" },
        { { X86_FORM_CMP_R32_RM32, X86_FORM_CMP_R64_RM64 }, BUSTER_IR_INTRINSIC_SSUBO, "ssubo" },
        { { X86_FORM_IMUL_R32_RM32, X86_FORM_IMUL_R64_RM64 }, BUSTER_IR_INTRINSIC_SMULO, "smulo" },
    };
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    const uint8_t direct = BUSTER_X86_REGISTER_DIRECT;
    const uint8_t r = BUSTER_TIER0_ROLE_INTEGER_RESULT;
    const uint8_t i0 = BUSTER_TIER0_ROLE_INTEGER(0);
    const uint8_t i1 = BUSTER_TIER0_ROLE_INTEGER(1);
    const uint8_t s0 = BUSTER_TIER0_ROLE_SCRATCH(0);
    uint32_t rows = 0;
    for (uint32_t k = 0; tables && k < BUSTER_ARRAY_LENGTH(operations); k += 1)
    {
        for (uint32_t wide = 0; wide < 2; wide += 1)
        {
            const uint8_t operand = wide != 0 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_I32;
            const uint32_t count = 3;
            Tier0Operation row;
            memset(&row, 0, sizeof(row));
            row.records[0] = (X86Instruction){ .form = operations[k].forms[wide], .reg = i0, .rm = i1, .mode = direct };
            row.records[1] = (X86Instruction){ .form = X86_FORM_SETO_RM8, .rm = s0, .mode = direct };
            row.records[2] = (X86Instruction){ .form = X86_FORM_MOVZX_R32_RM8, .reg = r, .rm = s0, .mode = direct };
            row.record_count = (uint8_t)count;
            row.operand_count = 2;
            row.operand_classes[0] = operand;
            row.operand_classes[1] = operand;
            row.result_class = BUSTER_TYPED_CLASS_I32;
            char description[192];
            snprintf(description, sizeof(description), "%s of %u-byte operands: a row Tier 0 takes", operations[k].name, tier0_class_bytes(operand));
            ir_test_expect(tally, tier0_operation_valid(&row), description);
            IrSimdTypedOperation decoded;
            memset(&decoded, 0xa5, sizeof(decoded));
            const IrSimdTypedStatus status = ir_simd_typed_row(&row, 0, &decoded);
            snprintf(description, sizeof(description), "%s of %u-byte operands: its intrinsic of its operands at the baseline", operations[k].name, tier0_class_bytes(operand));
            ir_test_expect(tally, status == IR_SIMD_TYPED_OK && decoded.intrinsic == operations[k].intrinsic + 1 && decoded.operand_count == 2 &&
                                      decoded.operand_classes[0] == operand && decoded.operand_classes[1] == operand && decoded.intrinsic_operands[0] == 0 &&
                                      decoded.intrinsic_operands[1] == 1 && decoded.required_features == 0 && decoded.mask_bytes == 0 && !decoded.machine,
                           description);
            rows += status == IR_SIMD_TYPED_OK;
            for (uint32_t m = 0; m < 13; m += 1)
            {
                Tier0Operation changed = row;
                X86Instruction* operation = &changed.records[0];
                switch (m)
                {
                case 0: operation->decorations = 1; break;
                case 1: operation->displacement = 8; break;
                case 2: operation->immediate = 1; break;
                case 3: operation->mode = BUSTER_X86_MEMORY_BASE; break;
                case 4: operation->rm = X86_REGISTER_RCX; break;
                case 5: operation->form = operations[k].forms[!wide]; break;
                case 6: changed.records[count - 2].form = X86_FORM_SETB_RM8; break;
                case 7: changed.result_class = wide != 0 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_U32; break;
                case 8:
                    changed.operand_classes[0] = (uint8_t)(operand + 1);
                    changed.operand_classes[1] = (uint8_t)(operand + 1);
                    break;
                case 9:
                    changed.operand_count = 3;
                    changed.operand_classes[2] = operand;
                    break;
                case 10:
                    operation->reg = i1;
                    operation->rm = i0;
                    break;
                case 11: changed.records[count - 1].rm = r; break;
                default:
                    changed.records[count - 2] = changed.records[count - 1];
                    changed.record_count -= 1;
                    break;
                }
                IrSimdTypedOperation mutated;
                memset(&mutated, 0x3c, sizeof(mutated));
                const IrSimdTypedStatus mutated_status = ir_simd_typed_overflow(&changed, &mutated);
                snprintf(description, sizeof(description), "%s of %u-byte operands: its row changed one way (%u) isn't an overflow's row", operations[k].name,
                         tier0_class_bytes(operand), m);
                ir_test_expect(tally, mutated_status == IR_SIMD_TYPED_UNSUPPORTED, description);
                const IrSimdTypedStatus row_status = ir_simd_typed_row(&changed, UINT64_MAX, &mutated);
                ir_test_expect(tally, ir_simd_typed_test_undecoded(row_status, &mutated), description);
            }
        }
    }
    ir_test_expect(tally, rows == BUSTER_ARRAY_LENGTH(operations) * 2, "every signed overflow builtin's row decodes");
    printf("  IR SIMD rows: %u of the signed overflow builtins' rows decode to intrinsics\n", rows);
}

// The machine rows (#141, ir_simd_typed_machine_row): rows the IR's own operations don't take decode as the row itself,
// with their operands' classes and bytes and their features: the union of their forms' (x86_target_requirement's) and
// of their vectors' and masks' moves' (AVX512F, VL below 64 bytes, DQ for a byte mask, BW for a doubleword or a
// quadword one), and nothing else: a byte average's of two vectors (the IR has no average), a compress-store's
// (vpcompressd to memory) of an address, a vector and a mask of no result (the masked stores are the IR's own,
// ir_simd_typed_test_masked_memory), and a shift's of a 64-bit count from a general register (the builtins' int counts
// are the IR's shift, ir_simd_typed_test_shifts). Without each feature, TARGET with the result
// untouched. An SSE carrier's (#1280: a vector of 8 or 4 bytes) row decodes with VL's moves. At a target without
// AVX512VL (#1281), a 16- or 32-byte vector's moves are AVX's VEX ones: AVX2's saturating byte addition of two 32-byte vectors
// takes AVX and AVX2 there, and nothing of AVX-512. A row of a class no row call takes (a vector of 2 or 12 bytes, a
// long double, a 128-bit integer) is UNSUPPORTED.
static void ir_simd_typed_test_machine(IrTestTally* tally)
{
    const uint64_t avx512 = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ);
    Tier0Operation rows[4];
    memset(rows, 0, sizeof(rows));
    rows[0].records[0] = (X86Instruction){ .form = X86_FORM_VPAVGB_Z_Z_ZM512, .reg = BUSTER_TIER0_ROLE_VECTOR(0), .vvvv = BUSTER_TIER0_ROLE_VECTOR(0), .rm = BUSTER_TIER0_ROLE_VECTOR(1), .mode = BUSTER_X86_REGISTER_DIRECT };
    rows[0].record_count = 1;
    rows[0].operand_count = 2;
    rows[0].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    rows[0].operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
    rows[0].operand_bytes[0] = 64;
    rows[0].operand_bytes[1] = 64;
    rows[0].result_class = BUSTER_TYPED_CLASS_VECTOR;
    rows[0].result_bytes = 64;
    rows[1].records[0] = (X86Instruction){ .form = X86_FORM_VPCOMPRESSD_XM128_X, .reg = BUSTER_TIER0_ROLE_VECTOR(1), .rm = BUSTER_TIER0_ROLE_INTEGER(0), .mode = BUSTER_X86_MEMORY_BASE, .decorations = BUSTER_TIER0_DECORATION_MASK(2) };
    rows[1].record_count = 1;
    rows[1].operand_count = 3;
    rows[1].operand_classes[0] = BUSTER_TYPED_CLASS_U64;
    rows[1].operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
    rows[1].operand_classes[2] = BUSTER_TYPED_CLASS_MASK;
    rows[1].operand_bytes[1] = 16;
    rows[1].operand_bytes[2] = 2;
    rows[1].result_class = BUSTER_TYPED_CLASS_VOID;
    rows[2].records[0] = (X86Instruction){ .form = X86_FORM_VMOVQ_X_RM64_EVEX, .reg = BUSTER_TIER0_ROLE_VECTOR(1), .rm = BUSTER_TIER0_ROLE_INTEGER(1), .mode = BUSTER_X86_REGISTER_DIRECT };
    rows[2].records[1] = (X86Instruction){ .form = X86_FORM_VPSRLQ_Y_Y_XM128_EVEX, .reg = BUSTER_TIER0_ROLE_VECTOR(0), .vvvv = BUSTER_TIER0_ROLE_VECTOR(0), .rm = BUSTER_TIER0_ROLE_VECTOR(1), .mode = BUSTER_X86_REGISTER_DIRECT };
    rows[2].record_count = 2;
    rows[2].operand_count = 2;
    rows[2].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    rows[2].operand_classes[1] = BUSTER_TYPED_CLASS_I64;
    rows[2].operand_bytes[0] = 32;
    rows[2].result_class = BUSTER_TYPED_CLASS_VECTOR;
    rows[2].result_bytes = 32;
    rows[3].records[0] = (X86Instruction){ .form = X86_FORM_VPADDUSB_Y_Y_YM256, .reg = BUSTER_TIER0_ROLE_VECTOR(0), .vvvv = BUSTER_TIER0_ROLE_VECTOR(0), .rm = BUSTER_TIER0_ROLE_VECTOR(1), .mode = BUSTER_X86_REGISTER_DIRECT };
    rows[3].record_count = 1;
    rows[3].operand_count = 2;
    rows[3].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    rows[3].operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
    rows[3].operand_bytes[0] = 32;
    rows[3].operand_bytes[1] = 32;
    rows[3].result_class = BUSTER_TYPED_CLASS_VECTOR;
    rows[3].result_bytes = 32;
    // Each row's features, written out: the forms' and the moves'.
    const uint64_t expected[4] = {
        BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW),
        BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL),
        BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL),
        BUSTER_X86_FEATURE(AVX) | BUSTER_X86_FEATURE(AVX2),
    };
    // The last row's target: AVX's and AVX2's, no AVX-512.
    const uint64_t targets[4] = { avx512, avx512, avx512, BUSTER_X86_FEATURE(AVX) | BUSTER_X86_FEATURE(AVX2) };
    static const uint64_t features[] = {
        BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ), BUSTER_X86_FEATURE(AVX), BUSTER_X86_FEATURE(AVX2),
    };
    char description[192];
    for (uint32_t r = 0; r < 4; r += 1)
    {
        IrSimdTypedOperation decoded;
        memset(&decoded, 0x5a, sizeof(decoded));
        const IrSimdTypedStatus status = ir_simd_typed_row(&rows[r], targets[r], &decoded);
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
            const IrSimdTypedStatus refused = ir_simd_typed_row(&rows[r], targets[r] & ~features[f], &without);
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

// #1623: the front end's comparisons into masks, every COMPARE rule's row (c_check_program_x86.c's, moved into Tier 0's
// mask class by check_x86_lower_masks) at every immediate clang takes (an integer compare's 8, a float compare's 32), with
// float compares' predicates as LLVM selects them and strictly (constrained, every bit and SAE kept). An integer row
// decodes as the IR's vcmp of its lanes for vpcmp's ==, <, <=, !=, >= and > (signed, or unsigned for vpcmpu), and a
// packed float's for EQ_OQ, LT_OS, LE_OS, NEQ_UQ, GE_OS and GT_OS (C's operators: clang's codes 0, 1, 2, 4, 13 and
// 14, and as LLVM selects them their quiet and signaling twins 16 to 30, and GE and GT as LE and LT of the operands
// swapped), each written out here; its mask the lanes' (a byte for 8 or fewer), ANDed with the writemask; its features
// written out (AVX512F, VL below 64 bytes, BW for bytes' and words' compares and for 32 or 64 lanes, DQ for 8 lanes or
// fewer), and without each it stays a machine row. Every other immediate, a scalar's or FP16's compare, SAE's form,
// and each row changed one way (decorations, displacement, memory, registers, the result's mask of another width, an
// operand of another width or class, a record too many) stays the machine row: none is the IR's comparison. Swapping
// the two vectors' registers gives the reversed predicate (a < b as b > a), written out too.
static void ir_simd_typed_test_compare_masks(IrTestTally* tally)
{
    const X86Target every = { .features = UINT64_MAX, .extensions = UINT64_MAX };
    const uint64_t avx512 = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ);
    const uint8_t none = IR_SIMD_PREDICATE_COUNT;
    // vpcmp's immediates, signed and unsigned.
    static const uint8_t integers[2][8] = {
        { IR_SIMD_EQ, IR_SIMD_LT, IR_SIMD_LE, IR_SIMD_PREDICATE_COUNT, IR_SIMD_NE, IR_SIMD_GE, IR_SIMD_GT, IR_SIMD_PREDICATE_COUNT },
        { IR_SIMD_EQ, IR_SIMD_ULT, IR_SIMD_ULE, IR_SIMD_PREDICATE_COUNT, IR_SIMD_NE, IR_SIMD_UGE, IR_SIMD_UGT, IR_SIMD_PREDICATE_COUNT },
    };
    // The reversed predicates, for the operands swapped.
    static const uint8_t reversed[IR_SIMD_PREDICATE_COUNT] = {
        [IR_SIMD_EQ] = IR_SIMD_EQ,
        [IR_SIMD_NE] = IR_SIMD_NE,
        [IR_SIMD_LT] = IR_SIMD_GT,
        [IR_SIMD_LE] = IR_SIMD_GE,
        [IR_SIMD_GT] = IR_SIMD_LT,
        [IR_SIMD_GE] = IR_SIMD_LE,
        [IR_SIMD_ULT] = IR_SIMD_UGT,
        [IR_SIMD_ULE] = IR_SIMD_UGE,
        [IR_SIMD_UGT] = IR_SIMD_ULT,
        [IR_SIMD_UGE] = IR_SIMD_ULE,
    };
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ) };
    uint32_t decoded_rows = 0;
    uint32_t machine_rows = 0;
    uint32_t rules = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t i = 0; tables && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const bool floating = rule->form != X86_FORM_NONE;
        const uint32_t codes = floating ? 32 : 8;
        rules += rule->family == CHECK_X86_LOWER_COMPARE ? 1 : 0;
        for (uint32_t strict = 0; rule->family == CHECK_X86_LOWER_COMPARE && strict < (floating ? 2u : 1u); strict += 1)
        {
            for (uint32_t code = 0; code < codes; code += 1)
            {
                Tier0Operation row;
                char description[224];
                const bool made = check_x86_lower_row_of(&every, rule, code, 4, 0, strict != 0, &row) == NULL && check_x86_lower_masks(&row, &every);
                snprintf(description, sizeof(description), "%s (code %u, strict %u): its row of masks is the front end's", rule->name, code, strict);
                ir_test_expect(tally, made, description);
                if (!made)
                {
                    continue;
                }
                // The expected comparison, written out: a packed float compare's codes by C's operators, as LLVM selects
                // them (the low 4 bits) or strictly (all 5).
                const uint32_t float_code = strict != 0 ? code : code & 15;
                const uint8_t float_predicate = float_code == 0 ? IR_SIMD_EQ : float_code == 1 ? IR_SIMD_LT : float_code == 2 ? IR_SIMD_LE :
                                                float_code == 4 ? IR_SIMD_NE : float_code == 13 ? IR_SIMD_GE : float_code == 14 ? IR_SIMD_GT : none;
                const bool packed_float = floating && rule->fixed == CHECK_X86_PREDICATE_FCMP && (rule->element == 4 || rule->element == 8);
                const uint8_t predicate = !floating ? integers[rule->is_signed ? 0 : 1][code] : packed_float ? float_predicate : none;
                const uint8_t lane = floating ? (rule->element == 4 ? IR_SIMD_LANE_F32 : IR_SIMD_LANE_F64) :
                                     rule->element == 1 ? IR_SIMD_LANE_I8 : rule->element == 2 ? IR_SIMD_LANE_I16 : rule->element == 4 ? IR_SIMD_LANE_I32 : IR_SIMD_LANE_I64;
                const uint32_t lanes = rule->bytes / rule->element;
                const uint64_t expected = BUSTER_X86_FEATURE(AVX512F) | (rule->bytes < 64 ? BUSTER_X86_FEATURE(AVX512VL) : 0) |
                                          (!floating && rule->element <= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0) | (lanes >= 32 ? BUSTER_X86_FEATURE(AVX512BW) : 0) |
                                          (lanes <= 8 ? BUSTER_X86_FEATURE(AVX512DQ) : 0);
                IrSimdTypedOperation decoded;
                memset(&decoded, 0xa5, sizeof(decoded));
                const IrSimdTypedStatus status = ir_simd_typed_row(&row, avx512, &decoded);
                if (predicate == none)
                {
                    machine_rows += 1;
                    snprintf(description, sizeof(description), "%s (code %u, strict %u): no IR predicate, so it stays the machine row", rule->name, code, strict);
                    ir_test_expect(tally, ir_simd_typed_test_undecoded(status, &decoded), description);
                    continue;
                }
                decoded_rows += 1;
                snprintf(description, sizeof(description), "%s (code %u, strict %u): the IR's comparison of its lanes into its mask, ANDed with the writemask, at its features", rule->name, code,
                         strict);
                ir_test_expect(tally, status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.compare_mask && !decoded.expand_mask && decoded.operation == IR_SIMD_COMPARE &&
                                      decoded.lane == lane && decoded.predicate == predicate && decoded.mask_bytes == 0 && decoded.required_features == expected &&
                                      decoded.shape == (IrSimdShape)(IR_SIMD_V4I32 + (rule->bytes == 16 ? 0 : rule->bytes == 32 ? 1 : 2)) && decoded.operand_count == 3 &&
                                      decoded.operand_classes[2] == BUSTER_TYPED_CLASS_MASK && decoded.operand_bytes[2] == (lanes <= 8 ? 1 : lanes / 8),
                               description);
                for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
                {
                    IrSimdTypedOperation without;
                    memset(&without, 0x5a, sizeof(without));
                    const IrSimdTypedStatus refused = ir_simd_typed_row(&row, avx512 & ~features[f], &without);
                    const bool needed = (expected & features[f]) != 0;
                    snprintf(description, sizeof(description), "%s (code %u): without a feature it needs it stays the machine row, and needs no other", rule->name, code);
                    ir_test_expect(tally, needed ? ir_simd_typed_test_undecoded(refused, &without) : refused == IR_SIMD_TYPED_OK && without.compare_mask && without.predicate == predicate,
                                   description);
                }
                // The two vectors' registers swapped: the reversed predicate.
                Tier0Operation other = row;
                other.records[0].vvvv = row.records[0].rm;
                other.records[0].rm = row.records[0].vvvv;
                memset(&decoded, 0xa5, sizeof(decoded));
                snprintf(description, sizeof(description), "%s (code %u, strict %u): its operands swapped, the reversed predicate", rule->name, code, strict);
                ir_test_expect(tally, ir_simd_typed_row(&other, avx512, &decoded) == IR_SIMD_TYPED_OK && decoded.compare_mask && decoded.predicate == reversed[predicate] &&
                                      decoded.lane == lane,
                               description);
                // The strict form under SAE, and the row changed one way at a time.
                for (uint32_t m = 0; m < 14; m += 1)
                {
                    Tier0Operation changed = row;
                    X86Instruction* record = &changed.records[0];
                    bool applies = true;
                    switch (m)
                    {
                    case 0: record->decorations = 0; break;
                    case 1: record->decorations = BUSTER_TIER0_DECORATION_MASK(3); break;
                    case 2: record->displacement = 8; break;
                    case 3: record->mode = BUSTER_X86_MEMORY_BASE; break;
                    case 4: record->reg = BUSTER_TIER0_ROLE_VECTOR(0); break;
                    case 5: record->rm = record->vvvv; break;
                    case 6: record->immediate = 3; break;
                    case 7: record->immediate = 7; break;
                    case 8:
                        changed.result_bytes = changed.result_bytes == 8 ? 4 : (uint8_t)(changed.result_bytes * 2);
                        changed.operand_bytes[2] = changed.result_bytes;
                        break;
                    case 9: changed.operand_bytes[1] = rule->bytes == 64 ? 32 : 64; break;
                    case 10: changed.operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR; break;
                    case 11:
                        changed.records[1] = changed.records[0];
                        changed.record_count = 2;
                        break;
                    case 12: changed.operand_count = 2; break;
                    case 13:
                        applies = floating && strict != 0 && rule->rounding == CHECK_X86_ROUND_SAE;
                        applies = applies && check_x86_lower_row_of(&every, rule, code, 8, 0, true, &changed) == NULL && check_x86_lower_masks(&changed, &every);
                        break;
                    }
                    if (applies)
                    {
                        memset(&decoded, 0xa5, sizeof(decoded));
                        const IrSimdTypedStatus refused = ir_simd_typed_row(&changed, avx512, &decoded);
                        snprintf(description, sizeof(description), "%s (code %u, change %u): not the IR's comparison", rule->name, code, m);
                        ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &decoded), description);
                    }
                }
            }
        }
    }
    // 24 integer rules (bytes to quadwords, signed and unsigned, at 3 widths) at 6 of 8 immediates, and 6 packed float
    // rules at 12 codes of 32 as LLVM selects them and 6 strictly.
    ir_test_expect(tally, rules >= 30 && decoded_rows == 24 * 6 + 6 * (12 + 6), "every comparison into a mask an IR predicate is decodes, and no other");
    ir_test_expect(tally, machine_rows != 0, "the comparisons no IR predicate is stay machine rows");
    printf("  IR SIMD rows: %u of the front end's %u comparison rows into masks decode to the IR's comparisons, %u stay machine rows\n", decoded_rows, decoded_rows + machine_rows,
           machine_rows);
}

static void ir_simd_typed_test_splats(IrTestTally* tally)
{
    static const TypeId elements[] = {
        BUSTER_TYPE_SCHAR,
        BUSTER_TYPE_UCHAR,
        BUSTER_TYPE_SHORT,
        BUSTER_TYPE_USHORT,
        BUSTER_TYPE_INT,
        BUSTER_TYPE_UINT,
        BUSTER_TYPE_LLONG,
        BUSTER_TYPE_ULLONG,
        BUSTER_TYPE_FLOAT,
        BUSTER_TYPE_DOUBLE,
    };
    static const uint8_t classes[] = {
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U32,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_F32,
        BUSTER_TYPED_CLASS_F64,
    };
    const X86Target target = { .features = UINT64_MAX, .extensions = UINT64_MAX };
    TypeTestTable table = {0};
    const bool ready = type_test_table_open(&table, 256);
    ir_test_expect(tally, ready, "splat decoder producer type table");
    for (uint32_t width = 0; ready && width < 3; width += 1)
    {
        for (uint32_t element = 0; element < BUSTER_ARRAY_LENGTH(elements); element += 1)
        {
            const uint32_t bytes = (uint32_t)type_size(&table.table, elements[element]);
            const TypeId type = type_vector(&table.table, elements[element], (16u << width) / bytes, false);
            Tier0Operation row = {0};
            const char* reason = check_vector_lower_row(&table.table, &target, false, BUSTER_CHECK_VECTOR_SPLAT, type, classes[element], &row);
            IrSimdTypedOperation decoded = {0};
            const IrSimdTypedStatus status = ir_simd_typed_row(&row, target.features, &decoded);
            const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) | (bytes < 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
            ir_test_expect(tally, reason == NULL && status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.splat_bytes == bytes &&
                                  decoded.operand_count == 1 && decoded.operand_classes[0] == classes[element] && decoded.required_features == required,
                           "every scalar splat producer becomes semantic IR at each width with its exact features");
            if (reason == NULL && status == IR_SIMD_TYPED_OK)
            {
                const IrSimdTypedOperation before = decoded;
                for (uint64_t features = required; features != 0; features &= features - 1)
                {
                    const uint64_t missing = features & (UINT64_C(0) - features);
                    const IrSimdTypedStatus refused = ir_simd_typed_row(&row, target.features & ~missing, &decoded);
                    ir_test_expect(tally, refused == IR_SIMD_TYPED_TARGET && memcmp(&decoded, &before, sizeof(decoded)) == 0,
                                   "a splat missing any required target feature refuses before publication");
                }
                for (uint32_t mutation = 0; mutation < 11; mutation += 1)
                {
                    Tier0Operation changed = row;
                    switch (mutation)
                    {
                        case 0: changed.records[0].decorations = 1; break;
                        case 1: changed.records[0].displacement = 8; break;
                        case 2: changed.records[0].mode = BUSTER_X86_MEMORY_BASE; break;
                        case 3: changed.records[0].reg ^= 1; break;
                        case 4: changed.records[0].rm ^= 1; break;
                        case 5: changed.records[0].vvvv = 1; break;
                        case 6: changed.records[0].immediate = 1; break;
                        case 7: changed.operand_count = 2; break;
                        case 8: changed.operand_classes[0] = BUSTER_TYPED_CLASS_VOID; break;
                        case 9: changed.result_bytes = row.result_bytes == 16 ? 32 : 16; break;
                        case 10: changed.operand_classes[0] = BUSTER_TYPED_CLASS_U8; break;
                    }
                    const IrSimdTypedStatus refused = ir_simd_typed_splat(&changed, target.features, &decoded);
                    ir_test_expect(tally, refused == IR_SIMD_TYPED_UNSUPPORTED && memcmp(&decoded, &before, sizeof(decoded)) == 0,
                                   "changed splat roles, widths, decorations and effects are not silently discarded");
                }
            }
        }
    }
    type_test_table_close(&table);
}

// The width changes (#1840): every BINARY_IMMEDIATE rule of a vinsert form and every EXTRACT_LANE rule's row as the front
// end makes it (an extract's in the mask class) at every immediate below 8 decodes as the IR's insert or masked extract of
// the immediate's part, its features exactly AVX512F's and VL's and its forms' (DQ for 32x8 and 64x2, and an extract's
// byte mask's), refused without each and under changes; and the casts' rows of no record, of one operand or two of one
// width, decode as an extract of part 0 or a widen at every pair of widths and at no other row.
static void ir_simd_typed_test_widths(IrTestTally* tally)
{
    const X86Target every = { .features = UINT64_MAX, .extensions = UINT64_MAX };
    const uint64_t avx512 = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ);
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ) };
    uint32_t inserts = 0;
    uint32_t extracts = 0;
    uint32_t casts = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t i = 0; tables && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const char* mnemonic = rule->form != X86_FORM_NONE ? x86_form_description(rule->form)->mnemonic : "";
        const bool insert = rule->family == CHECK_X86_LOWER_BINARY_IMMEDIATE && rule->other != 0 && strncmp(mnemonic, "vinsert", 7) == 0 && strcmp(mnemonic, "vinsertps") != 0;
        const bool extract = rule->family == CHECK_X86_LOWER_EXTRACT_LANE;
        for (uint32_t code = 0; (insert || extract) && code < 8; code += 1)
        {
            Tier0Operation row;
            char description[224];
            const bool made = check_x86_lower_row_of(&every, rule, code, 4, 0, false, &row) == NULL && (!extract || check_x86_lower_masks(&row, &every));
            snprintf(description, sizeof(description), "%s (code %u): its row is the front end's", rule->name, code);
            ir_test_expect(tally, made, description);
            if (!made)
            {
                continue;
            }
            const uint32_t parts = (uint32_t)rule->bytes / rule->other;
            const uint32_t lanes = (uint32_t)rule->other / rule->element;
            const bool dq = strstr(mnemonic, "32x8") != 0 || strstr(mnemonic, "64x2") != 0;
            const uint64_t expected = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | (dq || (extract && lanes <= 8) ? BUSTER_X86_FEATURE(AVX512DQ) : 0);
            IrSimdTypedOperation decoded;
            memset(&decoded, 0xa5, sizeof(decoded));
            const IrSimdTypedStatus status = ir_simd_typed_row(&row, avx512, &decoded);
            snprintf(description, sizeof(description), "%s (code %u): the IR's %s of its part at exactly its features", rule->name, code, insert ? "insert" : "masked extract");
            ir_test_expect(tally, status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.operation == IR_SIMD_OPERATION_COUNT && decoded.part == code % parts &&
                                      decoded.width_change == (insert ? BUSTER_IR_INTRINSIC_INSERT : BUSTER_IR_INTRINSIC_EXTRACT) + 1 && decoded.merge_mask == extract &&
                                      (!extract || decoded.lane == (rule->element == 4 ? IR_SIMD_LANE_I32 : IR_SIMD_LANE_I64)) && decoded.required_features == expected,
                           description);
            inserts += insert;
            extracts += extract;
            for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
            {
                IrSimdTypedOperation without;
                memset(&without, 0x5a, sizeof(without));
                const IrSimdTypedStatus refused = ir_simd_typed_row(&row, avx512 & ~features[f], &without);
                const bool needed = (expected & features[f]) != 0;
                snprintf(description, sizeof(description), "%s (code %u): without a feature it needs it stays the machine row, and needs no other", rule->name, code);
                ir_test_expect(tally, needed ? ir_simd_typed_test_undecoded(refused, &without) : refused == IR_SIMD_TYPED_OK && without.width_change != 0, description);
            }
            for (uint32_t m = 0; m < 9; m += 1)
            {
                Tier0Operation changed = row;
                X86Instruction* first = &changed.records[0];
                bool applies = true;
                switch (m)
                {
                case 0: first->decorations = extract ? 0 : BUSTER_TIER0_DECORATION_MASK(0); break;
                case 1: first->immediate = (int32_t)parts; break;
                case 2: first->mode = BUSTER_X86_MEMORY_BASE; break;
                case 3: first->rm = BUSTER_TIER0_ROLE_VECTOR(2); break;
                case 4: first->reg = BUSTER_TIER0_ROLE_VECTOR(1); break;
                case 5: changed.operand_bytes[1] = rule->other == 16 ? 32 : 16; break;
                case 6:
                    applies = extract;
                    changed.records[1].rm = BUSTER_TIER0_ROLE_VECTOR(0);
                    break;
                case 7:
                    changed.records[changed.record_count] = changed.records[changed.record_count - 1];
                    changed.record_count += 1;
                    break;
                case 8: first->vvvv = insert ? BUSTER_TIER0_ROLE_VECTOR(1) : BUSTER_TIER0_ROLE_VECTOR(0); break;
                }
                if (applies)
                {
                    memset(&decoded, 0xa5, sizeof(decoded));
                    const IrSimdTypedStatus refused = ir_simd_typed_row(&changed, avx512, &decoded);
                    snprintf(description, sizeof(description), "%s (code %u, change %u): not the IR's width change", rule->name, code, m);
                    ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &decoded), description);
                }
            }
        }
    }
    // The casts' rows of no record (c_check_program_vector_builtins.c's shuffles that move no element).
    for (uint32_t result = 0; result < 3; result += 1)
    {
        for (uint32_t operand = 0; operand < 3; operand += 1)
        {
            for (uint32_t kept = 0; kept < 2; kept += 1)
            {
                Tier0Operation row = {
                    .result_class = BUSTER_TYPED_CLASS_VECTOR,
                    .result_bytes = (uint8_t)(16u << result),
                    .operand_count = (uint8_t)(1 + kept),
                };
                row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
                row.operand_bytes[0] = (uint8_t)(16u << operand);
                row.operand_classes[1] = kept != 0 ? BUSTER_TYPED_CLASS_VECTOR : 0;
                row.operand_bytes[1] = kept != 0 ? (uint8_t)(16u << operand) : 0;
                IrSimdTypedOperation decoded;
                memset(&decoded, 0xa5, sizeof(decoded));
                const IrSimdTypedStatus status = ir_simd_typed_row(&row, avx512, &decoded);
                char description[160];
                snprintf(description, sizeof(description), "a cast of %u bytes to %u (%u operands): %s", 16u << operand, 16u << result, 1 + kept,
                         result == operand ? "the machine row" : result < operand ? "the IR's extract of part 0" : "the IR's widen");
                const uint8_t kind = result < operand ? BUSTER_IR_INTRINSIC_EXTRACT + 1 : BUSTER_IR_INTRINSIC_WIDEN + 1;
                ir_test_expect(tally, result == operand ? ir_simd_typed_test_undecoded(status, &decoded) :
                                                         status == IR_SIMD_TYPED_OK && decoded.width_change == kind && decoded.part == 0 && !decoded.merge_mask &&
                                                             decoded.required_features == (BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL)),
                               description);
                casts += status == IR_SIMD_TYPED_OK && decoded.width_change != 0;
                // Changed: a record, an operand of another width or class, a target without VL.
                Tier0Operation changed = row;
                changed.records[0] = (X86Instruction){ .form = X86_FORM_VMOVDQU64_Z_ZM512, .reg = BUSTER_TIER0_ROLE_VECTOR(0), .rm = BUSTER_TIER0_ROLE_VECTOR(0), .mode = BUSTER_X86_REGISTER_DIRECT };
                changed.record_count = 1;
                ir_test_expect(tally, ir_simd_typed_test_undecoded(ir_simd_typed_row(&changed, avx512, &decoded), &decoded), "a cast's row with a record is no width change");
                changed = row;
                changed.operand_classes[0] = BUSTER_TYPED_CLASS_U64;
                changed.operand_bytes[0] = 0;
                ir_test_expect(tally, ir_simd_typed_test_undecoded(ir_simd_typed_row(&changed, avx512, &decoded), &decoded), "a cast's row of an integer is no width change");
                if (kept != 0)
                {
                    changed = row;
                    changed.operand_bytes[1] = (uint8_t)(16u << ((operand + 1) % 3));
                    ir_test_expect(tally, ir_simd_typed_test_undecoded(ir_simd_typed_row(&changed, avx512, &decoded), &decoded), "a cast's kept operand has its first's width");
                }
                ir_test_expect(tally, ir_simd_typed_test_undecoded(ir_simd_typed_row(&row, avx512 & ~BUSTER_X86_FEATURE(AVX512VL), &decoded), &decoded), "a cast needs AVX512VL");
            }
        }
    }
    // Inserts: 4 forms of 16 bytes into 32 (vinserti32x4's and vinsertf32x4's, the vinsertf128 builtins' 3 too,
    // vinserti64x2's, vinsertf64x2's) and the 512-bit ones, at 8 immediates.
    ir_test_expect(tally, inserts != 0 && inserts % 8 == 0 && extracts != 0 && extracts % 8 == 0 && casts == 2 * 6, "every insert, masked extract and cast decodes");
    printf("  IR SIMD rows: %u insert rows and %u masked extract rows of the front end at 8 immediates, and %u casts' rows, decode to the IR's width changes\n", inserts, extracts, casts);
}

// A front-end rule's permute (#1840), written out from its form's mnemonic: its kind past BUSTER_IR_INTRINSIC_PERMUTE
// (0 vpermb to vpermq, 1 vpshufb, 2 valignd and valignq, 3 vshufi32x4 and vshufi64x2), or 4 for any other rule; and its
// flags at immediate `code`: the elements' bytes' log2, 0, that log2 in bits 4 and 5 and the code's count below the
// lanes, and the code below 4 of a 32-byte vector's or any byte of a 64-byte one's.
static uint32_t ir_simd_typed_test_permute_kind(const CheckX86LowerRule* rule, uint32_t code, uint32_t* flags)
{
    const char* mnemonic = rule->form != X86_FORM_NONE ? x86_form_description(rule->form)->mnemonic : "";
    const bool binary = rule->family == CHECK_X86_LOWER_BINARY && rule->other == 0;
    const bool immediate = rule->family == CHECK_X86_LOWER_BINARY_IMMEDIATE && rule->other == 0;
    const uint32_t size = rule->element == 1 ? 0 : rule->element == 2 ? 1 : rule->element == 4 ? 2 : 3;
    const bool permute = binary && rule->swap && (strcmp(mnemonic, "vpermb") == 0 || strcmp(mnemonic, "vpermw") == 0 ||
                                                  (strcmp(mnemonic, "vpermd") == 0 && rule->bytes >= 32) || (strcmp(mnemonic, "vpermq") == 0 && rule->bytes == 64));
    const bool shuffle = binary && !rule->swap && strcmp(mnemonic, "vpshufb") == 0;
    const bool align = immediate && (strcmp(mnemonic, "valignd") == 0 || strcmp(mnemonic, "valignq") == 0);
    const bool lanes = immediate && rule->bytes >= 32 && (strcmp(mnemonic, "vshufi32x4") == 0 || strcmp(mnemonic, "vshufi64x2") == 0);
    const uint32_t kind = permute ? 0 : shuffle ? 1 : align ? 2 : lanes ? 3 : 4;
    *flags = kind == 0 ? size : kind == 2 ? (size << 4) | (code % (rule->bytes >> size)) : kind == 3 ? (rule->bytes == 32 ? code & 3 : code & 255) : 0;
    return kind;
}

// The permutes (#1840): every front-end rule of vpermb, vpermw, vpermd and vpermq (the variable permutes, their indices
// in vvvv), vpshufb, valignd, valignq, vshufi32x4 and vshufi64x2, at 8 immediates for those that take one, decodes as the
// IR's permute of its kind and flags, written out, at exactly its form's features and its vectors' moves; refused without
// each and under 8 changes.
static void ir_simd_typed_test_permutes(IrTestTally* tally)
{
    const X86Target every = { .features = UINT64_MAX, .extensions = UINT64_MAX };
    const uint64_t all = UINT64_MAX;
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512VBMI) };
    uint32_t decoded_rows[4] = { 0 };
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t i = 0; tables && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        uint32_t flags = 0;
        const uint32_t kind = ir_simd_typed_test_permute_kind(rule, 0, &flags);
        const uint32_t codes = kind >= 2 ? 8 : 1;
        for (uint32_t code = 0; kind < 4 && code < codes; code += 1)
        {
            ir_simd_typed_test_permute_kind(rule, code, &flags);
            Tier0Operation row;
            char description[224];
            const bool made = check_x86_lower_row_of(&every, rule, code, 4, 0, false, &row) == NULL;
            snprintf(description, sizeof(description), "%s (code %u): its row is the front end's", rule->name, code);
            ir_test_expect(tally, made, description);
            if (!made)
            {
                continue;
            }
            const uint64_t expected = BUSTER_X86_FEATURE(AVX512F) | (rule->bytes < 64 ? BUSTER_X86_FEATURE(AVX512VL) : 0) |
                                      ((kind == 0 && rule->element <= 2) || kind == 1 ? BUSTER_X86_FEATURE(AVX512BW) : 0) |
                                      (kind == 0 && rule->element == 1 ? BUSTER_X86_FEATURE(AVX512VBMI) : 0);
            IrSimdTypedOperation decoded;
            memset(&decoded, 0xa5, sizeof(decoded));
            const IrSimdTypedStatus status = ir_simd_typed_row(&row, all, &decoded);
            snprintf(description, sizeof(description), "%s (code %u): the IR's permute of kind %u and flags 0x%x at exactly its features", rule->name, code, kind, flags);
            ir_test_expect(tally, status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.permute == BUSTER_IR_INTRINSIC_PERMUTE + kind + 1 && decoded.immediate == flags &&
                                      decoded.width_change == 0 && decoded.operation == IR_SIMD_OPERATION_COUNT && (decoded.required_features & ~BUSTER_X86_FEATURE(AVX)) == expected,
                           description);
            decoded_rows[kind] += status == IR_SIMD_TYPED_OK && decoded.permute != 0;
            for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
            {
                IrSimdTypedOperation without;
                memset(&without, 0x5a, sizeof(without));
                const IrSimdTypedStatus refused = ir_simd_typed_row(&row, all & ~features[f], &without);
                const bool needed = (expected & features[f]) != 0;
                snprintf(description, sizeof(description), "%s (code %u): without a feature it needs it stays the machine row, and needs no other", rule->name, code);
                ir_test_expect(tally, needed ? ir_simd_typed_test_undecoded(refused, &without) || refused == IR_SIMD_TYPED_OK : refused == IR_SIMD_TYPED_OK && without.permute != 0,
                               description);
                if (needed && refused == IR_SIMD_TYPED_OK)
                {
                    ir_test_expect(tally, without.permute == 0, description);
                }
            }
            for (uint32_t m = 0; m < 8; m += 1)
            {
                Tier0Operation changed = row;
                X86Instruction* first = &changed.records[0];
                switch (m)
                {
                case 0: first->decorations = BUSTER_TIER0_DECORATION_MASK(0); break;
                case 1: first->mode = BUSTER_X86_MEMORY_BASE; break;
                case 2: first->rm = first->vvvv; break;
                case 3: first->reg = BUSTER_TIER0_ROLE_VECTOR(1); break;
                case 4: changed.operand_bytes[1] = rule->bytes == 64 ? 32 : 64; break;
                case 5:
                    changed.records[1] = changed.records[0];
                    changed.record_count = 2;
                    break;
                case 6: first->displacement = 4; break;
                case 7: first->immediate = kind < 2 ? 1 : 256; break;
                }
                memset(&decoded, 0xa5, sizeof(decoded));
                const IrSimdTypedStatus refused = ir_simd_typed_row(&changed, all, &decoded);
                snprintf(description, sizeof(description), "%s (code %u, change %u): not the IR's permute", rule->name, code, m);
                ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &decoded) || (refused == IR_SIMD_TYPED_OK && decoded.permute == 0 && !decoded.machine && m == 4), description);
            }
        }
    }
    // vpermb and vpermw at 3 widths, vpermd at 2 and vpermq at 1; vpshufb at 3; valignd and valignq at 3 widths, and the
    // two shuffles of lanes at 2, at 8 immediates.
    ir_test_expect(tally, decoded_rows[0] == 9 && decoded_rows[1] == 3 && decoded_rows[2] == 6 * 8 && decoded_rows[3] == 4 * 8, "every permute rule decodes");
    printf("  IR SIMD rows: the front end's permutes decode to the IR's: %u variable permutes, %u byte shuffles, %u aligns and %u shuffles of lanes\n", decoded_rows[0],
           decoded_rows[1], decoded_rows[2], decoded_rows[3]);
}

// The shifts by a scalar count (#1840): every front-end rule of the shifts by an int (CHECK_X86_LOWER_SHIFT:
// __builtin_ia32_psllwi512 to __builtin_ia32_psraqi128, 27 of them) decodes as the IR's scalar shift of its lanes and
// shift, written out from the builtin's name, at exactly its forms' features and its vectors' moves; refused without
// each and under 8 changes.
static void ir_simd_typed_test_shifts(IrTestTally* tally)
{
    const X86Target every = { .features = UINT64_MAX, .extensions = UINT64_MAX };
    const uint64_t all = UINT64_MAX;
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ) };
    uint32_t decoded_rows = 0;
    uint32_t rules = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t i = 0; tables && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const char* name = rule->name;
        const size_t prefix = sizeof("__builtin_ia32_ps") - 1;
        if (rule->family != CHECK_X86_LOWER_SHIFT || strncmp(name, "__builtin_ia32_ps", prefix) != 0 || strlen(name) < prefix + 3)
        {
            continue;
        }
        rules += 1;
        // The name's shift (ll, rl, ra) and lanes (w, d, q).
        const uint32_t kind = name[prefix] == 'l' ? 0 : name[prefix + 1] == 'l' ? 1 : 2;
        const char lane = name[prefix + 2];
        const uint32_t lanes = lane == 'w' ? 1 : lane == 'd' ? 2 : 3;
        const uint32_t flags = lanes | kind << 2;
        Tier0Operation row;
        char description[224];
        const bool made = check_x86_lower_row_of(&every, rule, 0, 4, 0, false, &row) == NULL;
        snprintf(description, sizeof(description), "%s: its row is the front end's", name);
        ir_test_expect(tally, made, description);
        if (!made)
        {
            continue;
        }
        const uint64_t expected = BUSTER_X86_FEATURE(AVX512F) | (rule->bytes < 64 ? BUSTER_X86_FEATURE(AVX512VL) : 0) | (lanes == 1 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
        IrSimdTypedOperation decoded;
        memset(&decoded, 0xa5, sizeof(decoded));
        const IrSimdTypedStatus status = ir_simd_typed_row(&row, all, &decoded);
        snprintf(description, sizeof(description), "%s: the IR's shift of flags 0x%x at exactly its features", name, flags);
        ir_test_expect(tally, status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.shift && decoded.immediate == flags && decoded.permute == 0 && decoded.convert == 0 &&
                                  decoded.operation == IR_SIMD_OPERATION_COUNT && decoded.operand_count == 2 && (decoded.required_features & ~BUSTER_X86_FEATURE(AVX)) == expected,
                       description);
        decoded_rows += status == IR_SIMD_TYPED_OK && decoded.shift;
        for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
        {
            IrSimdTypedOperation without;
            memset(&without, 0x5a, sizeof(without));
            const IrSimdTypedStatus refused = ir_simd_typed_row(&row, all & ~features[f], &without);
            const bool needed = (expected & features[f]) != 0;
            snprintf(description, sizeof(description), "%s: without a feature it needs it stays the machine row, and needs no other", name);
            ir_test_expect(tally, needed ? ir_simd_typed_test_undecoded(refused, &without) : refused == IR_SIMD_TYPED_OK && without.shift, description);
        }
        for (uint32_t m = 0; m < 8; m += 1)
        {
            Tier0Operation changed = row;
            X86Instruction* shift = &changed.records[1];
            switch (m)
            {
            case 0: shift->decorations = BUSTER_TIER0_DECORATION_MASK(0); break;
            case 1: shift->mode = BUSTER_X86_MEMORY_BASE; break;
            case 2: shift->rm = shift->vvvv; break;
            case 3: changed.records[0].rm = BUSTER_TIER0_ROLE_INTEGER(0); break;
            case 4: changed.operand_classes[1] = BUSTER_TYPED_CLASS_I64; break;
            case 5: changed.record_count = 1; break;
            case 6: shift->immediate = 1; break;
            case 7: changed.records[0].form = X86_FORM_VMOVQ_X_RM64_EVEX; break;
            }
            memset(&decoded, 0xa5, sizeof(decoded));
            const IrSimdTypedStatus refused = ir_simd_typed_row(&changed, all, &decoded);
            snprintf(description, sizeof(description), "%s (change %u): not the IR's shift", name, m);
            ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &decoded), description);
        }
    }
    // Shifts left and both right of words, doublewords and quadwords at three widths.
    ir_test_expect(tally, rules == 27 && decoded_rows == 27, "every shift rule decodes");
    printf("  IR SIMD rows: the front end's %u shifts by an int decode to the IR's shift\n", decoded_rows);
}

// The conversions (#1840): every __builtin_convertvector row between integer vectors of other element sizes, by the real
// producer (check_vector_convert_row) at every pair of sizes and signedness, every result width and the operand widths it
// takes, decodes as the IR's extend or truncate of its lanes' sizes and sign, written out, at exactly its form's features
// and AVX512F's and VL's; and every TRUNCATE rule's row (the vpmov, vpmovs and vpmovus builtins under a mask) as the IR's
// truncate merged under the mask where its lanes fill its result, and a machine row where they don't. Each refused
// without each feature it needs and under changes.
static void ir_simd_typed_test_conversions(IrTestTally* tally)
{
    static const TypeId elements[2][4] = {
        { BUSTER_TYPE_UCHAR, BUSTER_TYPE_USHORT, BUSTER_TYPE_UINT, BUSTER_TYPE_ULLONG },
        { BUSTER_TYPE_SCHAR, BUSTER_TYPE_SHORT, BUSTER_TYPE_INT, BUSTER_TYPE_LLONG },
    };
    const X86Target every = { .features = UINT64_MAX, .extensions = UINT64_MAX };
    const uint64_t all = UINT64_MAX;
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ) };
    uint32_t extends = 0;
    uint32_t truncates = 0;
    uint32_t masked = 0;
    uint32_t partial = 0;
    TypeTestTable table = {0};
    const bool ready = type_test_table_open(&table, 512) && x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, ready, "conversion decoder producer type table");
    for (uint32_t sign = 0; ready && sign < 2; sign += 1)
    {
        for (uint32_t from = 0; from < 4; from += 1)
        {
            for (uint32_t to = 0; to < 4; to += 1)
            {
                for (uint32_t width = 0; from != to && width < 3; width += 1)
                {
                    // The result's width, and the operand's: the source's lanes' bytes, or a wider operand's where those are fewer than 16.
                    const uint32_t bytes = 16u << width;
                    const uint32_t lanes = bytes >> to;
                    const uint32_t source_bytes = lanes << from;
                    if (source_bytes > 64)
                    {
                        continue;
                    }
                    for (uint32_t operand = source_bytes < 16 ? 16 : source_bytes; operand <= 64; operand *= 2)
                    {
                        const TypeId source = type_vector(&table.table, elements[sign][from], lanes, false);
                        const TypeId result = type_vector(&table.table, elements[sign][to], lanes, false);
                        Tier0Operation row;
                        const char* reason = check_vector_convert_row(&table.table, &every, source, operand, result, &row);
                        char description[224];
                        snprintf(description, sizeof(description), "convertvector of %u lanes of %u bytes (signed %u) to %u bytes, operand %u: ", lanes, 1u << from, sign,
                                 1u << to, operand);
                        if (reason != NULL)
                        {
                            // The producer refuses a truncation of a wider operand's first lanes.
                            ir_test_expect(tally, to < from && operand != source_bytes, description);
                            continue;
                        }
                        const bool extend = to > from;
                        // Written out: the form's features (BW for bytes to words, either way), AVX512F's and VL's.
                        const uint64_t expected = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | ((from == 0 && to == 1) || (from == 1 && to == 0) ? BUSTER_X86_FEATURE(AVX512BW) : 0);
                        const uint32_t flags = from | to << 2 | (extend ? sign << 4 : 0);
                        IrSimdTypedOperation decoded;
                        memset(&decoded, 0xa5, sizeof(decoded));
                        const IrSimdTypedStatus status = ir_simd_typed_row(&row, all, &decoded);
                        ir_test_expect(tally, status == IR_SIMD_TYPED_OK && decoded.convert == (extend ? BUSTER_IR_INTRINSIC_EXTEND : BUSTER_IR_INTRINSIC_TRUNCATE) + 1 &&
                                                  decoded.immediate == flags && !decoded.merge_mask && !decoded.machine && decoded.required_features == expected,
                                       description);
                        extends += extend && status == IR_SIMD_TYPED_OK;
                        truncates += !extend && status == IR_SIMD_TYPED_OK;
                        for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
                        {
                            IrSimdTypedOperation without;
                            memset(&without, 0x5a, sizeof(without));
                            const IrSimdTypedStatus refused = ir_simd_typed_row(&row, all & ~features[f], &without);
                            ir_test_expect(tally, (expected & features[f]) != 0 ? ir_simd_typed_test_undecoded(refused, &without) : refused == IR_SIMD_TYPED_OK && without.convert != 0,
                                           "a conversion without a feature it needs stays the machine row, and needs no other");
                        }
                        for (uint32_t m = 0; m < 5; m += 1)
                        {
                            Tier0Operation changed = row;
                            switch (m)
                            {
                            case 0: changed.records[0].decorations = BUSTER_TIER0_DECORATION_MASK(0); break;
                            case 1: changed.records[0].mode = BUSTER_X86_MEMORY_BASE; break;
                            case 2: changed.records[0].rm = BUSTER_TIER0_ROLE_VECTOR(1); break;
                            case 3: changed.result_bytes = (uint8_t)(bytes == 64 ? 32 : bytes * 2); break;
                            case 4: changed.records[0].immediate = 1; break;
                            }
                            ir_test_expect(tally, ir_simd_typed_test_undecoded(ir_simd_typed_row(&changed, all, &decoded), &decoded), "a changed conversion isn't the IR's");
                        }
                    }
                }
            }
        }
    }
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        if (rule->family != CHECK_X86_LOWER_TRUNCATE)
        {
            continue;
        }
        Tier0Operation row;
        const bool made = check_x86_lower_row_of(&every, rule, 0, 4, 0, false, &row) == NULL && check_x86_lower_masks(&row, &every);
        ir_test_expect(tally, made, "a truncation's builtin's row in the mask class");
        const char* mnemonic = x86_form_description(rule->form)->mnemonic;
        const uint32_t mode = strncmp(mnemonic, "vpmovus", 7) == 0 ? 2 : strncmp(mnemonic, "vpmovs", 6) == 0 && mnemonic[6] != 'x' ? 1 : 0;
        const uint32_t to_bytes = mnemonic[strlen(mnemonic) - 1] == 'b' ? 1 : mnemonic[strlen(mnemonic) - 1] == 'w' ? 2 : 4;
        const uint32_t lanes = (uint32_t)rule->bytes / rule->element;
        const bool fills = lanes * to_bytes == rule->other;
        IrSimdTypedOperation decoded;
        memset(&decoded, 0xa5, sizeof(decoded));
        const IrSimdTypedStatus status = made ? ir_simd_typed_row(&row, all, &decoded) : IR_SIMD_TYPED_INVALID;
        char description[160];
        snprintf(description, sizeof(description), "%s: %s", rule->name, fills ? "the IR's truncate merged under its mask" : "a machine row, its lanes not filling its result");
        const uint32_t from = rule->element == 2 ? 1 : rule->element == 4 ? 2 : 3;
        const uint32_t to = to_bytes == 1 ? 0 : to_bytes == 2 ? 1 : 2;
        ir_test_expect(tally, fills ? status == IR_SIMD_TYPED_OK && decoded.convert == BUSTER_IR_INTRINSIC_TRUNCATE + 1 && decoded.merge_mask &&
                                          decoded.immediate == (from | to << 2 | mode << 4) && decoded.lane == (to == 0 ? IR_SIMD_LANE_I8 : to == 1 ? IR_SIMD_LANE_I16 : IR_SIMD_LANE_I32) :
                                      ir_simd_typed_test_undecoded(status, &decoded),
                       description);
        masked += fills && status == IR_SIMD_TYPED_OK;
        partial += !fills;
        if (fills)
        {
            Tier0Operation changed = row;
            changed.records[0].decorations = 0;
            ir_test_expect(tally, ir_simd_typed_test_undecoded(ir_simd_typed_row(&changed, all, &decoded), &decoded), "a masked truncation's row without its mask isn't the IR's");
            changed = row;
            changed.records[1].rm = BUSTER_TIER0_ROLE_VECTOR(0);
            ir_test_expect(tally, ir_simd_typed_test_undecoded(ir_simd_typed_row(&changed, all, &decoded), &decoded), "a masked truncation's row moving another register isn't the IR's");
        }
    }
    type_test_table_close(&table);
    ir_test_expect(tally, extends != 0 && truncates != 0 && masked != 0 && partial != 0, "the conversions decode, and partial masked truncations stay rows");
    printf("  IR SIMD rows: the front end's conversions decode to the IR's: %u extensions and %u truncations of convertvector, %u masked truncations (%u partial ones stay rows)\n",
           extends, truncates, masked, partial);
}

// The lane operations (#1840): clang's __builtin_elementwise_min, _max, _add_sat and _sub_sat of every integer element
// size, signedness and width the producer has a form for (check_vector_elementwise_forms, the row's one record of its
// final case: VECTOR(0) and VECTOR(1) into VECTOR(0)) decode as the IR's lane_op of their lanes and operation, written
// out, at exactly the form's features and the vectors' moves; refused without each and under 6 changes.
static void ir_simd_typed_test_lane_ops(IrTestTally* tally)
{
    static const CBuiltinId builtins[4] = { CBUILTIN_ELEMENTWISE_MIN, CBUILTIN_ELEMENTWISE_MAX, CBUILTIN_ELEMENTWISE_ADD_SAT, CBUILTIN_ELEMENTWISE_SUB_SAT };
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ) };
    const uint64_t all = UINT64_MAX;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    uint32_t decoded_rows = 0;
    uint32_t refused_forms = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t k = 0; tables && k < 4; k += 1)
    {
        for (uint32_t size = 0; size < 4; size += 1)
        {
            for (uint32_t sign = 0; sign < 2; sign += 1)
            {
                for (uint32_t width = 0; width < 3; width += 1)
                {
                    const uint32_t bytes = 16u << width;
                    const CheckVectorShape shape = { .lanes = bytes >> size, .bytes = bytes, .element = (uint8_t)(1u << size), .integer = true, .is_signed = sign != 0 };
                    uint64_t needed = 0;
                    const CheckVectorForms forms = check_vector_elementwise_forms(builtins[k], shape, &needed);
                    char description[224];
                    // The producer's forms: saturation of bytes and words alone.
                    snprintf(description, sizeof(description), "elementwise %u of %u-byte elements (signed %u) at %u bytes: a form where AVX-512 has one", k, 1u << size, sign, bytes);
                    ir_test_expect(tally, (forms.forms[width] != 0) == (k < 2 || size <= 1), description);
                    if (forms.forms[width] == 0)
                    {
                        refused_forms += 1;
                        continue;
                    }
                    Tier0Operation row;
                    memset(&row, 0, sizeof(row));
                    row.records[0] = check_vector_lower_record(forms.forms[width], v0, v0, v1);
                    row.record_count = 1;
                    row.operand_count = 2;
                    row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
                    row.operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
                    row.operand_bytes[0] = (uint8_t)bytes;
                    row.operand_bytes[1] = (uint8_t)bytes;
                    row.result_class = BUSTER_TYPED_CLASS_VECTOR;
                    row.result_bytes = (uint8_t)bytes;
                    // Written out: minimum 0 and 1, maximum 2 and 3, saturating sum 4 and 5 and difference 6 and 7, the signed one
                    // odd; AVX512F, VL below 64 bytes and BW for bytes and words.
                    const uint32_t flags = size | (2 * k + sign) << 2;
                    const uint64_t expected = BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) | (size <= 1 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
                    IrSimdTypedOperation decoded;
                    memset(&decoded, 0xa5, sizeof(decoded));
                    const IrSimdTypedStatus status = ir_simd_typed_row(&row, all, &decoded);
                    snprintf(description, sizeof(description), "elementwise %u of %u-byte elements (signed %u) at %u bytes: the IR's lane_op 0x%x at exactly its features", k,
                             1u << size, sign, bytes, flags);
                    ir_test_expect(tally, status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.lane_op && decoded.immediate == flags && !decoded.shift && decoded.permute == 0 &&
                                              decoded.operation == IR_SIMD_OPERATION_COUNT && decoded.operand_count == 2 && (decoded.required_features & ~BUSTER_X86_FEATURE(AVX)) == expected &&
                                              (needed & ~expected) == 0,
                                   description);
                    decoded_rows += status == IR_SIMD_TYPED_OK && decoded.lane_op;
                    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
                    {
                        IrSimdTypedOperation without;
                        memset(&without, 0x5a, sizeof(without));
                        const IrSimdTypedStatus refused = ir_simd_typed_row(&row, all & ~features[f], &without);
                        const bool required = (expected & features[f]) != 0;
                        ir_test_expect(tally, required ? ir_simd_typed_test_undecoded(refused, &without) : refused == IR_SIMD_TYPED_OK && without.lane_op,
                                       "a lane operation without a feature it needs stays the machine row, and needs no other");
                    }
                    for (uint32_t m = 0; m < 6; m += 1)
                    {
                        Tier0Operation changed = row;
                        X86Instruction* record = &changed.records[0];
                        switch (m)
                        {
                        case 0: record->decorations = BUSTER_TIER0_DECORATION_MASK(0); break;
                        case 1: record->mode = BUSTER_X86_MEMORY_BASE; break;
                        case 2: record->vvvv = v1; break;
                        case 3: changed.operand_bytes[1] = (uint8_t)(bytes == 64 ? 32 : 64); break;
                        case 4:
                            changed.records[1] = changed.records[0];
                            changed.record_count = 2;
                            break;
                        case 5: record->immediate = 1; break;
                        }
                        memset(&decoded, 0xa5, sizeof(decoded));
                        const IrSimdTypedStatus refused = ir_simd_typed_row(&changed, all, &decoded);
                        ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &decoded), "a changed lane operation's row is not the IR's lane_op");
                    }
                }
            }
        }
    }
    // Minima and maxima of 4 element sizes, 2 signednesses and 3 widths; saturations of 2 sizes.
    ir_test_expect(tally, decoded_rows == 2 * 4 * 2 * 3 + 2 * 2 * 2 * 3 && refused_forms == 2 * 2 * 2 * 3, "every elementwise minimum, maximum and saturation decodes");
    printf("  IR SIMD rows: the front end's %u elementwise minima, maxima and saturations decode to the IR's lane_op\n", decoded_rows);
}

// The permutes of two tables (#1840): vpermt2b to vpermt2q at each width, the indices in vvvv and the second table in rm
// (the permutex2var builtins' rows) or the other way around (__builtin_shufflevector's), decode as the IR's permute2 of
// their lanes, its operands in the order the row names them, at exactly the features written out; refused without each
// and under 6 changes.
static void ir_simd_typed_test_permute2s(IrTestTally* tally)
{
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512VBMI) };
    const uint64_t all = UINT64_MAX;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    uint32_t decoded_rows = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t size = 0; tables && size < 4; size += 1)
    {
        for (uint32_t swapped = 0; swapped < 2; swapped += 1)
        {
            for (uint32_t width = 0; width < 3; width += 1)
            {
                const uint32_t bytes = 16u << width;
                Tier0Operation row;
                memset(&row, 0, sizeof(row));
                row.records[0] = check_vector_lower_record(ir_simd_permute2_forms[size][width], v0, swapped != 0 ? v2 : v1, swapped != 0 ? v1 : v2);
                row.record_count = 1;
                row.operand_count = 3;
                for (uint32_t o = 0; o < 3; o += 1)
                {
                    row.operand_classes[o] = BUSTER_TYPED_CLASS_VECTOR;
                    row.operand_bytes[o] = (uint8_t)bytes;
                }
                row.result_class = BUSTER_TYPED_CLASS_VECTOR;
                row.result_bytes = (uint8_t)bytes;
                // Written out: AVX512F, VL below 64 bytes, BW for bytes and words and VBMI for bytes.
                const uint64_t expected = BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) | (size <= 1 ? BUSTER_X86_FEATURE(AVX512BW) : 0) |
                                          (size == 0 ? BUSTER_X86_FEATURE(AVX512VBMI) : 0);
                IrSimdTypedOperation decoded;
                memset(&decoded, 0xa5, sizeof(decoded));
                const IrSimdTypedStatus status = ir_simd_typed_row(&row, all, &decoded);
                char description[160];
                snprintf(description, sizeof(description), "vpermt2 of %u-byte lanes (swapped %u) at %u bytes: the IR's permute2 at exactly its features", 1u << size, swapped, bytes);
                ir_test_expect(tally, status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.permute2 && decoded.permute2_indices == (swapped != 0 ? 2 : 1) && decoded.permute2_table == (swapped != 0 ? 1 : 2) && decoded.immediate == size &&
                                          decoded.permute == 0 && !decoded.lane_op && !decoded.shift && decoded.operation == IR_SIMD_OPERATION_COUNT && decoded.operand_count == 3 &&
                                          (decoded.required_features & ~BUSTER_X86_FEATURE(AVX)) == expected,
                               description);
                decoded_rows += status == IR_SIMD_TYPED_OK && decoded.permute2;
                for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
                {
                    IrSimdTypedOperation without;
                    memset(&without, 0x5a, sizeof(without));
                    const IrSimdTypedStatus refused = ir_simd_typed_row(&row, all & ~features[f], &without);
                    const bool required = (expected & features[f]) != 0;
                    ir_test_expect(tally, required ? ir_simd_typed_test_undecoded(refused, &without) : refused == IR_SIMD_TYPED_OK && without.permute2,
                                   "a permute of two tables without a feature it needs stays the machine row, and needs no other");
                }
                for (uint32_t m = 0; m < 6; m += 1)
                {
                    Tier0Operation changed = row;
                    X86Instruction* record = &changed.records[0];
                    switch (m)
                    {
                    case 0: record->decorations = BUSTER_TIER0_DECORATION_MASK(0); break;
                    case 1: record->mode = BUSTER_X86_MEMORY_BASE; break;
                    case 2: record->vvvv = v0; break;
                    case 3: changed.operand_bytes[swapped != 0 ? 2 : 1] = (uint8_t)(bytes == 64 ? 32 : 64); break;
                    case 4:
                        changed.records[1] = changed.records[0];
                        changed.record_count = 2;
                        break;
                    case 5: record->reg = v1; break;
                    }
                    memset(&decoded, 0xa5, sizeof(decoded));
                    const IrSimdTypedStatus refused = ir_simd_typed_row(&changed, all, &decoded);
                    ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &decoded), "a changed permute of two tables' row is not the IR's permute2");
                }
            }
        }
    }
    // __builtin_shufflevector's of narrower vectors: one 16-byte table twice (the indices VECTOR(1), the table VECTOR(0)
    // again), and two tables of half the result's width (the indices VECTOR(2)), each table widened; a table wider than
    // the result, or indices narrower, stays a machine row.
    uint32_t narrow_rows = 0;
    for (uint32_t size = 0; tables && size < 4; size += 1)
    {
        for (uint32_t width = 1; width < 3; width += 1)
        {
            for (uint32_t shape = 0; shape < 4; shape += 1)
            {
                const uint32_t bytes = 16u << width;
                const bool one = shape == 0 || shape == 2;
                Tier0Operation row;
                memset(&row, 0, sizeof(row));
                row.records[0] = check_vector_lower_record(ir_simd_permute2_forms[size][width], v0, one ? v1 : v2, one ? v0 : v1);
                row.record_count = 1;
                row.operand_count = one ? 2 : 3;
                for (uint32_t o = 0; o < row.operand_count; o += 1)
                {
                    const bool indices = o == (one ? 1u : 2u);
                    row.operand_classes[o] = BUSTER_TYPED_CLASS_VECTOR;
                    // 0 and 1 admitted; 2: a 64-byte table, wider than a 32-byte result (admitted at 64); 3: indices narrower.
                    row.operand_bytes[o] = (uint8_t)(indices ? (shape == 3 ? bytes / 2 : bytes) : shape == 0 ? 16 : shape == 2 ? 64 : bytes / 2);
                }
                row.result_class = BUSTER_TYPED_CLASS_VECTOR;
                row.result_bytes = (uint8_t)bytes;
                const bool admitted = shape <= 1 || (shape == 2 && width == 2);
                IrSimdTypedOperation decoded;
                memset(&decoded, 0xa5, sizeof(decoded));
                const IrSimdTypedStatus status = ir_simd_typed_row(&row, all, &decoded);
                ir_test_expect(tally,
                               admitted ? status == IR_SIMD_TYPED_OK && decoded.permute2 && decoded.permute2_indices == (one ? 1 : 2) && decoded.permute2_table == (one ? 0 : 1) &&
                                              decoded.operand_count == row.operand_count && decoded.immediate == size :
                                          ir_simd_typed_test_undecoded(status, &decoded),
                               "a permute of narrower tables decodes as the IR's permute2 of them widened, and of wider ones stays a machine row");
                narrow_rows += admitted && status == IR_SIMD_TYPED_OK;
            }
        }
    }
    ir_test_expect(tally, decoded_rows == 4 * 2 * 3 && narrow_rows == 4 * (2 * 2 + 1), "every permute of two tables decodes");
    printf("  IR SIMD rows: %u permutes of two tables decode to the IR's permute2\n", decoded_rows);
}

// The bitwise functions of three vectors (#1840): vpternlogd and vpternlogq at each width, without a writemask, under
// MASK(3)'s merging into the first operand and under its zeroing, decode as the IR's ternlog of its truth table, at
// exactly the features written out; refused without each and under 6 changes.
static void ir_simd_typed_test_ternlogs(IrTestTally* tally)
{
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ) };
    const uint64_t all = UINT64_MAX;
    uint32_t decoded_rows = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t lanes = 0; tables && lanes < 2; lanes += 1)
    {
        for (uint32_t width = 0; width < 3; width += 1)
        {
            for (uint32_t variant = 0; variant < 3; variant += 1)
            {
                const uint32_t bytes = 16u << width;
                const uint32_t count = bytes / (4u << lanes);
                const uint32_t mask_bytes = count <= 8 ? 1 : count / 8;
                const bool masked = variant != 0;
                Tier0Operation row;
                memset(&row, 0, sizeof(row));
                row.records[0] = check_vector_lower_record(ir_simd_ternlog_forms[lanes][width], BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_VECTOR(1), BUSTER_TIER0_ROLE_VECTOR(2));
                row.records[0].decorations = (uint8_t)(masked ? BUSTER_TIER0_DECORATION_MASK(3) | (variant == 2 ? BUSTER_X86_DECORATION_ZEROING : 0) : 0);
                row.records[0].immediate = 0xe8;
                row.record_count = 1;
                row.operand_count = (uint8_t)(masked ? 4 : 3);
                for (uint32_t o = 0; o < 3; o += 1)
                {
                    row.operand_classes[o] = BUSTER_TYPED_CLASS_VECTOR;
                    row.operand_bytes[o] = (uint8_t)bytes;
                }
                row.operand_classes[3] = masked ? BUSTER_TYPED_CLASS_MASK : 0;
                row.operand_bytes[3] = (uint8_t)(masked ? mask_bytes : 0);
                row.result_class = BUSTER_TYPED_CLASS_VECTOR;
                row.result_bytes = (uint8_t)bytes;
                // Written out: AVX512F, VL below 64 bytes, and a byte's mask's DQ.
                const uint64_t expected = BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) | (masked && mask_bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0);
                IrSimdTypedOperation decoded;
                memset(&decoded, 0xa5, sizeof(decoded));
                const IrSimdTypedStatus status = ir_simd_typed_row(&row, all, &decoded);
                char description[160];
                snprintf(description, sizeof(description), "vpternlog of %u-byte lanes (variant %u) at %u bytes: the IR's ternlog at exactly its features", 4u << lanes, variant, bytes);
                ir_test_expect(tally, status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.ternlog && decoded.immediate == 0xe8 && decoded.merge_mask == masked &&
                                          decoded.ternlog_zero == (variant == 2) && (!masked || decoded.lane == (lanes == 0 ? IR_SIMD_LANE_I32 : IR_SIMD_LANE_I64)) &&
                                          !decoded.permute2 && decoded.operand_count == row.operand_count && (decoded.required_features & ~BUSTER_X86_FEATURE(AVX)) == expected,
                               description);
                decoded_rows += status == IR_SIMD_TYPED_OK && decoded.ternlog;
                for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
                {
                    IrSimdTypedOperation without;
                    memset(&without, 0x5a, sizeof(without));
                    const IrSimdTypedStatus refused = ir_simd_typed_row(&row, all & ~features[f], &without);
                    const bool required = (expected & features[f]) != 0;
                    ir_test_expect(tally, required ? ir_simd_typed_test_undecoded(refused, &without) : refused == IR_SIMD_TYPED_OK && without.ternlog,
                                   "a bitwise function without a feature it needs stays the machine row, and needs no other");
                }
                for (uint32_t m = 0; m < 6; m += 1)
                {
                    Tier0Operation changed = row;
                    X86Instruction* record = &changed.records[0];
                    switch (m)
                    {
                    case 0: record->decorations = BUSTER_TIER0_DECORATION_MASK(2); break;
                    case 1: record->mode = BUSTER_X86_MEMORY_BASE; break;
                    case 2: record->vvvv = BUSTER_TIER0_ROLE_VECTOR(0); break;
                    case 3: changed.operand_bytes[2] = (uint8_t)(bytes == 64 ? 32 : 64); break;
                    case 4:
                        changed.records[1] = changed.records[0];
                        changed.record_count = 2;
                        break;
                    case 5: record->immediate = 256; break;
                    }
                    memset(&decoded, 0xa5, sizeof(decoded));
                    const IrSimdTypedStatus refused = ir_simd_typed_row(&changed, all, &decoded);
                    ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &decoded), "a changed bitwise function's row is not the IR's ternlog");
                }
            }
        }
    }
    ir_test_expect(tally, decoded_rows == 2 * 3 * 3, "every bitwise function of three vectors decodes");
    printf("  IR SIMD rows: %u bitwise functions of three vectors decode to the IR's ternlog\n", decoded_rows);
}

// The compresses and expands into a register (#1840): vpcompressb to vpcompressq and vpexpandb to vpexpandq at each
// width, merged under MASK(2) into VECTOR(1) and moved into VECTOR(0), decode as the IR's compress or expand of their
// lanes, at exactly the features written out; refused without each and under 6 changes.
static void ir_simd_typed_test_compresses(IrTestTally* tally)
{
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ), BUSTER_X86_FEATURE(AVX512VBMI2) };
    static const uint16_t moves[3] = { X86_FORM_VMOVDQU64_X_XM128, X86_FORM_VMOVDQU64_Y_YM256, X86_FORM_VMOVDQU64_Z_ZM512 };
    const uint64_t all = UINT64_MAX;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    uint32_t decoded_rows = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t expand = 0; tables && expand < 2; expand += 1)
    {
        for (uint32_t size = 0; size < 4; size += 1)
        {
            for (uint32_t width = 0; width < 3; width += 1)
            {
                const uint32_t bytes = 16u << width;
                const uint32_t lanes = bytes >> size;
                const uint32_t mask_bytes = lanes <= 8 ? 1 : lanes / 8;
                Tier0Operation row;
                memset(&row, 0, sizeof(row));
                const uint16_t form = expand != 0 ? ir_simd_expand_forms[size][width] : ir_simd_compress_forms[size][width];
                row.records[0] = check_vector_lower_record(form, expand != 0 ? v1 : v0, 0, expand != 0 ? v0 : v1);
                row.records[0].decorations = BUSTER_TIER0_DECORATION_MASK(2);
                row.records[1] = check_vector_lower_record(moves[width], v0, 0, v1);
                row.record_count = 2;
                row.operand_count = 3;
                row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
                row.operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
                row.operand_classes[2] = BUSTER_TYPED_CLASS_MASK;
                row.operand_bytes[0] = (uint8_t)bytes;
                row.operand_bytes[1] = (uint8_t)bytes;
                row.operand_bytes[2] = (uint8_t)mask_bytes;
                row.result_class = BUSTER_TYPED_CLASS_VECTOR;
                row.result_bytes = (uint8_t)bytes;
                // Written out: AVX512F, VL below 64 bytes, BW and VBMI2 for bytes and words, a byte's mask's DQ and 4
                // bytes' BW.
                const uint64_t expected = BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) |
                                          (size <= 1 ? BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512VBMI2) : 0) | (mask_bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0) |
                                          (mask_bytes >= 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
                IrSimdTypedOperation decoded;
                memset(&decoded, 0xa5, sizeof(decoded));
                const IrSimdTypedStatus status = ir_simd_typed_row(&row, all, &decoded);
                char description[160];
                snprintf(description, sizeof(description), "%s of %u-byte lanes at %u bytes: the IR's own at exactly its features", expand != 0 ? "vpexpand" : "vpcompress", 1u << size, bytes);
                ir_test_expect(tally, status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.compress == (expand != 0 ? BUSTER_IR_INTRINSIC_EXPAND : BUSTER_IR_INTRINSIC_COMPRESS) + 1 &&
                                          decoded.immediate == size && !decoded.ternlog && !decoded.permute2 && decoded.operand_count == 3 &&
                                          (decoded.required_features & ~BUSTER_X86_FEATURE(AVX)) == expected,
                               description);
                decoded_rows += status == IR_SIMD_TYPED_OK && decoded.compress != 0;
                for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
                {
                    IrSimdTypedOperation without;
                    memset(&without, 0x5a, sizeof(without));
                    const IrSimdTypedStatus refused = ir_simd_typed_row(&row, all & ~features[f], &without);
                    const bool required = (expected & features[f]) != 0;
                    ir_test_expect(tally, required ? ir_simd_typed_test_undecoded(refused, &without) : refused == IR_SIMD_TYPED_OK && without.compress != 0,
                                   "a compress or expand without a feature it needs stays the machine row, and needs no other");
                }
                for (uint32_t m = 0; m < 6; m += 1)
                {
                    Tier0Operation changed = row;
                    switch (m)
                    {
                    case 0: changed.records[0].decorations = 0; break;
                    case 1: changed.records[0].mode = BUSTER_X86_MEMORY_BASE; break;
                    case 2: changed.records[1].rm = v0; break;
                    case 3: changed.operand_bytes[2] = (uint8_t)(mask_bytes == 8 ? 4 : mask_bytes * 2); break;
                    case 4: changed.record_count = 1; break;
                    case 5: changed.records[0].decorations = BUSTER_TIER0_DECORATION_MASK(2) | BUSTER_X86_DECORATION_ZEROING; break;
                    }
                    memset(&decoded, 0xa5, sizeof(decoded));
                    const IrSimdTypedStatus refused = ir_simd_typed_row(&changed, all, &decoded);
                    ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &decoded), "a changed compress or expand row is not the IR's");
                }
            }
        }
    }
    ir_test_expect(tally, decoded_rows == 2 * 4 * 3, "every compress and expand decodes");
    printf("  IR SIMD rows: %u compresses and expands decode to the IR's compress and expand\n", decoded_rows);
}

// The masked loads and stores (#1840): each of the front end's moves (ir_simd_masked_moves: vmovdqu8 to vmovdqu64,
// vmovdqa32, vmovdqa64, vmovups, vmovupd, vmovaps, vmovapd) at each width, a load's from [INTEGER(0)] into VECTOR(1) under
// MASK(2) then moved whole into VECTOR(0), a store's of VECTOR(1) to [INTEGER(0)] under MASK(2), decode as the IR's
// masked load or store of their lanes, an aligned move's alignment the vector's bytes, at exactly the features written
// out; refused without each and under 7 changes.
static void ir_simd_typed_test_masked_memory(IrTestTally* tally)
{
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ) };
    static const uint16_t moves[3] = { X86_FORM_VMOVDQU64_X_XM128, X86_FORM_VMOVDQU64_Y_YM256, X86_FORM_VMOVDQU64_Z_ZM512 };
    static const uint8_t sizes[10] = { 0, 1, 2, 3, 2, 3, 2, 3, 2, 3 };
    static const uint8_t lanes_of[4] = { IR_SIMD_LANE_I8, IR_SIMD_LANE_I16, IR_SIMD_LANE_I32, IR_SIMD_LANE_I64 };
    const uint64_t all = UINT64_MAX;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    uint32_t decoded_rows = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t kind = 0; tables && kind < 10; kind += 1)
    {
        for (uint32_t store = 0; store < 2; store += 1)
        {
            for (uint32_t width = 0; width < 3; width += 1)
            {
                const uint32_t size = sizes[kind];
                const bool aligned = kind == 4 || kind == 5 || kind == 8 || kind == 9;
                const uint32_t bytes = 16u << width;
                const uint32_t lanes = bytes >> size;
                const uint32_t mask_bytes = lanes <= 8 ? 1 : lanes / 8;
                Tier0Operation row;
                memset(&row, 0, sizeof(row));
                row.records[0] = (X86Instruction){ .form = ir_simd_masked_moves[kind][store][width], .reg = v1, .rm = BUSTER_TIER0_ROLE_INTEGER(0), .mode = BUSTER_X86_MEMORY_BASE,
                                                   .decorations = BUSTER_TIER0_DECORATION_MASK(2) };
                row.records[1] = check_vector_lower_record(moves[width], v0, 0, v1);
                row.record_count = store != 0 ? 1 : 2;
                row.operand_count = 3;
                row.operand_classes[0] = BUSTER_TYPED_CLASS_U64;
                row.operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
                row.operand_classes[2] = BUSTER_TYPED_CLASS_MASK;
                row.operand_bytes[1] = (uint8_t)bytes;
                row.operand_bytes[2] = (uint8_t)mask_bytes;
                row.result_class = store != 0 ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_VECTOR;
                row.result_bytes = store != 0 ? 0 : (uint8_t)bytes;
                // Written out: AVX512F, VL below 64 bytes, BW for bytes' and words' moves, a byte's mask's DQ and 4 bytes' BW.
                const uint64_t expected = BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) | (size <= 1 ? BUSTER_X86_FEATURE(AVX512BW) : 0) |
                                          (mask_bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0) | (mask_bytes >= 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
                IrSimdTypedOperation decoded;
                memset(&decoded, 0xa5, sizeof(decoded));
                const IrSimdTypedStatus status = ir_simd_typed_row(&row, all, &decoded);
                char description[160];
                snprintf(description, sizeof(description), "masked %s %u of %u-byte lanes at %u bytes: the IR's own at exactly its features", store != 0 ? "store" : "load", kind, 1u << size,
                         bytes);
                ir_test_expect(tally, status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.masked_memory == 1 + store && decoded.lane == lanes_of[size] &&
                                          decoded.immediate == (aligned ? 4 + width : 0) && decoded.compress == 0 && decoded.operand_count == 3 &&
                                          (decoded.required_features & ~BUSTER_X86_FEATURE(AVX)) == expected,
                               description);
                decoded_rows += status == IR_SIMD_TYPED_OK && decoded.masked_memory != 0;
                for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
                {
                    IrSimdTypedOperation without;
                    memset(&without, 0x5a, sizeof(without));
                    const IrSimdTypedStatus refused = ir_simd_typed_row(&row, all & ~features[f], &without);
                    const bool required = (expected & features[f]) != 0;
                    ir_test_expect(tally, required ? ir_simd_typed_test_undecoded(refused, &without) : refused == IR_SIMD_TYPED_OK && without.masked_memory != 0,
                                   "a masked load or store without a feature it needs stays the machine row, and needs no other");
                }
                for (uint32_t m = 0; m < 7; m += 1)
                {
                    Tier0Operation changed = row;
                    switch (m)
                    {
                    case 0: changed.records[0].decorations = 0; break;
                    case 1: changed.records[0].mode = BUSTER_X86_REGISTER_DIRECT; break;
                    case 2: changed.records[0].rm = BUSTER_TIER0_ROLE_INTEGER(1); break;
                    case 3: changed.operand_bytes[2] = (uint8_t)(mask_bytes == 8 ? 4 : mask_bytes * 2); break;
                    case 4: changed.record_count = store != 0 ? 2 : 1; break;
                    case 5: changed.records[0].decorations = BUSTER_TIER0_DECORATION_MASK(2) | BUSTER_X86_DECORATION_ZEROING; break;
                    case 6: changed.records[0].displacement = 8; break;
                    }
                    memset(&decoded, 0xa5, sizeof(decoded));
                    const IrSimdTypedStatus refused = ir_simd_typed_row(&changed, all, &decoded);
                    ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &decoded), "a changed masked load or store row is not the IR's masked access");
                }
            }
        }
    }
    ir_test_expect(tally, decoded_rows == 10 * 2 * 3, "every masked load and store decodes");
    printf("  IR SIMD rows: %u masked loads and stores decode to the IR's masked_load and masked_store\n", decoded_rows);
}

// One lane's moves and the lanes' sign bits (#1840): vmovd and vmovq of lane 0 and vpextrb to vpextrq of each lane of
// each width's low 16 bytes, and after vextracti32x4 of each part past them, into an integer of the lane's bytes or 4;
// vpinsrb to vpinsrq into each lane of a 16-byte vector; and vpmovmskb, vmovmskps and vmovmskpd of a 16- and a 32-byte
// vector, decode as the IR's lane_extract, lane_insert and movemask of their lane at exactly their records' features
// (x86_target_requirement's, and the moves'), refused without each and under 4 changes.
static void ir_simd_typed_test_lane_moves(IrTestTally* tally)
{
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ),
                                         BUSTER_X86_FEATURE(AVX), BUSTER_X86_FEATURE(AVX2) };
    static const uint8_t result_classes[4] = { BUSTER_TYPED_CLASS_U8, BUSTER_TYPED_CLASS_U16, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I64 };
    const uint64_t all = UINT64_MAX;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t out = BUSTER_TIER0_ROLE_INTEGER_RESULT;
    uint32_t decoded_rows = 0;
    uint32_t tried = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t shape = 0; tables && shape < 4 * 3 * 4 * 16 + 4 * 16 + 4 * 2; shape += 1)
    {
        Tier0Operation row;
        memset(&row, 0, sizeof(row));
        uint32_t kind = 0;
        uint32_t expected_flags = 0;
        bool valid = true;
        if (shape < 4 * 3 * 4 * 16)
        {
            // An extraction: lane sub of the part `part` of a vector of `width`, by lanes of `size`.
            const uint32_t size = shape % 4;
            const uint32_t width = (shape / 4) % 3;
            const uint32_t part = (shape / 12) % 4;
            const uint32_t sub = shape / 48;
            const uint32_t per = 16u >> size;
            valid = sub < per && part < (1u << width);
            const uint8_t from = part != 0 ? v1 : v0;
            uint32_t r = 0;
            if (part != 0)
            {
                row.records[r++] = (X86Instruction){ .form = ir_simd_part_extract_forms[width == 2], .reg = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = part };
            }
            const bool whole = sub == 0 && size >= 2;
            const bool word = !whole && size == 1;
            row.records[r++] = (X86Instruction){ .form = whole ? (size == 2 ? X86_FORM_VMOVD_RM32_X_EVEX : X86_FORM_VMOVQ_RM64_X_EVEX) : ir_simd_lane_extract_forms[size],
                                                 .reg = word ? out : from,
                                                 .rm = word ? from : out,
                                                 .mode = BUSTER_X86_REGISTER_DIRECT,
                                                 .immediate = whole ? 0 : sub };
            row.record_count = r;
            row.operand_count = 1;
            row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row.operand_bytes[0] = (uint8_t)(16u << width);
            row.result_class = result_classes[size];
            kind = BUSTER_IR_INTRINSIC_LANE_EXTRACT;
            expected_flags = size | ((part * per + sub) << 2);
        }
        else if (shape < 4 * 3 * 4 * 16 + 4 * 16)
        {
            const uint32_t index = shape - 4 * 3 * 4 * 16;
            const uint32_t size = index % 4;
            const uint32_t sub = index / 4;
            valid = sub < (16u >> size);
            row.records[0] = (X86Instruction){ .form = ir_simd_lane_insert_forms[size], .reg = v0, .vvvv = v0, .rm = BUSTER_TIER0_ROLE_INTEGER(1), .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = sub };
            row.record_count = 1;
            row.operand_count = 2;
            row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row.operand_bytes[0] = 16;
            row.operand_classes[1] = size == 3 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_I32;
            row.result_class = BUSTER_TYPED_CLASS_VECTOR;
            row.result_bytes = 16;
            kind = BUSTER_IR_INTRINSIC_LANE_INSERT;
            expected_flags = size | (sub << 2);
        }
        else
        {
            const uint32_t index = shape - 4 * 3 * 4 * 16 - 4 * 16;
            const uint32_t size = index % 4;
            const uint32_t width = index / 4;
            valid = size != 1;
            row.records[0] = (X86Instruction){ .form = ir_simd_movemask_forms[size][width], .reg = out, .rm = v0, .mode = BUSTER_X86_REGISTER_DIRECT };
            row.record_count = 1;
            row.operand_count = 1;
            row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row.operand_bytes[0] = (uint8_t)(16u << width);
            row.result_class = BUSTER_TYPED_CLASS_I32;
            kind = BUSTER_IR_INTRINSIC_MOVEMASK;
            expected_flags = size;
        }
        if (!valid)
        {
            continue;
        }
        tried += 1;
        uint64_t records = 0;
        for (uint32_t r = 0; r < row.record_count; r += 1)
        {
            records |= x86_target_requirement(row.records[r].form) & ~BUSTER_X86_TARGET_KNOWN;
        }
        const uint64_t expected = records | ir_typed_vector_moves(row.operand_bytes[0], all);
        IrSimdTypedOperation decoded;
        memset(&decoded, 0xa5, sizeof(decoded));
        const IrSimdTypedStatus status = ir_simd_typed_row(&row, all, &decoded);
        char description[160];
        snprintf(description, sizeof(description), "lane move %u of flags %u at %u bytes: the IR's own at exactly its features", kind, expected_flags, row.operand_bytes[0]);
        ir_test_expect(tally, status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.lane_move == kind + 1 && decoded.immediate == expected_flags && decoded.masked_memory == 0 &&
                                  decoded.required_features == expected,
                       description);
        decoded_rows += status == IR_SIMD_TYPED_OK && decoded.lane_move != 0;
        for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
        {
            IrSimdTypedOperation without;
            memset(&without, 0x5a, sizeof(without));
            const IrSimdTypedStatus refused = ir_simd_typed_row(&row, all & ~features[f], &without);
            // The moves at that target: a VEX record's vector moves are AVX's there without AVX512VL (#1281).
            const uint64_t at_target = records | ir_typed_vector_moves(row.operand_bytes[0], all & ~features[f]);
            const bool required = (at_target & features[f]) != 0;
            ir_test_expect(tally, required ? ir_simd_typed_test_undecoded(refused, &without) : refused == IR_SIMD_TYPED_OK && without.lane_move != 0,
                           "a lane move without a feature it needs stays the machine row, and needs no other");
        }
        for (uint32_t m = 0; m < 4; m += 1)
        {
            Tier0Operation changed = row;
            X86Instruction* last = &changed.records[changed.record_count - 1];
            switch (m)
            {
            case 0: last->decorations = BUSTER_TIER0_DECORATION_MASK(0); break;
            case 1: last->mode = BUSTER_X86_MEMORY_BASE; break;
            case 2: last->immediate = 16; break;
            case 3: changed.result_class = kind == BUSTER_IR_INTRINSIC_LANE_INSERT ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_F64; break;
            }
            memset(&decoded, 0xa5, sizeof(decoded));
            const IrSimdTypedStatus refused = ir_simd_typed_row(&changed, all, &decoded);
            ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &decoded), "a changed lane move's row is not the IR's");
        }
    }
    // Extractions: 30 lanes in 16 bytes (16 + 8 + 4 + 2) in 7 parts (1 + 2 + 4); insertions: 30; movemasks: 3 sizes at 2 widths.
    ir_test_expect(tally, decoded_rows == tried && tried == 30 * 7 + 30 + 3 * 2, "every lane move decodes");
    printf("  IR SIMD rows: %u of the front end's one-lane moves and movemasks decode to the IR's lane_extract, lane_insert and movemask\n", decoded_rows);
}

// The lanes' unary operations (#1840): each row of tier1_masks_test_lane_unary_row at each width decodes as the IR's
// lane_unary of its lanes and operation, written out (the two-operand leading zero count's with its select), at exactly
// its records' features and the vectors' moves; refused without each and under 6 changes.
static void ir_simd_typed_test_lane_unaries(IrTestTally* tally)
{
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ),
                                         BUSTER_X86_FEATURE(AVX512CD), BUSTER_X86_FEATURE(AVX512BITALG), BUSTER_X86_FEATURE(AVX512VPOPCNTDQ) };
    const uint64_t all = UINT64_MAX;
    uint32_t decoded_rows = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t kind = 0; tables && kind < 10; kind += 1)
    {
        for (uint32_t width = 0; width < 3; width += 1)
        {
            uint32_t flags = 0;
            const Tier0Operation row = tier1_masks_test_lane_unary_row(kind, width, &flags);
            uint64_t expected = ir_typed_vector_moves(row.result_bytes, all);
            for (uint32_t r = 0; r < row.record_count; r += 1)
            {
                expected |= x86_target_requirement(row.records[r].form) & ~BUSTER_X86_TARGET_KNOWN;
            }
            IrSimdTypedOperation decoded;
            memset(&decoded, 0xa5, sizeof(decoded));
            const IrSimdTypedStatus status = ir_simd_typed_row(&row, all, &decoded);
            char description[192];
            snprintf(description, sizeof(description), "lane unary %u at %u bytes: the IR's lane_unary 0x%x at exactly its features", kind, row.result_bytes, flags);
            const bool ok = status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.lane_unary && decoded.lane_unary_zero == (kind == 6 || kind == 7) &&
                            decoded.immediate == flags && !decoded.lane_op && decoded.required_features == expected;
            ir_test_expect(tally, ok, description);
            decoded_rows += ok;
            for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
            {
                IrSimdTypedOperation without;
                memset(&without, 0x5a, sizeof(without));
                const IrSimdTypedStatus refused = ir_simd_typed_row(&row, all & ~features[f], &without);
                const bool required = ((expected | ir_typed_vector_moves(row.result_bytes, all & ~features[f])) & features[f]) != 0;
                ir_test_expect(tally, required ? ir_simd_typed_test_undecoded(refused, &without) : refused == IR_SIMD_TYPED_OK && without.lane_unary,
                               "a lane's unary operation without a feature it needs stays the machine row, and needs no other");
            }
            for (uint32_t m = 0; m < 6; m += 1)
            {
                Tier0Operation changed = row;
                X86Instruction* last = &changed.records[changed.record_count - 1];
                switch (m)
                {
                case 0: changed.records[0].decorations = BUSTER_TIER0_DECORATION_MASK(0); break;
                case 1: last->mode = BUSTER_X86_MEMORY_BASE; break;
                case 2: last->rm = BUSTER_TIER0_ROLE_VECTOR(3); break;
                case 3: changed.result_bytes = (uint8_t)(row.result_bytes == 64 ? 32 : 64); break;
                case 4: last->decorations ^= 1; break;
                case 5:
                    changed.records[changed.record_count] = changed.records[0];
                    changed.record_count += 1;
                    break;
                }
                memset(&decoded, 0xa5, sizeof(decoded));
                const IrSimdTypedStatus refused = ir_simd_typed_row(&changed, all, &decoded);
                ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &decoded), "a changed lane unary operation's row is not the IR's lane_unary");
            }
        }
    }
    ir_test_expect(tally, decoded_rows == 10 * 3, "every lane count and conflict decodes");
    printf("  IR SIMD rows: %u population counts, leading zero counts and conflicts decode to the IR's lane_unary\n", decoded_rows);
}

// The AES rounds (#1840): vaesenc and vaesenclast, EVEX at each width and VEX at 16 and 32 bytes, VECTOR(0) the state
// and VECTOR(1) the round key, decode as the IR's aes of their round at exactly their form's features; refused without
// VAES and under 4 changes.
static void ir_simd_typed_test_aes(IrTestTally* tally)
{
    const uint64_t all = UINT64_MAX;
    uint32_t decoded_rows = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t form = 0; tables && form < 4; form += 1)
    {
        for (uint32_t width = 0; width < 3; width += 1)
        {
            const uint32_t last = form & 1;
            const uint16_t record_form = form < 2 ? ir_simd_aes_forms[last][width] : ir_simd_aes_vex_forms[last][width];
            if (record_form == X86_FORM_NONE)
            {
                continue;
            }
            const uint32_t bytes = 16u << width;
            Tier0Operation row;
            memset(&row, 0, sizeof(row));
            row.records[0] = check_vector_lower_record(record_form, BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_VECTOR(1));
            row.record_count = 1;
            row.operand_count = 2;
            row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row.operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row.operand_bytes[0] = (uint8_t)bytes;
            row.operand_bytes[1] = (uint8_t)bytes;
            row.result_class = BUSTER_TYPED_CLASS_VECTOR;
            row.result_bytes = (uint8_t)bytes;
            const uint64_t needed = x86_target_requirement(record_form) & ~BUSTER_X86_TARGET_KNOWN;
            IrSimdTypedOperation decoded;
            memset(&decoded, 0xa5, sizeof(decoded));
            const IrSimdTypedStatus status = ir_simd_typed_row(&row, all, &decoded);
            ir_test_expect(tally, status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.aes && decoded.immediate == last && decoded.operand_count == 2 &&
                                      (decoded.required_features & needed) == needed && needed != 0,
                           "an AES round decodes as the IR's aes at its form's features");
            decoded_rows += status == IR_SIMD_TYPED_OK && decoded.aes;
            IrSimdTypedOperation without;
            memset(&without, 0x5a, sizeof(without));
            const uint64_t crypto = BUSTER_X86_FEATURE(VAES) | BUSTER_X86_FEATURE(AES);
            ir_test_expect(tally, ir_simd_typed_test_undecoded(ir_simd_typed_row(&row, all & ~crypto, &without), &without), "an AES round without AES and VAES stays the machine row");
            for (uint32_t m = 0; m < 4; m += 1)
            {
                Tier0Operation changed = row;
                X86Instruction* record = &changed.records[0];
                switch (m)
                {
                case 0: record->decorations = BUSTER_TIER0_DECORATION_MASK(1); break;
                case 1: record->mode = BUSTER_X86_MEMORY_BASE; break;
                case 2: record->vvvv = BUSTER_TIER0_ROLE_VECTOR(1); break;
                case 3: changed.operand_bytes[1] = (uint8_t)(bytes == 64 ? 32 : 64); break;
                }
                memset(&decoded, 0xa5, sizeof(decoded));
                ir_test_expect(tally, ir_simd_typed_test_undecoded(ir_simd_typed_row(&changed, all, &decoded), &decoded), "a changed AES round's row is not the IR's aes");
            }
        }
    }
    ir_test_expect(tally, decoded_rows == 2 * 3 + 2 * 2, "every AES round decodes");
    printf("  IR SIMD rows: %u AES rounds decode to the IR's aes\n", decoded_rows);
}

// The masks to vectors and back (#1840): every MASK_VECTOR rule's row (`__builtin_ia32_cvtmask2b` to `_cvtmask2q`) and
// every VECTOR_MASK rule's (`_cvtb2mask` to `_cvtq2mask`), at each width, as the opmask conversion leaves them, decodes
// as the IR's select of all ones and zeros or its comparison of signed lanes less than zero, at the elements' lanes and
// exactly the features written out; refused without each and under 6 changes.
static void ir_simd_typed_test_mask_vectors(IrTestTally* tally)
{
    const X86Target every = { .features = UINT64_MAX, .extensions = UINT64_MAX };
    const uint64_t all = UINT64_MAX;
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ) };
    uint32_t expansions = 0;
    uint32_t signs = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t i = 0; tables && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const bool expand = rule->family == CHECK_X86_LOWER_MASK_VECTOR;
        if (!expand && rule->family != CHECK_X86_LOWER_VECTOR_MASK)
        {
            continue;
        }
        Tier0Operation row;
        char description[224];
        const bool made = check_x86_lower_row_of(&every, rule, 0, 4, 0, false, &row) == NULL && check_x86_lower_masks(&row, &every);
        snprintf(description, sizeof(description), "%s: its row is the front end's", rule->name);
        ir_test_expect(tally, made, description);
        if (!made)
        {
            continue;
        }
        const uint32_t lanes = rule->bytes / rule->element;
        const uint32_t mask_bytes = lanes <= 8 ? 1 : lanes / 8;
        const uint8_t lane = rule->element == 1 ? IR_SIMD_LANE_I8 : rule->element == 2 ? IR_SIMD_LANE_I16 : rule->element == 4 ? IR_SIMD_LANE_I32 : IR_SIMD_LANE_I64;
        // Written out: AVX512F, VL below 64 bytes, the form's (BW for bytes and words, DQ for doublewords and quadwords),
        // and the mask's moves (DQ for 8 lanes or fewer, BW for 32 and 64).
        const uint64_t expected = BUSTER_X86_FEATURE(AVX512F) | (rule->bytes < 64 ? BUSTER_X86_FEATURE(AVX512VL) : 0) |
                                  (rule->element <= 2 || lanes >= 32 ? BUSTER_X86_FEATURE(AVX512BW) : 0) | (rule->element >= 4 || lanes <= 8 ? BUSTER_X86_FEATURE(AVX512DQ) : 0);
        IrSimdTypedOperation decoded;
        memset(&decoded, 0xa5, sizeof(decoded));
        const IrSimdTypedStatus status = ir_simd_typed_row(&row, all, &decoded);
        snprintf(description, sizeof(description), "%s: the IR's %s at its elements' lanes and features", rule->name, expand ? "select of ones and zeros" : "comparison less than zero");
        ir_test_expect(tally, status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.mask_expand == expand && decoded.sign_mask == !expand && decoded.lane == lane &&
                                  decoded.operation == IR_SIMD_OPERATION_COUNT && !decoded.compare_mask && !decoded.expand_mask && decoded.mask_bytes == 0 &&
                                  decoded.operand_count == 1 && decoded.operand_bytes[0] == (expand ? mask_bytes : rule->bytes) && decoded.required_features == expected,
                       description);
        expansions += expand && status == IR_SIMD_TYPED_OK && decoded.mask_expand;
        signs += !expand && status == IR_SIMD_TYPED_OK && decoded.sign_mask;
        for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
        {
            IrSimdTypedOperation without;
            memset(&without, 0x5a, sizeof(without));
            const IrSimdTypedStatus refused = ir_simd_typed_row(&row, all & ~features[f], &without);
            const bool needed = (expected & features[f]) != 0;
            snprintf(description, sizeof(description), "%s: without a feature it needs it stays the machine row, and needs no other", rule->name);
            ir_test_expect(tally, needed ? ir_simd_typed_test_undecoded(refused, &without) : refused == IR_SIMD_TYPED_OK && (without.mask_expand || without.sign_mask), description);
        }
        for (uint32_t m = 0; m < 6; m += 1)
        {
            Tier0Operation changed = row;
            X86Instruction* record = &changed.records[0];
            switch (m)
            {
            case 0: record->decorations = BUSTER_TIER0_DECORATION_MASK(0); break;
            case 1: record->mode = BUSTER_X86_MEMORY_BASE; break;
            case 2: record->reg = expand ? BUSTER_TIER0_ROLE_VECTOR(1) : BUSTER_TIER0_ROLE_MASK_SCRATCH(0); break;
            case 3: changed.operand_bytes[0] = expand ? (uint8_t)(mask_bytes == 8 ? 4 : mask_bytes * 2) : (uint8_t)(rule->bytes == 64 ? 32 : 64); break;
            case 4:
                changed.records[1] = changed.records[0];
                changed.record_count = 2;
                break;
            case 5: record->immediate = 1; break;
            }
            memset(&decoded, 0xa5, sizeof(decoded));
            const IrSimdTypedStatus refused = ir_simd_typed_row(&changed, all, &decoded);
            snprintf(description, sizeof(description), "%s (change %u): not the IR's mask conversion", rule->name, m);
            ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &decoded), description);
        }
    }
    // Bytes to quadwords at three widths, each way.
    ir_test_expect(tally, expansions == 12 && signs == 12, "every mask to vector and vector to mask rule decodes");
    printf("  IR SIMD rows: the front end's %u masks to vectors decode to the IR's vselect of ones and zeros, and %u vectors to masks to its comparison less than zero\n",
           expansions, signs);
}

// The masked moves' selects and the undefined vectors (#1840): every SELECT rule's row (`__builtin_ia32_select*`, of each
// element and width) as the opmask conversion leaves it decodes as the IR's vselect of its elements' integer lanes, its
// mask, VECTOR(1) and VECTOR(2), at exactly its forms' features and its mask's moves', and every UNDEFINED rule's as the
// zero vector; without a feature either needs, and changed one way at a time, each stays a machine row.
static void ir_simd_typed_test_selects(IrTestTally* tally)
{
    const X86Target every = { .features = UINT64_MAX, .extensions = UINT64_MAX };
    const uint64_t avx512 = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ);
    static const uint64_t features[] = { BUSTER_X86_FEATURE(AVX512F), BUSTER_X86_FEATURE(AVX512VL), BUSTER_X86_FEATURE(AVX512BW), BUSTER_X86_FEATURE(AVX512DQ) };
    uint32_t selects = 0;
    uint32_t zeros = 0;
    const bool tables = x86_encoder_initialize() && tier0_initialize();
    ir_test_expect(tally, tables, "the encoder's, the effects' and Tier 0's tables");
    for (uint32_t i = 0; tables && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const bool select = rule->family == CHECK_X86_LOWER_SELECT;
        if (!select && rule->family != CHECK_X86_LOWER_UNDEFINED)
        {
            continue;
        }
        Tier0Operation row;
        char description[224];
        const bool made = check_x86_lower_row_of(&every, rule, 0, 4, 0, false, &row) == NULL && (!select || check_x86_lower_masks(&row, &every));
        snprintf(description, sizeof(description), "%s: its row is the front end's", rule->name);
        ir_test_expect(tally, made, description);
        if (!made)
        {
            continue;
        }
        const uint32_t lanes = rule->bytes / rule->element;
        const uint32_t mask_bytes = lanes <= 8 ? 1 : lanes / 8;
        const uint8_t lane = rule->element == 1 ? IR_SIMD_LANE_I8 : rule->element == 2 ? IR_SIMD_LANE_I16 : rule->element == 4 ? IR_SIMD_LANE_I32 : IR_SIMD_LANE_I64;
        // Written out: AVX512F, VL below 64 bytes, BW for bytes' and words' moves and for 32 and 64 lanes' masks, DQ for 8
        // lanes' or fewer; an undefined vector's vpxorq AVX512F's and VL's alone.
        const uint64_t expected = BUSTER_X86_FEATURE(AVX512F) | (rule->bytes < 64 ? BUSTER_X86_FEATURE(AVX512VL) : 0) |
                                  (select && (rule->element <= 2 || lanes >= 32) ? BUSTER_X86_FEATURE(AVX512BW) : 0) |
                                  (select && lanes <= 8 ? BUSTER_X86_FEATURE(AVX512DQ) : 0);
        const IrSimdShape shape = (IrSimdShape)(IR_SIMD_V4I32 + (rule->bytes == 16 ? 0 : rule->bytes == 32 ? 1 : 2));
        IrSimdTypedOperation decoded;
        memset(&decoded, 0xa5, sizeof(decoded));
        const IrSimdTypedStatus status = ir_simd_typed_row(&row, avx512, &decoded);
        const bool common = status == IR_SIMD_TYPED_OK && !decoded.machine && !decoded.compare_mask && !decoded.expand_mask && decoded.mask_bytes == 0 &&
                            decoded.intrinsic == 0 && decoded.splat_bytes == 0 && decoded.required_features == expected && decoded.shape == shape;
        if (select)
        {
            selects += 1;
            snprintf(description, sizeof(description), "%s: the IR's vselect of its mask and two vectors at its elements' lanes and features", rule->name);
            ir_test_expect(tally, common && !decoded.zero && decoded.operation == IR_SIMD_SELECT && decoded.lane == lane && decoded.operand_count == 3 &&
                                      decoded.operand_classes[0] == BUSTER_TYPED_CLASS_MASK && decoded.operand_bytes[0] == mask_bytes,
                           description);
        }
        else
        {
            zeros += 1;
            snprintf(description, sizeof(description), "%s: the IR's zero vector at its features", rule->name);
            ir_test_expect(tally, common && decoded.zero && decoded.operation == IR_SIMD_OPERATION_COUNT && decoded.operand_count == 0, description);
        }
        for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(features); f += 1)
        {
            IrSimdTypedOperation without;
            memset(&without, 0x5a, sizeof(without));
            const IrSimdTypedStatus refused = ir_simd_typed_row(&row, avx512 & ~features[f], &without);
            const bool needed = (expected & features[f]) != 0;
            snprintf(description, sizeof(description), "%s: without a feature it needs it stays the machine row, and needs no other", rule->name);
            ir_test_expect(tally, needed ? ir_simd_typed_test_undecoded(refused, &without) : refused == IR_SIMD_TYPED_OK && !without.machine, description);
        }
        for (uint32_t m = 0; m < 11; m += 1)
        {
            Tier0Operation changed = row;
            X86Instruction* last = &changed.records[changed.record_count - 1];
            bool applies = true;
            switch (m)
            {
            case 0: last->decorations = select ? 0 : BUSTER_TIER0_DECORATION_MASK(0); break;
            case 1: last->decorations = (uint8_t)(BUSTER_TIER0_DECORATION_MASK(0) | BUSTER_X86_DECORATION_ZEROING); break;
            case 2: last->displacement = 8; break;
            case 3: last->mode = BUSTER_X86_MEMORY_BASE; break;
            case 4: last->rm = BUSTER_TIER0_ROLE_VECTOR(3); break;
            case 5: last->reg = BUSTER_TIER0_ROLE_VECTOR(1); break;
            case 6: changed.records[0].rm = BUSTER_TIER0_ROLE_VECTOR(1); break;
            case 7: last->immediate = 1; break;
            case 8:
                applies = select;
                changed.operand_bytes[0] = (uint8_t)(mask_bytes == 8 ? 4 : mask_bytes * 2);
                break;
            case 9:
                applies = select;
                changed.operand_bytes[2] = rule->bytes == 64 ? 32 : 64;
                break;
            case 10:
                changed.records[changed.record_count] = *last;
                changed.record_count += 1;
                break;
            }
            if (applies)
            {
                memset(&decoded, 0xa5, sizeof(decoded));
                const IrSimdTypedStatus refused = ir_simd_typed_row(&changed, avx512, &decoded);
                snprintf(description, sizeof(description), "%s (change %u): neither the IR's select nor its zero vector", rule->name, m);
                ir_test_expect(tally, ir_simd_typed_test_undecoded(refused, &decoded), description);
            }
        }
    }
    // Bytes, words, doublewords and quadwords and the packed floats', doubles', halves' and bf16s' selects at 3 widths, and
    // the 3 undefined vectors.
    ir_test_expect(tally, selects == 8 * 3 && zeros == 3, "every select and undefined vector rule decodes");
    printf("  IR SIMD rows: %u of the front end's select rows decode to the IR's vselect and %u undefined vectors to its zero vector\n", selects, zeros);
}

static void ir_simd_typed_test(IrTestTally* tally)
{
    ir_simd_typed_test_selects(tally);
    ir_simd_typed_test_conversions(tally);
    ir_simd_typed_test_permutes(tally);
    ir_simd_typed_test_shifts(tally);
    ir_simd_typed_test_mask_vectors(tally);
    ir_simd_typed_test_lane_ops(tally);
    ir_simd_typed_test_permute2s(tally);
    ir_simd_typed_test_ternlogs(tally);
    ir_simd_typed_test_compresses(tally);
    ir_simd_typed_test_masked_memory(tally);
    ir_simd_typed_test_lane_moves(tally);
    ir_simd_typed_test_lane_unaries(tally);
    ir_simd_typed_test_aes(tally);
    ir_simd_typed_test_widths(tally);
    ir_simd_typed_test_splats(tally);
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
            // Floats' and doubles' arithmetic and comparisons (#141's element types) decode into the IR's floating
            // operations of their lanes, and their comparisons into a comparison of floating lanes expanded at their
            // width (C's predicates: no unsigned ones).
            static const CheckOp floating_operations[] = { CHECK_OP_ADD, CHECK_OP_SUBTRACT, CHECK_OP_MULTIPLY, CHECK_OP_DIVIDE, CHECK_OP_LESS, CHECK_OP_LESS_EQUAL,
                                                           CHECK_OP_GREATER, CHECK_OP_GREATER_EQUAL, CHECK_OP_EQUAL, CHECK_OP_NOT_EQUAL };
            static const IrSimdOperation floating_semantics[] = { IR_SIMD_FADD, IR_SIMD_FSUB, IR_SIMD_FMUL, IR_SIMD_FDIV };
            for (uint32_t element = 0; unsigned_lanes == 0 && element < 2; element += 1)
            {
                const TypeId floating = type_vector(&table.table, element == 0 ? BUSTER_TYPE_FLOAT : BUSTER_TYPE_DOUBLE, (4u << width) >> element, false);
                for (uint32_t operation = 0; operation < BUSTER_ARRAY_LENGTH(floating_operations); operation += 1)
                {
                    Tier0Operation row = { 0 };
                    const char* reason = check_vector_lower_row(&table.table, &producer, false, floating_operations[operation], floating, BUSTER_TYPED_CLASS_VOID, &row);
                    IrSimdTypedOperation decoded = { 0 };
                    const IrSimdTypedStatus status = ir_simd_typed_operation(&row, target, &decoded);
                    const bool compare = operation >= 4;
                    ir_test_expect(tally, reason == NULL && status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.shape == (IrSimdShape)(IR_SIMD_V4I32 + width) &&
                                          decoded.lane == (element == 0 ? IR_SIMD_LANE_F32 : IR_SIMD_LANE_F64) && decoded.expand_mask == compare &&
                                          (compare ? decoded.operation == IR_SIMD_COMPARE && decoded.predicate == predicates[0][operation - 4] : decoded.operation == floating_semantics[operation]),
                                   "a floating producer operation decodes into the IR's operation of its lanes");
                }
            }
        }
    }
    // At x86-64-v3, a target without AVX-512 (#1281), the producer's rows are VEX's (c_check_program_vectors_baseline.c):
    // the 32-bit integers' addition, subtraction, multiplication and bitwise operations decode into the IR's own
    // operations at 16 and 32 bytes, their features the form's (AVX, or AVX2 for a ymm register's integers) and AVX's
    // moves', and nothing of AVX-512; the complement and the comparisons, which VEX makes of several records, stay
    // machine rows; and a 64-byte vector has no VEX producer.
    X86Target v3 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v3", 9, &v3);
    ir_test_expect(tally, parsed && (v3.features & (BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL))) == 0, "x86-64-v3 is a target without AVX-512");
    for (uint32_t width = 0; ready && parsed && width < 2; width += 1)
    {
        const TypeId type = type_vector(&table.table, BUSTER_TYPE_INT, 4u << width, false);
        for (uint32_t operation = 0; operation < sizeof(operations) / sizeof(*operations); operation += 1)
        {
            Tier0Operation row = { 0 };
            const char* reason = check_vector_lower_row(&table.table, &v3, false, operations[operation], type, BUSTER_TYPED_CLASS_VOID, &row);
            IrSimdTypedOperation decoded = { 0 };
            const IrSimdTypedStatus admitted = ir_simd_typed_row(&row, v3.features, &decoded);
            const bool own = operation < 6;
            const uint64_t expected = BUSTER_X86_FEATURE(AVX) | (width == 1 && operation < 3 ? BUSTER_X86_FEATURE(AVX2) : 0);
            ir_test_expect(tally, reason == NULL && admitted == IR_SIMD_TYPED_OK && decoded.machine == !own && (!own || (decoded.operation == semantics[operation] && decoded.shape == (IrSimdShape)(IR_SIMD_V4I32 + width) &&
                                                                                                                       !decoded.expand_mask && decoded.required_features == expected)),
                           "x86-64-v3's VEX rows decode into the IR's own operations, or stay machine rows, at 16 and 32 bytes");
            const uint64_t features[] = { BUSTER_X86_FEATURE(AVX), BUSTER_X86_FEATURE(AVX2) };
            for (uint32_t feature = 0; own && feature < BUSTER_ARRAY_LENGTH(features); feature += 1)
            {
                IrSimdTypedOperation without;
                memset(&without, 0xa5, sizeof(without));
                const IrSimdTypedOperation before = without;
                const IrSimdTypedStatus status = ir_simd_typed_row(&row, v3.features & ~features[feature], &without);
                const bool needed = (expected & features[feature]) != 0;
                ir_test_expect(tally, needed ? status == IR_SIMD_TYPED_TARGET && memcmp(&without, &before, sizeof(without)) == 0 : status == IR_SIMD_TYPED_OK,
                               "each VEX operation's features are needed, the result untouched without one");
            }
        }
    }
    // At the SSE targets (#141), the x86-64 baseline and x86-64-v2, the producer's 16-byte rows are legacy SSE's
    // (c_check_program_vectors_baseline.c), two operands, the destination's the first: every element type's addition and
    // subtraction, the 16-bit (and at x86-64-v2 the 32-bit) product, the bitwise operations and the floating arithmetic
    // decode into the IR's operations of their lanes with no feature past the target's (SSE4.1's pmulld at x86-64-v2);
    // the comparisons, the complement and the shifts (which need AVX's rows) stay machine rows or are the producer's
    // refusals; and a 32-byte vector, which such a target computes in halves, is no IR value there.
    static const TypeId sse_elements[] = { BUSTER_TYPE_SCHAR, BUSTER_TYPE_USHORT, BUSTER_TYPE_INT, BUSTER_TYPE_ULLONG, BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE };
    static const IrSimdLane sse_lanes[] = { IR_SIMD_LANE_I8, IR_SIMD_LANE_I16, IR_SIMD_LANE_I32, IR_SIMD_LANE_I64, IR_SIMD_LANE_F32, IR_SIMD_LANE_F64 };
    static const CheckOp sse_operations[] = { CHECK_OP_ADD, CHECK_OP_SUBTRACT, CHECK_OP_MULTIPLY, CHECK_OP_DIVIDE, CHECK_OP_BIT_AND, CHECK_OP_BIT_OR, CHECK_OP_BIT_XOR };
    for (uint32_t level = 0; ready && level < 2; level += 1)
    {
        X86Target sse = { .architecture = X86_ARCH_BASELINE };
        const bool sse_parsed = level == 0 || x86_target_parse("x86-64-v2", 9, &sse);
        ir_test_expect(tally, sse_parsed && (sse.features & BUSTER_X86_FEATURE(AVX)) == 0, "the SSE targets have no AVX");
        for (uint32_t element = 0; sse_parsed && element < BUSTER_ARRAY_LENGTH(sse_elements); element += 1)
        {
            const bool floating = element >= 4;
            const uint64_t element_bytes = type_size(&table.table, sse_elements[element]);
            const TypeId vector = type_vector(&table.table, sse_elements[element], (uint32_t)(16 / element_bytes), false);
            for (uint32_t operation = 0; operation < BUSTER_ARRAY_LENGTH(sse_operations); operation += 1)
            {
                const CheckOp op = sse_operations[operation];
                const bool bitwise = op == CHECK_OP_BIT_AND || op == CHECK_OP_BIT_OR || op == CHECK_OP_BIT_XOR;
                // The rows the producer makes: x86 has no byte's or quadword's product, and pmulld is SSE4.1's.
                const bool made = floating ? !bitwise : op == CHECK_OP_DIVIDE ? false : op != CHECK_OP_MULTIPLY || element == 1 || (element == 2 && level == 1);
                Tier0Operation row = { 0 };
                const char* reason = check_vector_lower_row(&table.table, &sse, false, op, vector, BUSTER_TYPED_CLASS_VOID, &row);
                if (made && reason == NULL)
                {
                    IrSimdTypedOperation decoded = { 0 };
                    const IrSimdTypedStatus status = ir_simd_typed_row(&row, sse.features, &decoded);
                    const IrSimdOperation expected = bitwise ? (op == CHECK_OP_BIT_AND ? IR_SIMD_AND : op == CHECK_OP_BIT_OR ? IR_SIMD_OR : IR_SIMD_XOR) :
                                                     op == CHECK_OP_ADD ? (floating ? IR_SIMD_FADD : IR_SIMD_ADD) : op == CHECK_OP_SUBTRACT ? (floating ? IR_SIMD_FSUB : IR_SIMD_SUB) :
                                                     op == CHECK_OP_MULTIPLY ? (floating ? IR_SIMD_FMUL : IR_SIMD_MUL) : IR_SIMD_FDIV;
                    const IrSimdLane lane = bitwise ? IR_SIMD_LANE_I32 : sse_lanes[element];
                    ir_test_expect(tally, status == IR_SIMD_TYPED_OK && !decoded.machine && decoded.operation == expected && decoded.lane == lane && decoded.shape == IR_SIMD_V4I32 &&
                                          (decoded.required_features & ~sse.features) == 0,
                                   "an SSE target's legacy rows decode into the IR's operations of their lanes, of the target's features");
                }
                else
                {
                    ir_test_expect(tally, !made || reason != NULL, "an SSE target's producer makes the rows x86 has forms for");
                }
            }
        }
        Tier0Operation wide = { 0 };
        const TypeId wide_vector = type_vector(&table.table, BUSTER_TYPE_INT, 8, false);
        const char* wide_reason = check_vector_lower_row(&table.table, &sse, false, CHECK_OP_ADD, wide_vector, BUSTER_TYPED_CLASS_VOID, &wide);
        IrSimdTypedOperation decoded = { 0 };
        ir_test_expect(tally, wide_reason != NULL || ir_simd_typed_row(&wide, sse.features, &decoded) != IR_SIMD_TYPED_OK,
                       "an SSE target's 32-byte vector is no IR value");
    }
    // At Sandy Bridge, a target with AVX but not AVX2 (#1281's review), a 32-byte vector's bitwise operations are the
    // packed singles' VEX forms, which decode into the IR's own operations with AVX alone; Tier 1 computes them by those
    // forms there (Tier1SelectHoles.avx2).
    X86Target v2 = { 0 };
    const bool avx_parsed = x86_target_parse("x86-64-v2", 9, &v2);
    const X86Target sandybridge = { .features = v2.features | BUSTER_X86_FEATURE(AVX), .architecture = X86_ARCH_OTHER };
    ir_test_expect(tally, avx_parsed && (sandybridge.features & BUSTER_X86_FEATURE(AVX2)) == 0, "x86-64-v2 with AVX, as Sandy Bridge is, has no AVX2");
    for (uint32_t operation = 3; ready && avx_parsed && operation < 6; operation += 1)
    {
        const TypeId type = type_vector(&table.table, BUSTER_TYPE_INT, 8, false);
        Tier0Operation row = { 0 };
        const char* reason = check_vector_lower_row(&table.table, &sandybridge, false, operations[operation], type, BUSTER_TYPED_CLASS_VOID, &row);
        IrSimdTypedOperation decoded = { 0 };
        const IrSimdTypedStatus admitted = ir_simd_typed_row(&row, sandybridge.features, &decoded);
        ir_test_expect(tally, reason == NULL && admitted == IR_SIMD_TYPED_OK && !decoded.machine && decoded.operation == semantics[operation] && decoded.shape == IR_SIMD_V8I32 &&
                              decoded.required_features == BUSTER_X86_FEATURE(AVX),
                       "an AVX target's 32-byte bitwise rows decode into the IR's own operations, of AVX alone");
    }
    // The typed row has no element type. Logical bit operations are identical for all integer element widths;
    // normalize those records as raw bits; the arithmetic and comparisons decode at their lanes (#141's element types),
    // 8-, 16- or 64-bit, signed and unsigned comparisons by their forms.
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
                static const IrSimdLane lanes[] = { IR_SIMD_LANE_I8, IR_SIMD_LANE_I8, IR_SIMD_LANE_I16, IR_SIMD_LANE_I16, IR_SIMD_LANE_I64, IR_SIMD_LANE_I64 };
                const bool signed_element = (element & 1) == 0;
                const IrSimdOperation arithmetic = operation == 4 ? IR_SIMD_ADD : operation == 5 ? IR_SIMD_SUB : IR_SIMD_COMPARE;
                ir_test_expect(tally, reason == NULL && normalized_reason == NULL && status == IR_SIMD_TYPED_OK && !result.machine && result.shape == (IrSimdShape)(IR_SIMD_V4I32 + width) &&
                                      (bitwise ? !result.expand_mask && result.lane == IR_SIMD_LANE_I32 && memcmp(&row, &canonical, sizeof(row)) == 0 :
                                                 result.operation == arithmetic && result.lane == lanes[element] && result.expand_mask == (arithmetic == IR_SIMD_COMPARE) &&
                                                     (arithmetic != IR_SIMD_COMPARE || result.predicate == (signed_element ? IR_SIMD_LT : IR_SIMD_ULT))),
                               "integer bitwise rows normalize exactly; other integer lanes' arithmetic and comparisons decode at their lanes");
            }
        }
    }
    IrSimdTypedOperation decoded = { 0 };
    ir_test_expect(tally, ir_simd_typed_operation(NULL, target, &decoded) == IR_SIMD_TYPED_INVALID && ir_simd_typed_row(NULL, target, &decoded) == IR_SIMD_TYPED_INVALID,
                   "missing typed operation input is rejected before indexing a record");
    ir_simd_typed_test_masks(tally);
    ir_simd_typed_test_intrinsics(tally);
    ir_simd_typed_test_bits(tally);
    ir_simd_typed_test_overflows(tally);
    ir_simd_typed_test_compare_masks(tally);
    type_test_table_close(&table);
}
