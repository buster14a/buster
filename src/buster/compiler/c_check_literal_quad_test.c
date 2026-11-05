// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original binary128 literal fixtures for #1250. Included by the literal warning tests in test-checker.
// The expected words follow the binary128 encoding, not strtof128 or a host long double conversion.

static void check_test_literal_quad(CheckTest* test, Arena* scratch)
{
#if BUSTER_CHECK_HAVE_QUAD_READER
    const struct
    {
        const char* spelling;
        uint64_t low;
        uint64_t high;
        uint8_t range;
        bool imaginary;
    } exact[] =
    {
        { "0.0q", 0, 0, CHECK_LITERAL_RANGE_NONE, false },
        { "1.5q", 0, UINT64_C(0x3fff800000000000), CHECK_LITERAL_RANGE_NONE, false },
        { "1.5Q", 0, UINT64_C(0x3fff800000000000), CHECK_LITERAL_RANGE_NONE, false },
        { "0x1.8p0Q", 0, UINT64_C(0x3fff800000000000), CHECK_LITERAL_RANGE_NONE, false },
        { "1'2.5q", 0, UINT64_C(0x4002900000000000), CHECK_LITERAL_RANGE_NONE, false },
        { "0x1.0000000000000002p0q", UINT64_C(0x0002000000000000), UINT64_C(0x3fff000000000000), CHECK_LITERAL_RANGE_NONE, false },
        { "0x1.fffffffffffffffep16383Q", UINT64_C(0xfffe000000000000), UINT64_C(0x7ffeffffffffffff), CHECK_LITERAL_RANGE_NONE, false },
        { "0x1p-16382q", 0, UINT64_C(0x0001000000000000), CHECK_LITERAL_RANGE_NONE, false },
        { "0x1p-16445Q", UINT64_C(0x0002000000000000), 0, CHECK_LITERAL_RANGE_NONE, false },
        // The last hex digit is at place -116: 8 is the midpoint above 1 in binary128, not an x87 midpoint.
        { "0x1.00000000000000000000000000008p0Q", 0, UINT64_C(0x3fff000000000000), CHECK_LITERAL_RANGE_NONE, false },
        { "0x1.00000000000000000000000000007fffffffffp0q", 0, UINT64_C(0x3fff000000000000), CHECK_LITERAL_RANGE_NONE, false },
        { "1e5000q", 0, UINT64_C(0x7fff000000000000), CHECK_LITERAL_RANGE_OVERFLOW, false },
        { "1e-5000Q", 0, 0, CHECK_LITERAL_RANGE_UNDERFLOW, false },
        { "0x1p-16495Q", 0, 0, CHECK_LITERAL_RANGE_UNDERFLOW, false },
        { "0e-99999q", 0, 0, CHECK_LITERAL_RANGE_NONE, false },
        { "1.5qi", 0, UINT64_C(0x3fff800000000000), CHECK_LITERAL_RANGE_NONE, true },
        { "1.5iQ", 0, UINT64_C(0x3fff800000000000), CHECK_LITERAL_RANGE_NONE, true },
        { "1.5QJ", 0, UINT64_C(0x3fff800000000000), CHECK_LITERAL_RANGE_NONE, true },
        { "1.5Jq", 0, UINT64_C(0x3fff800000000000), CHECK_LITERAL_RANGE_NONE, true },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(exact); i += 1)
    {
        const uint64_t mark = scratch->length;
        CheckLiteral literal = { 0 };
        check_literal_number((const uint8_t*)exact[i].spelling, (uint32_t)strlen(exact[i].spelling), scratch, &literal);
        const TypeId type = exact[i].imaginary ? BUSTER_TYPE_COMPLEX(type_index(BUSTER_TYPE_FLOAT128)) : BUSTER_TYPE_FLOAT128;
        const CheckValue value = check_literal_value(literal);
        CheckValue copied = { .type = type };
        check_value_copy_floating(&copied, &value);
        uint64_t words[2] = { 0 };
        check_action_quad_bits(exact[i].imaginary ? copied.imaginary : copied.floating, words);
        CheckWarningEvent event = { 0 };
        const bool warned = check_literal_warning_event(&literal, 17, &event);
        char description[192];
        snprintf(description, sizeof(description), "quad literal %s preserves its binary128 words through handoff", exact[i].spelling);
        check_test_expect(test, literal.valid && !literal.resource && literal.real && literal.imaginary == exact[i].imaginary &&
                          literal.type == type && value.type == type && literal.range == exact[i].range &&
                          value.constant_kind == (exact[i].imaginary ? CHECK_CONSTANT_COMPLEX : CHECK_CONSTANT_FLOATING) &&
                          words[0] == exact[i].low && words[1] == exact[i].high && scratch->length == mark &&
                          warned == (exact[i].range != CHECK_LITERAL_RANGE_NONE) &&
                          (!warned || event.payload.first_type == BUSTER_TYPE_FLOAT128), description);
        if (!exact[i].imaginary && literal.valid)
        {
            CheckValue negative = { .type = type };
            check_action_fold_floating(CHECK_OP_NEGATIVE, &value, &value, &negative, 0);
            check_action_quad_bits(negative.floating, words);
            check_test_expect(test, negative.constant_kind == CHECK_CONSTANT_FLOATING && negative.fault == CHECK_FAULT_NONE &&
                              words[0] == exact[i].low && words[1] == (exact[i].high ^ (UINT64_C(1) << 63)),
                              "quad literal negation changes only the sign, including zero and infinity");
        }
    }
    const char* refused[] =
    {
        // Valid binary128 constants whose rounded value the checker's current x87 storage cannot preserve.
        "0.1Q",
        "0x1.0000000000000001p0q",
        "0x1.0000000000000000000000000001p0Q",
        "0x1.0000000000000000000000000000800000000001p0Q",
        "0x1.ffffffffffffffffffffffffffffp16383Q",
        "0x1p-16446Q",
        "0x1p-16494q",
        "0x1.0000000000000000000000000001p-16495Q",
        "0.1iQ",
        // Q is a floating suffix, not an integer suffix or a second format specifier.
        "1Q", "0x1q", "0x1.0q", "1.5qq", "1.5Qf", "1.5FQ", "1.5QL", "1.5iqj", "1'.5q", "1.5'q",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refused); i += 1)
    {
        const uint64_t mark = scratch->length;
        CheckLiteral literal = { 0 };
        check_literal_number((const uint8_t*)refused[i], (uint32_t)strlen(refused[i]), scratch, &literal);
        CheckWarningEvent event = { 0 };
        char description[192];
        snprintf(description, sizeof(description), "quad literal %s is refused rather than silently narrowed", refused[i]);
        check_test_expect(test, !literal.valid && !literal.resource && scratch->length == mark &&
                          !check_literal_warning_event(&literal, 17, &event), description);
    }
#else
    CheckLiteral literal = { 0 };
    const uint64_t mark = scratch->length;
    static const char spelling[] = "1.5q";
    check_literal_number((const uint8_t*)spelling, sizeof(spelling) - 1, scratch, &literal);
    check_test_expect(test, !literal.valid && !literal.resource && scratch->length == mark,
                      "quad literals are refused without an exact binary128 reader");
#endif
}
