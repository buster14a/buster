#include <buster/tests/compiler/assembly/aarch64_system_semantics_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_system_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL Target a64_system_test_m1_target(void)
{
    return (Target){
        .cpu_arch = CPU_ARCH_AARCH64,
        .cpu_model = CPU_MODEL_A64_APPLE_M1,
        .os = OPERATING_SYSTEM_MACOS,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_default(CPU_ARCH_AARCH64, CPU_MODEL_A64_APPLE_M1),
    };
}

BUSTER_GLOBAL_LOCAL BusterAarch64SystemInstruction a64_system_test_fixture(u32 row)
{
    BusterAarch64SystemSemanticRecord metadata = {0};
    BusterAarch64SystemInstruction result = {.row = (u16)row};
    if (buster_aarch64_system_semantic_row(row, &metadata))
    {
        result.field_count = metadata.field_count;
        for (u32 field = 0; field < metadata.field_count; field += 1)
        {
            BusterAarch64SystemFieldSchema schema = {0};
            buster_aarch64_system_semantic_field(row, field, &schema);
            result.fields[field] = (BusterAarch64SystemOperandValue){.kind = schema.kind, .width = schema.width, .value = schema.minimum};
        }
        if (row == BUSTER_AARCH64_SYSTEM_FORM_HINT)
        {
            result.fields[0].value = 0;
            result.fields[1].value = 0;
        }
        else if (row == BUSTER_AARCH64_SYSTEM_FORM_MSR_PSTATE)
        {
            result.fields[0].value = 5; // SPSel selector
            result.fields[1].value = 0;
            result.fields[2].value = 0;
        }
        else if (row == BUSTER_AARCH64_SYSTEM_FORM_SYS)
        {
            result.fields[0].value = 31;
            result.defaulted_mask = 1;
        }
        else if (row == BUSTER_AARCH64_SYSTEM_FORM_CLREX || row == BUSTER_AARCH64_SYSTEM_FORM_ISB)
        {
            result.fields[0].value = 15;
            result.defaulted_mask = 1;
        }
        else if (row == BUSTER_AARCH64_SYSTEM_FORM_DMB || row == BUSTER_AARCH64_SYSTEM_FORM_DSB)
        {
            // CRm=15 (`sy`) is the independent LLVM representative for both
            // barriers; DSB's #0/#4 aliases remain exercised below.
            result.fields[0].value = 15;
        }
    }

    return result;
}

