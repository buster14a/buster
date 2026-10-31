// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source-format boundaries, independently checked with Clang 23. Include after c_checker_test.c's helpers.

typedef struct CheckTestLiteralRange CheckTestLiteralRange;
struct CheckTestLiteralRange
{
    const char* spelling;
    TypeId type;
    uint8_t range;
};

static const CheckTestLiteralRange check_test_literal_range_cases[] =
{
    { "0", BUSTER_TYPE_INT, CHECK_LITERAL_RANGE_NONE },
    { "0.0", BUSTER_TYPE_DOUBLE, CHECK_LITERAL_RANGE_NONE },
    { "0e-99999f", BUSTER_TYPE_FLOAT, CHECK_LITERAL_RANGE_NONE },
    { "0x0.0p-99999L", BUSTER_TYPE_LONG_DOUBLE, CHECK_LITERAL_RANGE_NONE },
    { "1e40", BUSTER_TYPE_DOUBLE, CHECK_LITERAL_RANGE_NONE },
    { "1e-46", BUSTER_TYPE_DOUBLE, CHECK_LITERAL_RANGE_NONE },
    { "1e40f", BUSTER_TYPE_FLOAT, CHECK_LITERAL_RANGE_OVERFLOW },
    { "1e-40f", BUSTER_TYPE_FLOAT, CHECK_LITERAL_RANGE_NONE },
    { "1e-45f", BUSTER_TYPE_FLOAT, CHECK_LITERAL_RANGE_NONE },
    { "1e-46f", BUSTER_TYPE_FLOAT, CHECK_LITERAL_RANGE_UNDERFLOW },
    { "0x1.fffffep127f", BUSTER_TYPE_FLOAT, CHECK_LITERAL_RANGE_NONE },
    { "0x1.ffffffp127f", BUSTER_TYPE_FLOAT, CHECK_LITERAL_RANGE_OVERFLOW },
    { "0x1p-149f", BUSTER_TYPE_FLOAT, CHECK_LITERAL_RANGE_NONE },
    { "0x1p-150f", BUSTER_TYPE_FLOAT, CHECK_LITERAL_RANGE_UNDERFLOW },
    { "0x1.000002p-150f", BUSTER_TYPE_FLOAT, CHECK_LITERAL_RANGE_NONE },
    { "1e400", BUSTER_TYPE_DOUBLE, CHECK_LITERAL_RANGE_OVERFLOW },
    { "1e-400", BUSTER_TYPE_DOUBLE, CHECK_LITERAL_RANGE_UNDERFLOW },
    { "0x1.fffffffffffffp1023", BUSTER_TYPE_DOUBLE, CHECK_LITERAL_RANGE_NONE },
    { "0x1.fffffffffffff8p1023", BUSTER_TYPE_DOUBLE, CHECK_LITERAL_RANGE_OVERFLOW },
    { "0x1p-1074", BUSTER_TYPE_DOUBLE, CHECK_LITERAL_RANGE_NONE },
    { "0x1p-1075", BUSTER_TYPE_DOUBLE, CHECK_LITERAL_RANGE_UNDERFLOW },
    { "0x1.0000000000001p-1075", BUSTER_TYPE_DOUBLE, CHECK_LITERAL_RANGE_NONE },
    { "1e5000L", BUSTER_TYPE_LONG_DOUBLE, CHECK_LITERAL_RANGE_OVERFLOW },
    { "1e-5000L", BUSTER_TYPE_LONG_DOUBLE, CHECK_LITERAL_RANGE_UNDERFLOW },
    { "0x1.fffffffffffffffep16383L", BUSTER_TYPE_LONG_DOUBLE, CHECK_LITERAL_RANGE_NONE },
    { "0x1.ffffffffffffffffp16383L", BUSTER_TYPE_LONG_DOUBLE, CHECK_LITERAL_RANGE_OVERFLOW },
    { "0x1p-16445L", BUSTER_TYPE_LONG_DOUBLE, CHECK_LITERAL_RANGE_NONE },
    { "0x1p-16446L", BUSTER_TYPE_LONG_DOUBLE, CHECK_LITERAL_RANGE_UNDERFLOW },
    { "0x1.0000000000000002p-16446L", BUSTER_TYPE_LONG_DOUBLE, CHECK_LITERAL_RANGE_NONE },
    { "65504.0f16", BUSTER_TYPE_FLOAT16, CHECK_LITERAL_RANGE_NONE },
    { "65519.0f16", BUSTER_TYPE_FLOAT16, CHECK_LITERAL_RANGE_NONE },
    { "65520.0f16", BUSTER_TYPE_FLOAT16, CHECK_LITERAL_RANGE_OVERFLOW },
    { "65519.999999999999999999f16", BUSTER_TYPE_FLOAT16, CHECK_LITERAL_RANGE_NONE },
    { "0x1p-24f16", BUSTER_TYPE_FLOAT16, CHECK_LITERAL_RANGE_NONE },
    { "0x1p-25f16", BUSTER_TYPE_FLOAT16, CHECK_LITERAL_RANGE_UNDERFLOW },
    { "0x1.0000000000000000001p-25f16", BUSTER_TYPE_FLOAT16, CHECK_LITERAL_RANGE_NONE },
    { "0x0.fffffffffffffffffffP-25f16", BUSTER_TYPE_FLOAT16, CHECK_LITERAL_RANGE_UNDERFLOW },
    { "0e-99999f16", BUSTER_TYPE_FLOAT16, CHECK_LITERAL_RANGE_NONE },
    { "1e40fi", BUSTER_TYPE_FLOAT, CHECK_LITERAL_RANGE_OVERFLOW },
    { "1e-46if", BUSTER_TYPE_FLOAT, CHECK_LITERAL_RANGE_UNDERFLOW },
    { "1e400j", BUSTER_TYPE_DOUBLE, CHECK_LITERAL_RANGE_OVERFLOW },
    { "1e-5000Li", BUSTER_TYPE_LONG_DOUBLE, CHECK_LITERAL_RANGE_UNDERFLOW },
};
static void check_test_literal_warnings(CheckTest* test, Arena* scratch)
{
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(check_test_literal_range_cases); c += 1)
    {
        const CheckTestLiteralRange* item = check_test_literal_range_cases + c;
        const uint64_t mark = scratch->length;
        CheckLiteral literal = { 0 };
        check_literal_number((const uint8_t*)item->spelling, (uint32_t)strlen(item->spelling), scratch, &literal);
        CheckWarningEvent event = { 0 };
        const bool warned = check_literal_warning_event(&literal, 17, &event);
        const bool wanted = item->range != CHECK_LITERAL_RANGE_NONE;
        const TypeId type = literal.imaginary ? BUSTER_TYPE_BUILTIN(type_index(literal.type) - BUSTER_TYPE_CODE_COMPLEX) : literal.type;
        char description[192];
        snprintf(description, sizeof(description), "literal %s retains source-format range and sparse warning fact", item->spelling);
        check_test_expect(test, literal.valid && type == item->type && literal.range == item->range && warned == wanted &&
                          scratch->length == mark && (!warned || (event.id == CHECK_WARNING_ID_LITERAL_RANGE &&
                          event.kind == (item->range == CHECK_LITERAL_RANGE_OVERFLOW ? CHECK_WARNING_EVENT_LITERAL_OVERFLOW : CHECK_WARNING_EVENT_LITERAL_UNDERFLOW) &&
                          event.token == 17 && event.source == BUSTER_CHECK_WARNING_NO_SOURCE && event.flags == 0 &&
                          event.completion_kind == CHECK_WARNING_COMPLETE_AT_TOKEN && event.completion == 17 &&
                          event.payload.first_type == item->type && event.note_count == 0)), description);
    }
    CheckLiteral invalid = { 0 };
    check_literal_number((const uint8_t*)"1e999ff", sizeof("1e999ff") - 1, scratch, &invalid);
    CheckWarningEvent event = { 0 };
    check_test_expect(test, !invalid.valid && !check_literal_warning_event(&invalid, 19, &event),
                      "a rejected numeric spelling does not add a literal-range warning");
}