UnitTestResult aarch64_system_semantics_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    Target m1 = a64_system_test_m1_target();
    BUSTER_TEST(arguments, buster_aarch64_system_semantic_count() == 18);
    u32 fixed_system_canonical_count = 0;
    for (u32 fixed_index = 0; fixed_index < buster_aarch64_arm_m1_fixed_spelling_count(); fixed_index += 1)
    {
        BusterAarch64ArmM1FixedSpelling fixed = {0};
        BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_spelling(fixed_index, &fixed));
        fixed_system_canonical_count += fixed.system && fixed.canonical;
    }
    BUSTER_TEST(arguments, fixed_system_canonical_count == buster_aarch64_system_semantic_fixed_canonical_count() &&
                           fixed_system_canonical_count == 15);
    BUSTER_TEST(arguments, buster_aarch64_system_semantic_canonical_count() == buster_aarch64_system_semantic_count() + fixed_system_canonical_count &&
                           buster_aarch64_system_semantic_canonical_count() == 33);
    BUSTER_TEST(arguments, buster_aarch64_system_semantic_validate());
    BUSTER_STRING_TEST(arguments, string_from_pointer((char8*)buster_aarch64_system_semantic_digest()),
                       S8("18c9e62f0ab26bca5192dafd7cc05c2956d0ba6be7519e82159de1640f071e81"));
    BUSTER_TEST(arguments, buster_aarch64_system_semantic_target_supported(m1));
    Target generic_target = m1;
    generic_target.cpu_model = CPU_MODEL_A64_GENERIC;
    generic_target.cpu_features_explicit = false;
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_target_supported(generic_target));
    Target x86_target = m1;
    x86_target.cpu_arch = CPU_ARCH_X86_64;
    x86_target.cpu_model = CPU_MODEL_A64_GENERIC;
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_target_supported(x86_target));

    for (u32 row = 0; row < buster_aarch64_system_semantic_count(); row += 1)
    {
        BusterAarch64SystemSemanticRecord metadata = {0};
        BUSTER_TEST(arguments, buster_aarch64_system_semantic_row(row, &metadata) && metadata.form == row && metadata.field_count > 0);
        if (row == BUSTER_AARCH64_SYSTEM_FORM_DMB)
        {
            BUSTER_TEST(arguments, metadata.constraint_field == 0 && metadata.constraint_mask == UINT16_C(0xeeee));
        }
        else if (row == BUSTER_AARCH64_SYSTEM_FORM_DSB)
        {
            BUSTER_TEST(arguments, metadata.constraint_field == 0 && metadata.constraint_mask == UINT16_C(0xeeff));
        }
        else if (row == BUSTER_AARCH64_SYSTEM_FORM_ISB)
        {
            BUSTER_TEST(arguments, metadata.constraint_field == 0 && metadata.constraint_mask == UINT16_C(0x8000));
        }
        else
        {
            BUSTER_TEST(arguments, metadata.constraint_field == UINT8_MAX && metadata.constraint_mask == 0);
        }
        BusterAarch64SystemInstruction fixture = a64_system_test_fixture(row);
        u32 word = UINT32_C(0xfeedface);
        BUSTER_TEST(arguments, buster_aarch64_system_semantic_encode(m1, &fixture, &word));
        static u32 const llvm_oracle_words[BUSTER_AARCH64_SYSTEM_SEMANTIC_ROW_COUNT] = {
            UINT32_C(0xd4200000), UINT32_C(0xd5033f5f), UINT32_C(0xd4a00001), UINT32_C(0xd4a00002), UINT32_C(0xd4a00003),
            UINT32_C(0xd5033fbf), UINT32_C(0xd5033f9f), UINT32_C(0xd503201f), UINT32_C(0xd4400000), UINT32_C(0xd4000002),
            UINT32_C(0xd5033fdf), UINT32_C(0xd5300000), UINT32_C(0xd50040bf), UINT32_C(0xd5100000), UINT32_C(0xd4000003),
            UINT32_C(0xd4000001), UINT32_C(0xd5280000), UINT32_C(0xd508001f),
        };
        if (row == BUSTER_AARCH64_SYSTEM_FORM_DMB || row == BUSTER_AARCH64_SYSTEM_FORM_DSB)
        {
            // LLVM's option spelling `#15` canonicalizes to SY.
            BusterAarch64SystemInstruction oracle_fixture = fixture;
            oracle_fixture.defaulted_mask = 0;
            oracle_fixture.fields[0].value = 15;
            u32 oracle_word = 0;
            BUSTER_TEST(arguments, buster_aarch64_system_semantic_encode(m1, &oracle_fixture, &oracle_word) &&
                                   oracle_word == (row == BUSTER_AARCH64_SYSTEM_FORM_DMB ? UINT32_C(0xd5033fbf) : UINT32_C(0xd5033f9f)));
        }
        if (row != BUSTER_AARCH64_SYSTEM_FORM_DMB && row != BUSTER_AARCH64_SYSTEM_FORM_DSB)
        {
            BUSTER_TEST(arguments, word == llvm_oracle_words[row]);
        }
        BusterAarch64SystemInstruction decoded = {0};
        BUSTER_TEST(arguments, buster_aarch64_system_semantic_decode_form(m1, row, word, &decoded) && decoded.row == row &&
                               decoded.field_count == fixture.field_count && decoded.defaulted_mask == fixture.defaulted_mask);
        for (u32 field = 0; field < fixture.field_count; field += 1)
        {
            BUSTER_TEST(arguments, decoded.fields[field].kind == fixture.fields[field].kind &&
                                   decoded.fields[field].width == fixture.fields[field].width &&
                                   decoded.fields[field].value == fixture.fields[field].value);
        }
        u32 round_trip = 0;
        BUSTER_TEST(arguments, buster_aarch64_system_semantic_encode(m1, &decoded, &round_trip) && round_trip == word);
        BusterAarch64SystemInstruction word_first = {.row = UINT16_MAX, .field_count = UINT8_MAX};
        if (row == BUSTER_AARCH64_SYSTEM_FORM_HINT)
        {
            BUSTER_TEST(arguments, !buster_aarch64_system_semantic_decode(m1, word, &word_first) &&
                                   word_first.row == UINT16_MAX && word_first.field_count == UINT8_MAX);
        }
        else
        {
            BUSTER_TEST(arguments, buster_aarch64_system_semantic_decode(m1, word, &word_first) && word_first.row == row &&
                                   word_first.field_count == fixture.field_count);
            for (u32 field = 0; field < fixture.field_count; field += 1)
            {
                BUSTER_TEST(arguments, word_first.fields[field].kind == decoded.fields[field].kind &&
                                       word_first.fields[field].width == decoded.fields[field].width &&
                                       word_first.fields[field].value == decoded.fields[field].value);
            }
        }

        for (u32 field = 0; field < metadata.field_count; field += 1)
        {
            BusterAarch64SystemFieldSchema schema = {0};
            BUSTER_TEST(arguments, buster_aarch64_system_semantic_field(row, field, &schema));
            BusterAarch64SystemInstruction boundary = fixture;
            boundary.defaulted_mask = 0;
            boundary.fields[field].value = schema.maximum;
            if ((row == BUSTER_AARCH64_SYSTEM_FORM_HINT && field == 0) ||
                (row == BUSTER_AARCH64_SYSTEM_FORM_HINT && field == 1))
            {
                // HINT #127 is reserved; exercise its transactionality below.
                continue;
            }
            if (row == BUSTER_AARCH64_SYSTEM_FORM_MSR_PSTATE && field == 0) boundary.fields[field].value = 7;
            u32 boundary_word = UINT32_C(0xa5a5a5a5);
            BUSTER_TEST(arguments, buster_aarch64_system_semantic_encode(m1, &boundary, &boundary_word));
            BusterAarch64SystemInstruction boundary_decoded = {0};
            BUSTER_TEST(arguments, buster_aarch64_system_semantic_decode(m1, boundary_word, &boundary_decoded) && boundary_decoded.row == row);
        }
    }

    // Barrier option constraints are checked exhaustively at both encode and
    // form-directed decode boundaries.  DSB #0/#4 are allocated aliases whose
    // words are owned by fixed canonical SSBB/PSSBB rows in word-first mode.
    static u32 const barrier_rows[] = {
        BUSTER_AARCH64_SYSTEM_FORM_DMB,
        BUSTER_AARCH64_SYSTEM_FORM_DSB,
        BUSTER_AARCH64_SYSTEM_FORM_ISB,
    };
    static u16 const barrier_masks[] = {UINT16_C(0xeeee), UINT16_C(0xeeff), UINT16_C(0x8000)};
    for (u32 barrier_index = 0; barrier_index < BUSTER_ARRAY_LENGTH(barrier_rows); barrier_index += 1)
    {
        u32 row = barrier_rows[barrier_index];
        BusterAarch64SystemInstruction barrier = a64_system_test_fixture(row);
        barrier.defaulted_mask = 0;
        for (u32 option = 0; option < 16; option += 1)
        {
            barrier.fields[0].value = option;
            bool allocated = (barrier_masks[barrier_index] & (UINT16_C(1) << option)) != 0;
            u32 encoded = UINT32_C(0x2468ace0);
            bool encoded_ok = buster_aarch64_system_semantic_encode(m1, &barrier, &encoded);
            BUSTER_TEST(arguments, encoded_ok == allocated);
            if (!allocated)
            {
                BUSTER_TEST(arguments, encoded == UINT32_C(0x2468ace0));
                BusterAarch64SystemInstruction rejected = {.row = UINT16_MAX, .field_count = UINT8_MAX, .defaulted_mask = UINT8_MAX};
                BUSTER_TEST(arguments, !buster_aarch64_system_semantic_decode_form(m1, row,
                                                                                     UINT32_C(0xd5033000) | (option << 8) |
                                                                                         (row == BUSTER_AARCH64_SYSTEM_FORM_DMB ? UINT32_C(0xbf) :
                                                                                          row == BUSTER_AARCH64_SYSTEM_FORM_DSB ? UINT32_C(0x9f) : UINT32_C(0xdf)),
                                                                                     &rejected) &&
                                       rejected.row == UINT16_MAX && rejected.field_count == UINT8_MAX &&
                                       rejected.defaulted_mask == UINT8_MAX);
                continue;
            }
            BusterAarch64SystemInstruction decoded = {.row = UINT16_MAX, .field_count = UINT8_MAX, .defaulted_mask = UINT8_MAX};
            BUSTER_TEST(arguments, buster_aarch64_system_semantic_decode_form(m1, row, encoded, &decoded) && decoded.row == row &&
                                   decoded.fields[0].value == option);
            u32 round_trip = UINT32_C(0);
            BUSTER_TEST(arguments, buster_aarch64_system_semantic_encode(m1, &decoded, &round_trip) && round_trip == encoded);

            BusterAarch64SystemInstruction word_first = {.row = UINT16_MAX, .field_count = UINT8_MAX, .defaulted_mask = UINT8_MAX};
            bool alias = row == BUSTER_AARCH64_SYSTEM_FORM_DSB && (option == 0 || option == 4);
            if (alias)
            {
                BusterAarch64ArmM1FixedSpelling fixed_alias = {0};
                String8 alias_name = option == 0 ? S8("SSBB") : S8("PSSBB");
                BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_lookup(alias_name, &fixed_alias) && fixed_alias.word == encoded &&
                                       fixed_alias.system && fixed_alias.alias && !fixed_alias.canonical);
                BUSTER_TEST(arguments, buster_aarch64_system_semantic_decode(m1, encoded, &word_first) &&
                                       word_first.row == row && word_first.fields[0].value == option);
            }
            else
            {
                BUSTER_TEST(arguments, buster_aarch64_system_semantic_decode(m1, encoded, &word_first) &&
                                       word_first.row == row && word_first.fields[0].value == option);
            }
        }
    }

    // CLREX is the unconstrained four-bit barrier form: every CRm value
    // remains allocated and round-trips through both decode paths.
    BusterAarch64SystemInstruction clrex = a64_system_test_fixture(BUSTER_AARCH64_SYSTEM_FORM_CLREX);
    clrex.defaulted_mask = 0;
    for (u32 option = 0; option < 16; option += 1)
    {
        clrex.fields[0].value = option;
        u32 encoded = UINT32_C(0x2468ace0);
        BUSTER_TEST(arguments, buster_aarch64_system_semantic_encode(m1, &clrex, &encoded));
        BusterAarch64SystemInstruction decoded = {0};
        BUSTER_TEST(arguments, buster_aarch64_system_semantic_decode_form(m1, BUSTER_AARCH64_SYSTEM_FORM_CLREX, encoded, &decoded) &&
                               decoded.fields[0].value == option);
        BusterAarch64SystemInstruction word_first = {0};
        BUSTER_TEST(arguments, buster_aarch64_system_semantic_decode(m1, encoded, &word_first) &&
                               word_first.row == BUSTER_AARCH64_SYSTEM_FORM_CLREX && word_first.fields[0].value == option);
    }

    // MRS/MSR use Arm's one-bit o0 field while S<op0> uses 2/3.
    u32 o0 = UINT32_MAX, op0 = UINT32_MAX;
    BUSTER_TEST(arguments, buster_aarch64_system_op0_encode(2, &o0) && o0 == 0 && buster_aarch64_system_op0_decode(o0, &op0) && op0 == 2);
    BUSTER_TEST(arguments, buster_aarch64_system_op0_encode(3, &o0) && o0 == 1 && buster_aarch64_system_op0_decode(o0, &op0) && op0 == 3);
    BUSTER_TEST(arguments, !buster_aarch64_system_op0_encode(1, &o0) && !buster_aarch64_system_op0_decode(2, &op0));

    // HINT reserved and feature-ineligible values fail without changing the output.
    BusterAarch64SystemInstruction hint = a64_system_test_fixture(BUSTER_AARCH64_SYSTEM_FORM_HINT);
    u32 hint_word = UINT32_C(0x13579bdf);
    hint.fields[0].value = 2;
    hint.fields[1].value = 0; // immediate #2, WFE
    BUSTER_TEST(arguments, buster_aarch64_system_semantic_encode(m1, &hint, &hint_word));
    u32 unchanged = UINT32_C(0x2468ace0);
    hint.fields[0].value = 6;
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_encode(m1, &hint, &unchanged) && unchanged == UINT32_C(0x2468ace0));
    hint.fields[0].value = 1;
    hint.fields[1].value = 1; // immediate #9 is reserved in the canonical Arm inventory
    unchanged = UINT32_C(0x2468ace0);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_encode(m1, &hint, &unchanged) && unchanged == UINT32_C(0x2468ace0));
    Target no_pauth = m1;
    no_pauth.cpu_features = target_cpu_features_remove(no_pauth.cpu_features, TARGET_CPU_FEATURE_AARCH64_PAUTH);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(no_pauth));
    hint.fields[0].value = 7;
    hint.fields[1].value = 0;
    unchanged = UINT32_C(0x2468ace0);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_encode(no_pauth, &hint, &unchanged) && unchanged == UINT32_C(0x2468ace0));

    // The Arm MSR PSTATE reservation is a cross-field constraint.
    BusterAarch64SystemInstruction pstate = a64_system_test_fixture(BUSTER_AARCH64_SYSTEM_FORM_MSR_PSTATE);
    pstate.defaulted_mask = 0;
    pstate.fields[0].value = 2;
    u32 pstate_unchanged = UINT32_C(0x2468ace0);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_encode(m1, &pstate, &pstate_unchanged) &&
                           pstate_unchanged == UINT32_C(0x2468ace0));

    // Invalid fixed bits, invalid output pointers, and decode failure are transactional.
    BusterAarch64SystemInstruction decode_unchanged = {.row = UINT16_MAX, .field_count = UINT8_MAX, .defaulted_mask = UINT8_MAX};
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_decode(m1, UINT32_MAX, &decode_unchanged) &&
                           decode_unchanged.row == UINT16_MAX && decode_unchanged.field_count == UINT8_MAX &&
                           decode_unchanged.defaulted_mask == UINT8_MAX);
    BusterAarch64SystemInstruction invalid = a64_system_test_fixture(BUSTER_AARCH64_SYSTEM_FORM_BRK);
    u32 invalid_word = UINT32_C(0x2468ace0);
    invalid.fields[0].value = 65536;
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_encode(m1, &invalid, &invalid_word) && invalid_word == UINT32_C(0x2468ace0));
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_encode(m1, &invalid, 0));
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_decode(m1, UINT32_C(0xd4200000), 0));

    // Public metadata helpers reject malformed spans/indices without touching
    // caller-owned outputs, and conversion helpers retain sentinels on error.
    BusterAarch64SystemSemanticRecord row_unchanged = {.form = UINT8_MAX, .field_count = UINT8_MAX};
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_row(UINT32_MAX, &row_unchanged) &&
                           row_unchanged.form == UINT8_MAX && row_unchanged.field_count == UINT8_MAX);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_row(0, 0));
    BusterAarch64SystemFieldSchema field_unchanged = {.kind = UINT8_MAX, .width = UINT8_MAX};
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_field(UINT32_MAX, 0, &field_unchanged) &&
                           field_unchanged.kind == UINT8_MAX && field_unchanged.width == UINT8_MAX);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_field(0, UINT32_MAX, &field_unchanged) &&
                           field_unchanged.kind == UINT8_MAX && field_unchanged.width == UINT8_MAX);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_field(0, 0, 0));
    char8 string_sentinel[] = "sentinel";
    String8 string_unchanged = {.pointer = string_sentinel, .length = 8};
    BusterAarch64SystemString malformed_string = {.offset = UINT32_MAX, .length = 1};
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_string(malformed_string, &string_unchanged) &&
                           string_unchanged.pointer == string_sentinel && string_unchanged.length == 8);
    BUSTER_TEST(arguments, buster_aarch64_system_semantic_string_byte(malformed_string, 0) == 0);
    u32 lookup_unchanged = UINT32_MAX;
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_lookup((String8){.pointer = 0, .length = 1}, &lookup_unchanged) &&
                           lookup_unchanged == UINT32_MAX);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_lookup((String8){.pointer = (char8*)"", .length = 0}, &lookup_unchanged) &&
                           lookup_unchanged == UINT32_MAX);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_lookup(S8("not-a-row"), &lookup_unchanged) && lookup_unchanged == UINT32_MAX);
    u32 conversion_unchanged = UINT32_MAX;
    BUSTER_TEST(arguments, !buster_aarch64_system_op0_encode(1, &conversion_unchanged) && conversion_unchanged == UINT32_MAX);
    BUSTER_TEST(arguments, !buster_aarch64_system_op0_decode(2, &conversion_unchanged) && conversion_unchanged == UINT32_MAX);
    BUSTER_TEST(arguments, !buster_aarch64_system_op0_encode(2, 0) && !buster_aarch64_system_op0_decode(0, 0));

    // Operand shape is part of the typed contract: counts, defaults, kinds,
    // widths, and reserved bytes are all rejected transactionally.
    BusterAarch64SystemInstruction malformed = a64_system_test_fixture(BUSTER_AARCH64_SYSTEM_FORM_BRK);
    u32 malformed_unchanged = UINT32_C(0x2468ace0);
    malformed.field_count = 0;
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_encode(m1, &malformed, &malformed_unchanged) &&
                           malformed_unchanged == UINT32_C(0x2468ace0));
    malformed = a64_system_test_fixture(BUSTER_AARCH64_SYSTEM_FORM_BRK);
    malformed.defaulted_mask = UINT8_C(0x80);
    malformed_unchanged = UINT32_C(0x2468ace0);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_encode(m1, &malformed, &malformed_unchanged) &&
                           malformed_unchanged == UINT32_C(0x2468ace0));
    malformed = a64_system_test_fixture(BUSTER_AARCH64_SYSTEM_FORM_BRK);
    malformed.fields[0].kind = BUSTER_AARCH64_SYSTEM_FIELD_REGISTER;
    malformed_unchanged = UINT32_C(0x2468ace0);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_encode(m1, &malformed, &malformed_unchanged) &&
                           malformed_unchanged == UINT32_C(0x2468ace0));
    malformed = a64_system_test_fixture(BUSTER_AARCH64_SYSTEM_FORM_BRK);
    malformed.fields[0].width = 15;
    malformed_unchanged = UINT32_C(0x2468ace0);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_encode(m1, &malformed, &malformed_unchanged) &&
                           malformed_unchanged == UINT32_C(0x2468ace0));
    malformed = a64_system_test_fixture(BUSTER_AARCH64_SYSTEM_FORM_BRK);
    malformed.fields[0].reserved[0] = 1;
    malformed_unchanged = UINT32_C(0x2468ace0);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_encode(m1, &malformed, &malformed_unchanged) &&
                           malformed_unchanged == UINT32_C(0x2468ace0));
    BusterAarch64SystemInstruction invalid_target_instruction = a64_system_test_fixture(BUSTER_AARCH64_SYSTEM_FORM_BRK);
    u32 invalid_target_word = UINT32_C(0x2468ace0);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_encode(generic_target, &invalid_target_instruction, &invalid_target_word) &&
                           invalid_target_word == UINT32_C(0x2468ace0));
    BusterAarch64SystemInstruction invalid_target_decode = {.row = UINT16_MAX, .field_count = UINT8_MAX};
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_decode(generic_target, UINT32_C(0xd4200000), &invalid_target_decode) &&
                           invalid_target_decode.row == UINT16_MAX && invalid_target_decode.field_count == UINT8_MAX);
    BusterAarch64SystemInstruction invalid_form_decode = {.row = UINT16_MAX, .field_count = UINT8_MAX};
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_decode_form(m1, BUSTER_AARCH64_SYSTEM_SEMANTIC_ROW_COUNT,
                                                                        UINT32_C(0xd4200000), &invalid_form_decode) &&
                           invalid_form_decode.row == UINT16_MAX && invalid_form_decode.field_count == UINT8_MAX);
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_decode_form(m1, BUSTER_AARCH64_SYSTEM_FORM_BRK,
                                                                        UINT32_C(0xd4200001), &invalid_form_decode) &&
                           invalid_form_decode.row == UINT16_MAX && invalid_form_decode.field_count == UINT8_MAX);
    BusterAarch64SystemInstruction corrupted_decode = {.row = UINT16_MAX, .field_count = UINT8_MAX};
    BUSTER_TEST(arguments, !buster_aarch64_system_semantic_decode(m1, UINT32_C(0xd4200001), &corrupted_decode) &&
                           corrupted_decode.row == UINT16_MAX && corrupted_decode.field_count == UINT8_MAX);

    // Exact fixed spellings own their words in canonical word-first decoding.
    // The form-directed decoder still round-trips HINT #imm for the nine fixed
    // system spellings that share its pattern.
    u32 hint_overlap_count = 0;
    for (u32 fixed_index = 0; fixed_index < buster_aarch64_arm_m1_fixed_spelling_count(); fixed_index += 1)
    {
        BusterAarch64ArmM1FixedSpelling fixed = {0};
        BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_spelling(fixed_index, &fixed));
        if (!fixed.system || !fixed.canonical || (fixed.word & UINT32_C(0xfffff01f)) != UINT32_C(0xd503201f)) continue;
        hint_overlap_count += 1;
        BusterAarch64ArmM1FixedSpelling fixed_by_name = {0};
        BUSTER_TEST(arguments, buster_aarch64_arm_m1_fixed_lookup(fixed.spelling, &fixed_by_name) &&
                               fixed_by_name.word == fixed.word && fixed_by_name.canonical && fixed_by_name.system);
        BusterAarch64CanonicalDecodeResult canonical = {0};
        BUSTER_TEST(arguments, buster_aarch64_canonical_decode(m1, fixed.word, &canonical) == BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS &&
                               canonical.arm_row_digest == fixed.arm_row_digest);
        BusterAarch64SystemInstruction overlap = {.row = UINT16_MAX, .field_count = UINT8_MAX};
        BUSTER_TEST(arguments, !buster_aarch64_system_semantic_decode(m1, fixed.word, &overlap) &&
                               overlap.row == UINT16_MAX && overlap.field_count == UINT8_MAX);
        BUSTER_TEST(arguments, buster_aarch64_system_semantic_decode_form(m1, BUSTER_AARCH64_SYSTEM_FORM_HINT, fixed.word, &overlap) &&
                               overlap.row == BUSTER_AARCH64_SYSTEM_FORM_HINT && overlap.field_count == 2);
        u32 overlap_word = 0;
        BUSTER_TEST(arguments, buster_aarch64_system_semantic_encode(m1, &overlap, &overlap_word) && overlap_word == fixed.word);
    }
    BUSTER_TEST(arguments, hint_overlap_count == 9);
    return result;
}

#endif
