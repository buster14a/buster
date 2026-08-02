#include <buster/tests/string_test.h>


#if defined(_WIN32)
#define BUSTER_UNICODE_OS_TO_UTF8_TEST(args, arena_value, utf8_value, utf16_value)                                                                             \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        String8 unicode_utf8_from_os = string8_from_string16((arena_value), (utf16_value), true);                                                              \
        BUSTER_STRING_TEST((args), unicode_utf8_from_os, (utf8_value));                                                                                        \
        BUSTER_TEST_RAW((args), unicode_utf8_from_os.pointer[unicode_utf8_from_os.length] == 0,                                                                \
                        S8("string8_duplicate_from_string_os did not write a terminator"));                                                                    \
    } while (0)
#else
#define BUSTER_UNICODE_OS_TO_UTF8_TEST(args, arena_value, utf8_value, utf16_value)                                                                             \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        BUSTER_UNUSED(utf16_value);                                                                                                                            \
        String8 unicode_utf8_from_os = (utf8_value);                                                                                                           \
        BUSTER_STRING_TEST((args), unicode_utf8_from_os, (utf8_value));                                                                                        \
        BUSTER_TEST_RAW((args), unicode_utf8_from_os.pointer[unicode_utf8_from_os.length] == 0,                                                                \
                        S8("string8_duplicate_from_string_os did not write a terminator"));                                                                    \
    } while (0)
#endif

#define BUSTER_UNICODE_ROUND_TRIP_TEST(args, arena_value, utf8_value, utf16_value)                                                                             \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        String8 unicode_utf8 = (utf8_value);                                                                                                                   \
        String16 unicode_expected_utf16 = (utf16_value);                                                                                                       \
        String16 unicode_utf16 = string16_from_string8((arena_value), unicode_utf8, true);                                                                     \
        BUSTER_TEST_RAW((args), string16_equal(unicode_utf16, unicode_expected_utf16), S8("string16_from_string8 Unicode decode mismatch"));                   \
        BUSTER_TEST_RAW((args), unicode_utf16.pointer[unicode_utf16.length] == 0, S8("string16_from_string8 did not write a terminator"));                     \
        String8 unicode_utf8_round_trip = string8_from_string16((arena_value), unicode_utf16, true);                                                           \
        BUSTER_STRING_TEST((args), unicode_utf8_round_trip, unicode_utf8);                                                                                     \
        BUSTER_TEST_RAW((args), unicode_utf8_round_trip.pointer[unicode_utf8_round_trip.length] == 0,                                                          \
                        S8("string16_to_string8_arena did not write a terminator"));                                                                           \
        BUSTER_UNICODE_OS_TO_UTF8_TEST((args), (arena_value), unicode_utf8, unicode_expected_utf16);                                                           \
    } while (0)

#define BUSTER_UTF16_TO_UTF8_TEST(args, arena_value, utf16_value, expected_utf8_value)                                                                         \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        String16 unicode_utf16 = (utf16_value);                                                                                                                \
        String8 unicode_expected_utf8 = (expected_utf8_value);                                                                                                 \
        String8 unicode_utf8 = string8_from_string16((arena_value), unicode_utf16, true);                                                                      \
        BUSTER_STRING_TEST((args), unicode_utf8, unicode_expected_utf8);                                                                                       \
        BUSTER_TEST_RAW((args), unicode_utf8.pointer[unicode_utf8.length] == 0, S8("string16_to_string8_arena did not write a terminator"));                   \
    } while (0)

BUSTER_TEST_F_DECL UnitTestResult string_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    // string8_format
    {
        {
            String8 formatted = string_format(arena, S8("{{ {S8} }}"), S8("value"));
            BUSTER_STRING_TEST(arguments, formatted, S8("{ value }"));
        }
        {
            String8 formatted = string_format(arena, S8("async_thread_{u64}"), (u64)7);
            BUSTER_STRING_TEST(arguments, formatted, S8("async_thread_7"));
        }
        {
            String8 formatted = string_format(arena, S8("{s64}"), (s64)INT64_MIN);
            BUSTER_STRING_TEST(arguments, formatted, S8("-9223372036854775808"));
        }
        {
            char8 utf8_bytes[] = {'J', 'o', 's', (char8)0xC3, (char8)0xA9, '/', (char8)0xF0, (char8)0x9F, (char8)0x98, (char8)0x80, 0};
            char16 utf16_bytes[] = {'J', 'o', 's', 0x00E9, '/', 0xD83D, 0xDE00, 0};
            BUSTER_UNICODE_ROUND_TRIP_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 10), string16_from_pointer_length(utf16_bytes, 7));
        }
        {
            char8 utf8_bytes[] = {'A', (char8)0xC3, (char8)0xA9, (char8)0xE2, (char8)0x82, (char8)0xAC, (char8)0xF0, (char8)0x9F, (char8)0x98, (char8)0x80, 0};
            char16 utf16_bytes[] = {'A', 0x00E9, 0x20AC, 0xD83D, 0xDE00, 0};
            BUSTER_UNICODE_ROUND_TRIP_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 10), string16_from_pointer_length(utf16_bytes, 5));
        }
        {
            char8 utf8_bytes[] = {'[', (char8)0xF4, (char8)0x8F, (char8)0xBF, (char8)0xBF, ']', 0};
            char16 utf16_bytes[] = {'[', 0xDBFF, 0xDFFF, ']', 0};
            BUSTER_UNICODE_ROUND_TRIP_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 6), string16_from_pointer_length(utf16_bytes, 4));
        }
        {
            char16 utf16_bytes[] = {'A', 0xD83D, 'B', 0};
            char8 utf8_bytes[] = {'A', (char8)0xEF, (char8)0xBF, (char8)0xBD, 'B', 0};
            BUSTER_UTF16_TO_UTF8_TEST(arguments, arena, string16_from_pointer_length(utf16_bytes, 3), string_from_pointer_length(utf8_bytes, 5));
        }
        {
            char16 utf16_bytes[] = {'A', 0xDE00, 'B', 0};
            char8 utf8_bytes[] = {'A', (char8)0xEF, (char8)0xBF, (char8)0xBD, 'B', 0};
            BUSTER_UTF16_TO_UTF8_TEST(arguments, arena, string16_from_pointer_length(utf16_bytes, 3), string_from_pointer_length(utf8_bytes, 5));
        }

        enum UnsignedFormatTestCase
        {
            UNSIGNED_FORMAT_TEST_CASE_DEFAULT,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL,
            UNSIGNED_FORMAT_TEST_CASE_BINARY,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP,

            UNSIGNED_FORMAT_TEST_CASE_COUNT,
        };

        // u8
        {
            typedef enum UnsignedTestCaseId
            {
                UNSIGNED_TEST_CASE_U8,
                UNSIGNED_TEST_CASE_U16,
                UNSIGNED_TEST_CASE_U32,
                UNSIGNED_TEST_CASE_U64,
                UNSIGNED_TEST_CASE_COUNT,
            } UnsignedTestCaseId;

#undef S8
#define S8(strlit) S8_INITIALIZER(strlit)

            BUSTER_GLOBAL_LOCAL const String8
                format_strings[(u64)UNSIGNED_TEST_CASE_COUNT][(u64)UNSIGNED_FORMAT_TEST_CASE_COUNT] =
                    {
                        [(u64)UNSIGNED_TEST_CASE_U8] =
                            {
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("{u8}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("{u8:d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("{u8:x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("{u8:X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("{u8:o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("{u8:b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("{u8:no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("{u8:d,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("{u8:x,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("{u8:X,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("{u8:o,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("{u8:b,no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("{u8:width=[ ,2]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("{u8:d,width=[ ,4]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("{u8:x,width=[ ,8]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("{u8:X,width=[ ,16]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("{u8:o,width=[ ,32]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] = S8("{u8:b,width=[ ,64]}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("{u8:width=[0,2]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("{u8:d,width=[0,4]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("{u8:x,width=[0,8]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("{u8:X,width=[0,16]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("{u8:o,width=[0,32]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] = S8("{u8:b,width=[0,64]}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("{u8:width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("{u8:d,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("{u8:x,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("{u8:X,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("{u8:o,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("{u8:b,width=[0,x]}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("{u8:width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("{u8:d,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("{u8:x,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("{u8:X,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("{u8:o,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("{u8:b,width=[ ,x],no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("{u8:width=[0,2],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("{u8:d,width=[0,4],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("{u8:x,width=[0,8],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("{u8:X,width=[0,16],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("{u8:o,width=[0,32],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] = S8("{u8:b,width=[0,64],no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("{u8:width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("{u8:d,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("{u8:x,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("{u8:X,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("{u8:o,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("{u8:b,width=[0,x],no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("{u8:digit_group}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("{u8:digit_group,d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("{u8:digit_group,x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("{u8:digit_group,X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("{u8:digit_group,o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("{u8:digit_group,b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,no_prefix,d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,no_prefix,x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,no_prefix,X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,no_prefix,o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,no_prefix,b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],d,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                    S8("{u8:digit_group,width=[0,x],x,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                    S8("{u8:digit_group,width=[0,x],X,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],o,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u8:digit_group,width=[0,x],b,no_prefix}"),
                            },
                        [(u64)UNSIGNED_TEST_CASE_U16] =
                            {
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("{u16}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("{u16:d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("{u16:x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("{u16:X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("{u16:o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("{u16:b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("{u16:no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("{u16:d,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("{u16:x,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("{u16:X,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("{u16:o,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("{u16:b,no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("{u16:width=[ ,2]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("{u16:d,width=[ ,4]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("{u16:x,width=[ ,8]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("{u16:X,width=[ ,16]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("{u16:o,width=[ ,32]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] = S8("{u16:b,width=[ ,64]}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("{u16:width=[0,2]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("{u16:d,width=[0,4]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("{u16:x,width=[0,8]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("{u16:X,width=[0,16]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("{u16:o,width=[0,32]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] = S8("{u16:b,width=[0,64]}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("{u16:width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("{u16:d,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("{u16:x,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("{u16:X,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("{u16:o,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("{u16:b,width=[0,x]}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("{u16:width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("{u16:d,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("{u16:x,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("{u16:X,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("{u16:o,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("{u16:b,width=[ ,x],no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("{u16:width=[0,2],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("{u16:d,width=[0,4],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("{u16:x,width=[0,8],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("{u16:X,width=[0,16],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("{u16:o,width=[0,32],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] = S8("{u16:b,width=[0,64],no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("{u16:width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("{u16:d,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("{u16:x,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("{u16:X,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("{u16:o,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("{u16:b,width=[0,x],no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("{u16:digit_group}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("{u16:digit_group,d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("{u16:digit_group,x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("{u16:digit_group,X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("{u16:digit_group,o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("{u16:digit_group,b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,no_prefix,d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,no_prefix,x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,no_prefix,X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,no_prefix,o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,no_prefix,b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],d,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                    S8("{u16:digit_group,width=[0,x],x,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                    S8("{u16:digit_group,width=[0,x],X,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],o,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u16:digit_group,width=[0,x],b,no_prefix}"),
                            },
                        [(u64)UNSIGNED_TEST_CASE_U32] =
                            {
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("{u32}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("{u32:d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("{u32:x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("{u32:X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("{u32:o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("{u32:b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("{u32:no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("{u32:d,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("{u32:x,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("{u32:X,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("{u32:o,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("{u32:b,no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("{u32:width=[ ,2]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("{u32:d,width=[ ,4]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("{u32:x,width=[ ,8]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("{u32:X,width=[ ,16]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("{u32:o,width=[ ,32]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] = S8("{u32:b,width=[ ,64]}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("{u32:width=[0,2]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("{u32:d,width=[0,4]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("{u32:x,width=[0,8]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("{u32:X,width=[0,16]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("{u32:o,width=[0,32]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] = S8("{u32:b,width=[0,64]}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("{u32:width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("{u32:d,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("{u32:x,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("{u32:X,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("{u32:o,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("{u32:b,width=[0,x]}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("{u32:width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("{u32:d,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("{u32:x,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("{u32:X,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("{u32:o,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("{u32:b,width=[ ,x],no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("{u32:width=[0,2],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("{u32:d,width=[0,4],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("{u32:x,width=[0,8],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("{u32:X,width=[0,16],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("{u32:o,width=[0,32],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] = S8("{u32:b,width=[0,64],no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("{u32:width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("{u32:d,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("{u32:x,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("{u32:X,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("{u32:o,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("{u32:b,width=[0,x],no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("{u32:digit_group}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("{u32:digit_group,d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("{u32:digit_group,x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("{u32:digit_group,X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("{u32:digit_group,o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("{u32:digit_group,b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,no_prefix,d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,no_prefix,x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,no_prefix,X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,no_prefix,o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,no_prefix,b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],d,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                    S8("{u32:digit_group,width=[0,x],x,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                    S8("{u32:digit_group,width=[0,x],X,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],o,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u32:digit_group,width=[0,x],b,no_prefix}"),
                            },
                        [(u64)UNSIGNED_TEST_CASE_U64] =
                            {
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("{u64}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("{u64:d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("{u64:x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("{u64:X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("{u64:o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("{u64:b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("{u64:no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("{u64:d,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("{u64:x,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("{u64:X,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("{u64:o,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("{u64:b,no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("{u64:width=[ ,2]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("{u64:d,width=[ ,4]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("{u64:x,width=[ ,8]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("{u64:X,width=[ ,16]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("{u64:o,width=[ ,32]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] = S8("{u64:b,width=[ ,64]}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("{u64:width=[0,2]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("{u64:d,width=[0,4]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("{u64:x,width=[0,8]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("{u64:X,width=[0,16]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("{u64:o,width=[0,32]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] = S8("{u64:b,width=[0,64]}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("{u64:width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("{u64:d,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("{u64:x,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("{u64:X,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("{u64:o,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("{u64:b,width=[0,x]}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("{u64:width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("{u64:d,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("{u64:x,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("{u64:X,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("{u64:o,width=[ ,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("{u64:b,width=[ ,x],no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("{u64:width=[0,2],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("{u64:d,width=[0,4],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("{u64:x,width=[0,8],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("{u64:X,width=[0,16],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("{u64:o,width=[0,32],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] = S8("{u64:b,width=[0,64],no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("{u64:width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("{u64:d,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("{u64:x,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("{u64:X,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("{u64:o,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("{u64:b,width=[0,x],no_prefix}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("{u64:digit_group}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("{u64:digit_group,d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("{u64:digit_group,x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("{u64:digit_group,X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("{u64:digit_group,o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("{u64:digit_group,b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,no_prefix,d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,no_prefix,x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,no_prefix,X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,no_prefix,o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,no_prefix,b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x]}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],d}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],x}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],X}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],o}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],b}"),

                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],d,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                    S8("{u64:digit_group,width=[0,x],x,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                    S8("{u64:digit_group,width=[0,x],X,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],o,no_prefix}"),
                                [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("{u64:digit_group,width=[0,x],b,no_prefix}"),
                            },
                    };

            // 0, 1, 2, 4, 8, 16, UINT_MAX / 2, UINT_MAX

            typedef struct UnsignedTestCase UnsignedTestCase;
            struct UnsignedTestCase
            {
                String8 expected_results[(u64)UNSIGNED_FORMAT_TEST_CASE_COUNT];
                u64 value;
            };

            enum UnsignedTestCaseNumber
            {
                UNSIGNED_TEST_CASE_NUMBER_ZERO,
                UNSIGNED_TEST_CASE_NUMBER_ONE,
                UNSIGNED_TEST_CASE_NUMBER_TWO,
                UNSIGNED_TEST_CASE_NUMBER_FOUR,
                UNSIGNED_TEST_CASE_NUMBER_EIGHT,
                UNSIGNED_TEST_CASE_NUMBER_SIXTEEN,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1,
                UNSIGNED_TEST_CASE_NUMBER_UINT_MAX,
                UNSIGNED_TEST_CASE_NUMBER_COUNT,
            };

            BUSTER_GLOBAL_LOCAL const
                UnsignedTestCase
                    cases[(u64)UNSIGNED_TEST_CASE_COUNT][(u64)UNSIGNED_TEST_CASE_NUMBER_COUNT] =
                        {
                            [(u64)UNSIGNED_TEST_CASE_U8] =
                                {
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                                        {
                                            .value = 0,
                                            .expected_results =
                                                {
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b0"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("0"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                        S8("                                                               0"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("00"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                        S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x00"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x00"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b00000000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("  0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("  0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8(" 0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8(" 0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("  0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("       0"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("00"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                        S8("0000000000000000000000000000000000000000000000000000000000000000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("00"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("00"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b0"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("0"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b00000000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("000"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000"),
                                                },
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ONE] =
                                        {
                                            .value = 1,
                                            .expected_results =
                                                {
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                        S8("                                                               1"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("01"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                        S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x01"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x01"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b00000001"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("  1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("  1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8(" 1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8(" 1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("  1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("       1"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("01"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                        S8("0000000000000000000000000000000000000000000000000000000000000001"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("01"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("01"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("00000001"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b1"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("1"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x01"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x01"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b00000001"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("01"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("01"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("001"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000001"),
                                                },
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_TWO] =
                                        {
                                            .value = 2,
                                            .expected_results =
                                                {
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b10"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("10"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                        S8("                                                              10"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("02"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                        S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x02"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x02"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b00000010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("  2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("  2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8(" 2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8(" 2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("  2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("      10"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("02"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                        S8("0000000000000000000000000000000000000000000000000000000000000010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("02"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("02"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("00000010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b10"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("10"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x02"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x02"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b00000010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("02"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("02"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("002"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000010"),
                                                },
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                                        {
                                            .value = 4,
                                            .expected_results =
                                                {
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                        S8("                                                             100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("04"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                        S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x04"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x04"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b00000100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("  4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("  4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8(" 4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8(" 4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("  4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("     100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("04"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                        S8("0000000000000000000000000000000000000000000000000000000000000100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("04"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("04"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("00000100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x04"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x04"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b00000100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("04"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("04"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("004"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000100"),
                                                },
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                                        {
                                            .value = 8,
                                            .expected_results =
                                                {
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                              10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                        S8("                                                            1000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("08"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000010"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                        S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x08"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x08"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o010"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b00001000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("  8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("  8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8(" 8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8(" 8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8(" 10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("    1000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("08"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000010"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                        S8("0000000000000000000000000000000000000000000000000000000000001000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("08"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("08"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("010"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("00001000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b1000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("1000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x08"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x08"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o010"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b00001000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("008"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("08"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("08"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("010"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00001000"),
                                                },
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                                        {
                                            .value = 16,
                                            .expected_results =
                                                {
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o20"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b10000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("20"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("10000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("  16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("      10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("              10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                              20"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                        S8("                                                           10000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0016"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000010"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000010"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000020"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                        S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("016"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d016"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o020"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b00010000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8(" 16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8(" 16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8(" 20"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("   10000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0016"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000010"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000010"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000020"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                        S8("0000000000000000000000000000000000000000000000000000000000010000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("016"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("016"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("020"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("00010000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o20"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b10000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("16"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("20"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("10000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("016"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d016"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o020"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b00010000"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("016"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("016"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("020"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00010000"),
                                                },
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                                        {
                                            .value = UINT8_MAX / 2,
                                            .expected_results =
                                                {
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x7f"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x7F"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o177"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("7f"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("7F"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("177"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8(" 127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("      7f"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("              7F"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                             177"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                        S8("                                                         1111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x0000007f"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x000000000000007F"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000177"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                        S8("0b0000000000000000000000000000000000000000000000000000000001111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x7f"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x7F"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o177"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b01111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("7f"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("7F"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("177"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8(" 1111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("0000007f"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("000000000000007F"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000177"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                        S8("0000000000000000000000000000000000000000000000000000000001111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("7f"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("7F"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("177"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("01111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x7f"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x7F"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o177"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b1111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("7f"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("7F"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("177"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("1111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x7f"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x7F"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o177"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b01111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("127"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("177"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("01111111"),
                                                },
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                                        {
                                            .value = UINT8_MAX - 5,
                                            .expected_results =
                                                {
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfa"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFA"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o372"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b11111010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fa"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FA"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("372"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("11111010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8(" 250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("      fa"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("              FA"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                             372"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                        S8("                                                        11111010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x000000fa"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x00000000000000FA"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000372"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                        S8("0b0000000000000000000000000000000000000000000000000000000011111010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfa"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFA"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o372"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b11111010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fa"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FA"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("372"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("11111010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("000000fa"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("00000000000000FA"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000372"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                        S8("0000000000000000000000000000000000000000000000000000000011111010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fa"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FA"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("372"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("11111010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xfa"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFA"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o372"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("fa"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FA"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("372"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xfa"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFA"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o372"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111010"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("250"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fa"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FA"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("372"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("11111010"),
                                                },
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                                        {
                                            .value = UINT8_MAX - 4,
                                            .expected_results =
                                                {
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfb"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFB"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o373"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b11111011"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fb"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FB"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("373"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("11111011"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8(" 251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("      fb"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("              FB"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                             373"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                        S8("                                                        11111011"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x000000fb"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x00000000000000FB"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000373"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                        S8("0b0000000000000000000000000000000000000000000000000000000011111011"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfb"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFB"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o373"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b11111011"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fb"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FB"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("373"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("11111011"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("000000fb"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("00000000000000FB"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000373"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                        S8("0000000000000000000000000000000000000000000000000000000011111011"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fb"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FB"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("373"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("11111011"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xfb"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFB"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o373"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111011"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("fb"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FB"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("373"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111011"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xfb"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFB"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o373"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111011"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("251"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fb"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FB"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("373"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("11111011"),
                                                },
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                                        {
                                            .value = UINT8_MAX - 3,
                                            .expected_results =
                                                {
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfc"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFC"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o374"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b11111100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fc"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FC"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("374"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("11111100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8(" 252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("      fc"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("              FC"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                             374"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                        S8("                                                        11111100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x000000fc"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x00000000000000FC"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000374"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                        S8("0b0000000000000000000000000000000000000000000000000000000011111100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfc"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFC"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o374"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b11111100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fc"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FC"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("374"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("11111100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("000000fc"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("00000000000000FC"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000374"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                        S8("0000000000000000000000000000000000000000000000000000000011111100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fc"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FC"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("374"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("11111100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xfc"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFC"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o374"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("fc"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FC"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("374"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xfc"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFC"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o374"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111100"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("252"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fc"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FC"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("374"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("11111100"),
                                                },
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                                        {
                                            .value = UINT8_MAX - 2,
                                            .expected_results =
                                                {
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfd"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFD"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o375"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b11111101"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fd"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FD"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("375"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("11111101"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8(" 253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("      fd"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("              FD"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                             375"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                        S8("                                                        11111101"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x000000fd"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x00000000000000FD"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000375"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                        S8("0b0000000000000000000000000000000000000000000000000000000011111101"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfd"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFD"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o375"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b11111101"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fd"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FD"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("375"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("11111101"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("000000fd"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("00000000000000FD"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000375"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                        S8("0000000000000000000000000000000000000000000000000000000011111101"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fd"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FD"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("375"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("11111101"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xfd"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFD"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o375"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111101"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("fd"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FD"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("375"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111101"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xfd"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFD"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o375"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111101"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("253"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fd"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FD"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("375"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("11111101"),
                                                },
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                                        {
                                            .value = UINT8_MAX - 1,
                                            .expected_results =
                                                {
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfe"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFE"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o376"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b11111110"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fe"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FE"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("376"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("11111110"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8(" 254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("      fe"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("              FE"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                             376"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                        S8("                                                        11111110"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x000000fe"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x00000000000000FE"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000376"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                        S8("0b0000000000000000000000000000000000000000000000000000000011111110"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfe"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFE"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o376"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b11111110"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fe"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FE"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("376"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("11111110"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("000000fe"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("00000000000000FE"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000376"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                        S8("0000000000000000000000000000000000000000000000000000000011111110"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fe"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FE"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("376"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("11111110"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xfe"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFE"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o376"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111110"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("fe"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FE"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("376"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111110"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xfe"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFE"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o376"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111110"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("254"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("fe"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FE"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("376"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("11111110"),
                                                },
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                                        {
                                            .value = UINT8_MAX,
                                            .expected_results =
                                                {
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xff"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFF"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o377"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b11111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("ff"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FF"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("377"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("11111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8(" 255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("      ff"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("              FF"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                             377"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                        S8("                                                        11111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x000000ff"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x00000000000000FF"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000377"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                        S8("0b0000000000000000000000000000000000000000000000000000000011111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xff"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFF"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o377"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b11111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("ff"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FF"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("377"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("11111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("000000ff"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("00000000000000FF"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000377"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                        S8("0000000000000000000000000000000000000000000000000000000011111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("ff"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FF"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("377"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("11111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o377"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("377"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o377"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111111"),

                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("255"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("377"),
                                                    [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("11111111"),
                                                },
                                        },
                                },

                            // ==================== U16 ====================

                            [(u64)UNSIGNED_TEST_CASE_U16] =
                                {
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                                        {
                                            .value = 0,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                               0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("00"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("00000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d00000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b0000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("    0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("    0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("   0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("   0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("     0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("               0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("00"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("00000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("00000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d00000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b0000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ONE] =
                                        {
                                            .value = 1,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                               1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("01"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("00001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d00001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b0000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("    1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("    1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("   1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("   1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("     1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("               1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("01"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("00001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("00001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d00001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b0000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000001")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_TWO] =
                                        {
                                            .value = 2,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                              10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("02"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("00002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d00002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b0000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("    2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("    2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("   2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("   2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("     2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("              10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("02"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("00002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("00002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d00002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b0000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000010")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                                        {
                                            .value = 4,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                             100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("04"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("00004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d00004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b0000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("    4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("    4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("   4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("   4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("     4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("             100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("04"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("00004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("00004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d00004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b0000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000100")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                                        {
                                            .value = 8,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                              10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                            1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("08"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("00008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d00008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b0000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("    8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("    8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("   8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("   8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("    10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("            1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("08"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("00008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("00008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d00008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b0000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000001000")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                                        {
                                            .value = 16,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("  16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("      10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("              10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                              20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                           10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("00016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d00016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x0010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x0010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b0000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("   16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("   16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("  10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("  10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("    20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("           10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("00016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("0010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("0010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("00016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d00016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b0000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000010000")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                                        {
                                            .value = UINT16_MAX / 2,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x7fff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x7FFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o77777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("7fff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("7FFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("77777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("    7fff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("            7FFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                           77777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                 111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00007fff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000007FFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000077777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x7fff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x7FFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o077777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b0111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("7fff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("7FFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8(" 77777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8(" 111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00007fff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000007FFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000077777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("32767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("7fff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("7FFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("077777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("0111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("32.767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d32.767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x7f_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x7F_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o77_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b1111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("32.767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("32.767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("77_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("1111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("32.767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d32.767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x7f_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x7F_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o077_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b01111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("32.767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("32.767"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("077_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("01111111_11111111")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                                        {
                                            .value = UINT16_MAX - 5,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o177772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("177772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("    fffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("            FFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                          177772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                1111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x0000fffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x000000000000FFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000177772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000001111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o177772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b1111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("177772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("1111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("0000fffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("000000000000FFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000177772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000001111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("65530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("177772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("1111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("65.530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d65.530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_fa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o177_772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111111_11111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("65.530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("65.530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_fa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("177_772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("65.530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d65.530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_fa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o177_772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111111_11111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("65.530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("65.530"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("177_772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111010")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                                        {
                                            .value = UINT16_MAX - 4,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o177773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("177773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("    fffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("            FFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                          177773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                1111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x0000fffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x000000000000FFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000177773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000001111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o177773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b1111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("177773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("1111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("0000fffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("000000000000FFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000177773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000001111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("65531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("177773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("1111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("65.531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d65.531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_fb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o177_773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111111_11111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("65.531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("65.531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_fb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("177_773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("65.531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d65.531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_fb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o177_773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111111_11111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("65.531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("65.531"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("177_773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111011")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                                        {
                                            .value = UINT16_MAX - 3,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o177774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("177774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("    fffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("            FFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                          177774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                1111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x0000fffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x000000000000FFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000177774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000001111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o177774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b1111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("177774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("1111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("0000fffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("000000000000FFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000177774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000001111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("65532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("177774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("1111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("65.532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d65.532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_fc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o177_774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111111_11111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("65.532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("65.532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_fc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("177_774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("65.532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d65.532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_fc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o177_774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111111_11111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("65.532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("65.532"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("177_774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111100")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                                        {
                                            .value = UINT16_MAX - 2,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o177775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("177775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("    fffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("            FFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                          177775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                1111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x0000fffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x000000000000FFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000177775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000001111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o177775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b1111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("177775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("1111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("0000fffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("000000000000FFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000177775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000001111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("65533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("177775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("1111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("65.533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d65.533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_fd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o177_775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111111_11111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("65.533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("65.533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_fd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("177_775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("65.533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d65.533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_fd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o177_775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111111_11111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("65.533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("65.533"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("177_775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111101")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                                        {
                                            .value = UINT16_MAX - 1,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o177776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("177776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("    fffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("            FFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                          177776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                1111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x0000fffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x000000000000FFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000177776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000001111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o177776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b1111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("177776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("1111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("0000fffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("000000000000FFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000177776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000001111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("65534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("177776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("1111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("65.534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d65.534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_fe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o177_776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111111_11111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("65.534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("65.534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_fe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("177_776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("65.534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d65.534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_fe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o177_776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111111_11111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("65.534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("65.534"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_fe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("177_776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111110")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                                        {
                                            .value = UINT16_MAX,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o177777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("ffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("177777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("    ffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("            FFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                          177777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                1111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x0000ffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x000000000000FFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000177777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000001111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o177777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b1111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("ffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("177777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("1111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("0000ffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("000000000000FFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000177777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000001111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("65535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("ffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("177777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("1111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("65.535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d65.535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o177_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("65.535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("65.535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("177_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("65.535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d65.535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o177_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("65.535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("65.535"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("177_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111111")},
                                        },
                                },

                            // ==================== U32 ====================

                            [(u64)UNSIGNED_TEST_CASE_U32] =
                                {
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                                        {
                                            .value = 0,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                               0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("00"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("0000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d0000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o00000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b00000000000000000000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("         0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("         0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("       0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("       0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("          0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("                               0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("00"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d0000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o00000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b00000000000000000000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("00000000000000000000000000000000")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ONE] =
                                        {
                                            .value = 1,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                               1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("01"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("0000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d0000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o00000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b00000000000000000000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("         1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("         1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("       1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("       1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("          1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("                               1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("01"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d0000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o00000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b00000000000000000000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("00000000000000000000000000000001")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_TWO] =
                                        {
                                            .value = 2,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                              10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("02"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("0000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d0000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o00000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b00000000000000000000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("         2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("         2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("       2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("       2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("          2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("                              10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("02"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d0000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o00000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b00000000000000000000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("00000000000000000000000000000010")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                                        {
                                            .value = 4,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                             100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("04"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("0000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d0000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o00000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b00000000000000000000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("         4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("         4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("       4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("       4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("          4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("                             100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("04"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d0000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o00000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b00000000000000000000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("00000000000000000000000000000100")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                                        {
                                            .value = 8,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                              10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                            1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("08"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("0000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d0000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o00000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b00000000000000000000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("         8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("         8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("       8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("       8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("         10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("                            1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("08"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d0000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o00000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b00000000000000000000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("00000000000000000000000000001000")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                                        {
                                            .value = 16,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("  16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("      10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("              10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                              20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                           10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("0000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d0000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o00000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b00000000000000000000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("        16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("        16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("      10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("      10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("         20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("                           10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d0000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o00000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b00000000000000000000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("00000000000000000000000000010000")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                                        {
                                            .value = UINT32_MAX / 2,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x7fffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x7FFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o17777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("7fffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("7FFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("17777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("7fffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("        7FFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                     17777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                 1111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x7fffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x000000007FFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000017777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000001111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x7fffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x7FFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o17777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b01111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("7fffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("7FFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("17777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8(" 1111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("7fffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("000000007FFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000017777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000001111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("2147483647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("7fffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("7FFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("17777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("01111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("2.147.483.647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d2.147.483.647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x7f_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x7F_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o17_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b1111111_11111111_11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("2.147.483.647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("2.147.483.647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("17_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("1111111_11111111_11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("2.147.483.647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d2.147.483.647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x7f_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x7F_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o17_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b01111111_11111111_11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("2.147.483.647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("2.147.483.647"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("17_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("01111111_11111111_11111111_11111111")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                                        {
                                            .value = UINT32_MAX - 5,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o37777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b11111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("37777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("11111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("fffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("        FFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                     37777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                11111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0xfffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x00000000FFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000037777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000011111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o37777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b11111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("37777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("11111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("fffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("00000000FFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000037777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000011111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("4294967290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("37777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("11111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("4.294.967.290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d4.294.967.290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_ff_ff_fa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FF_FF_FA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o37_777_777_772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111111_11111111_11111111_11111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("37_777_777_772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111111_11111111_11111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("4.294.967.290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d4.294.967.290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_ff_ff_fa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FF_FF_FA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o37_777_777_772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111111_11111111_11111111_11111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.290"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("37_777_777_772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111010")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                                        {
                                            .value = UINT32_MAX - 4,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o37777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b11111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("37777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("11111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("fffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("        FFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                     37777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                11111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0xfffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x00000000FFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000037777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000011111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o37777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b11111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("37777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("11111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("fffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("00000000FFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000037777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000011111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("4294967291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("37777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("11111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("4.294.967.291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d4.294.967.291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_ff_ff_fb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FF_FF_FB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o37_777_777_773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111111_11111111_11111111_11111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("37_777_777_773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111111_11111111_11111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("4.294.967.291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d4.294.967.291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_ff_ff_fb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FF_FF_FB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o37_777_777_773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111111_11111111_11111111_11111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.291"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("37_777_777_773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111011")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                                        {
                                            .value = UINT32_MAX - 3,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o37777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b11111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("37777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("11111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("fffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("        FFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                     37777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                11111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0xfffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x00000000FFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000037777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000011111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o37777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b11111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("37777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("11111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("fffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("00000000FFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000037777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000011111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("4294967292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("37777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("11111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("4.294.967.292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d4.294.967.292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_ff_ff_fc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FF_FF_FC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o37_777_777_774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111111_11111111_11111111_11111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("37_777_777_774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111111_11111111_11111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("4.294.967.292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d4.294.967.292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_ff_ff_fc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FF_FF_FC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o37_777_777_774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111111_11111111_11111111_11111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.292"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("37_777_777_774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111100")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                                        {
                                            .value = UINT32_MAX - 2,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o37777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b11111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("37777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("11111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("fffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("        FFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                     37777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                11111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0xfffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x00000000FFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000037777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000011111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o37777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b11111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("37777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("11111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("fffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("00000000FFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000037777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000011111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("4294967293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("37777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("11111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("4.294.967.293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d4.294.967.293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_ff_ff_fd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FF_FF_FD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o37_777_777_775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111111_11111111_11111111_11111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("37_777_777_775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111111_11111111_11111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("4.294.967.293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d4.294.967.293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_ff_ff_fd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FF_FF_FD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o37_777_777_775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111111_11111111_11111111_11111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.293"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("37_777_777_775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111101")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                                        {
                                            .value = UINT32_MAX - 1,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o37777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b11111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("37777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("11111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("fffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("        FFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                     37777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                11111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0xfffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x00000000FFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000037777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000011111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o37777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b11111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("37777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("11111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("fffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("00000000FFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000037777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000011111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("4294967294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("37777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("11111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("4.294.967.294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d4.294.967.294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_ff_ff_fe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FF_FF_FE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o37_777_777_776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111111_11111111_11111111_11111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("37_777_777_776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111111_11111111_11111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("4.294.967.294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d4.294.967.294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_ff_ff_fe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FF_FF_FE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o37_777_777_776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111111_11111111_11111111_11111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.294"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_fe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("37_777_777_776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111110")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                                        {
                                            .value = UINT32_MAX,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o37777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b11111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("ffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("37777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("11111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("ffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("        FFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                     37777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                11111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0xffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x00000000FFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000037777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000011111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o37777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] = S8("0b11111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("ffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("37777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] = S8("11111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("ffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("00000000FFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000037777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000011111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("4294967295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("ffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("37777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] = S8("11111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("4.294.967.295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d4.294.967.295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o37_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b11111111_11111111_11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("37_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("11111111_11111111_11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("4.294.967.295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d4.294.967.295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o37_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0b11111111_11111111_11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("4.294.967.295"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("37_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111111")},
                                        },
                                },

                            // ==================== U64 ====================

                            [(u64)UNSIGNED_TEST_CASE_U64] =
                                {
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ZERO] =
                                        {
                                            .value = 0,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                               0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("00"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("00000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d00000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o0000000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("                   0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("                   0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("               0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("               0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("                     0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =
                                                     S8("                                                               0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("00"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("0"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("0"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("00000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d00000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o0000000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000000000"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000000")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_ONE] =
                                        {
                                            .value = 1,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                               1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("01"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("00000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d00000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o0000000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("                   1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("                   1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("               1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("               1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("                     1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =
                                                     S8("                                                               1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("01"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("1"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("1"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("00000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d00000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o0000000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000000001"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000001")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_TWO] =
                                        {
                                            .value = 2,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                              10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("02"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("00000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d00000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o0000000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("                   2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("                   2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("               2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("               2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("                     2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =
                                                     S8("                                                              10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("02"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("2"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("10"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("00000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d00000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o0000000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000000002"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000010")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_FOUR] =
                                        {
                                            .value = 4,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                               4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                             100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("04"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("00000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d00000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o0000000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("                   4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("                   4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("               4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("               4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("                     4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =
                                                     S8("                                                             100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("04"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("4"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("00000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d00000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o0000000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000000004"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000000100")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_EIGHT] =
                                        {
                                            .value = 8,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8(" 8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("   8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("       8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("               8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                              10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                            1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("08"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("00000000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d00000000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o0000000000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("                   8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("                   8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("               8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("               8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("                    10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =
                                                     S8("                                                            1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("08"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("8"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("1000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("00000000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d00000000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o0000000000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000008"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000001000")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_SIXTEEN] =
                                        {
                                            .value = 16,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] = S8("0b10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] = S8("10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("  16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("      10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("              10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("                              20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("                                                           10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d0016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000000000000000000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("00000000000000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d00000000000000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o0000000000000000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("                  16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("                  16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("              10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("              10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("                    20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =
                                                     S8("                                                           10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("0016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("00000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000000000000000000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("00000000000000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("0000000000000000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] = S8("0b10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("16"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("10"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("20"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] = S8("10000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("00000000000000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d00000000000000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o0000000000000000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =
                                                     S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000000000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("00000000000000000016"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000010"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0000000000000000000020"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("0000000000000000000000000000000000000000000000000000000000010000")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_DIVIDED_BY_2] =
                                        {
                                            .value = UINT64_MAX / 2,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("9223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d9223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0x7fffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0x7FFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =
                                                     S8("0b111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("9223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("9223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("7fffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("7FFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =
                                                     S8("111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("9223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("9223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("7fffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("7FFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("           777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8(" 111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("9223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d9223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0x7fffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0x7FFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000000777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b0111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("09223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d09223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0x7fffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0x7FFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o0777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =
                                                     S8("0b0111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8(" 9223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8(" 9223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("7fffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("7FFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8(" 777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =
                                                     S8(" 111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("9223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("9223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("7fffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("7FFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000000777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("0111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("09223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("09223372036854775807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("7fffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("7FFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("0777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =
                                                     S8("0111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("9.223.372.036.854.775.807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d9.223.372.036.854.775.807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0x7f_ff_ff_ff_ff_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0x7F_FF_FF_FF_FF_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o777_777_777_777_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =
                                                     S8("0b1111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("9.223.372.036.854.775.807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("9.223.372.036.854.775.807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("7f_ff_ff_ff_ff_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("7F_FF_FF_FF_FF_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("777_777_777_777_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("1111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("09.223.372.036.854.775.807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d09.223.372.036.854.775.807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x7f_ff_ff_ff_ff_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0x7F_FF_FF_FF_FF_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o0777_777_777_777_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =
                                                     S8("0b01111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("09.223.372.036.854.775.807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("09.223.372.036.854.775.807"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("7f_ff_ff_ff_ff_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("7F_FF_FF_FF_FF_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("0777_777_777_777_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("01111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_5] =
                                        {
                                            .value = UINT64_MAX - 5,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffffffffffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFFFFFFFFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o1777777777777777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffffffffffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFFFFFFFFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("1777777777777777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("fffffffffffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("FFFFFFFFFFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("          1777777777777777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0xfffffffffffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0xFFFFFFFFFFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000001777777777777777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffffffffffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFFFFFFFFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o1777777777777777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffffffffffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFFFFFFFFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("1777777777777777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("fffffffffffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("FFFFFFFFFFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000001777777777777777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("18446744073709551610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffffffffffffffa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFFFFFFFFFFFFFA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("1777777777777777777772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("18.446.744.073.709.551.610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d18.446.744.073.709.551.610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_ff_ff_ff_ff_ff_ff_fa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FF_FF_FF_FF_FF_FF_FA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o1_777_777_777_777_777_777_772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =
                                                     S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("1_777_777_777_777_777_777_772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("18.446.744.073.709.551.610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d18.446.744.073.709.551.610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_ff_ff_ff_ff_ff_ff_fa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FF_FF_FF_FF_FF_FF_FA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o1_777_777_777_777_777_777_772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =
                                                     S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.610"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("ff_ff_ff_ff_ff_ff_ff_fa"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("FF_FF_FF_FF_FF_FF_FF_FA"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("1_777_777_777_777_777_777_772"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111010")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_4] =
                                        {
                                            .value = UINT64_MAX - 4,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffffffffffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFFFFFFFFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o1777777777777777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffffffffffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFFFFFFFFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("1777777777777777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("fffffffffffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("FFFFFFFFFFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("          1777777777777777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0xfffffffffffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0xFFFFFFFFFFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000001777777777777777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffffffffffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFFFFFFFFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o1777777777777777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffffffffffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFFFFFFFFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("1777777777777777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("fffffffffffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("FFFFFFFFFFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000001777777777777777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("18446744073709551611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffffffffffffffb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFFFFFFFFFFFFFB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("1777777777777777777773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("18.446.744.073.709.551.611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d18.446.744.073.709.551.611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_ff_ff_ff_ff_ff_ff_fb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FF_FF_FF_FF_FF_FF_FB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o1_777_777_777_777_777_777_773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =
                                                     S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("1_777_777_777_777_777_777_773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("18.446.744.073.709.551.611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d18.446.744.073.709.551.611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_ff_ff_ff_ff_ff_ff_fb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FF_FF_FF_FF_FF_FF_FB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o1_777_777_777_777_777_777_773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =
                                                     S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.611"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("ff_ff_ff_ff_ff_ff_ff_fb"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("FF_FF_FF_FF_FF_FF_FF_FB"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("1_777_777_777_777_777_777_773"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111011")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_3] =
                                        {
                                            .value = UINT64_MAX - 3,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffffffffffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFFFFFFFFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o1777777777777777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffffffffffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFFFFFFFFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("1777777777777777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("fffffffffffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("FFFFFFFFFFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("          1777777777777777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0xfffffffffffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0xFFFFFFFFFFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000001777777777777777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffffffffffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFFFFFFFFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o1777777777777777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffffffffffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFFFFFFFFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("1777777777777777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("fffffffffffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("FFFFFFFFFFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000001777777777777777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("18446744073709551612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffffffffffffffc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFFFFFFFFFFFFFC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("1777777777777777777774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("18.446.744.073.709.551.612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d18.446.744.073.709.551.612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_ff_ff_ff_ff_ff_ff_fc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FF_FF_FF_FF_FF_FF_FC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o1_777_777_777_777_777_777_774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =
                                                     S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("1_777_777_777_777_777_777_774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("18.446.744.073.709.551.612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d18.446.744.073.709.551.612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_ff_ff_ff_ff_ff_ff_fc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FF_FF_FF_FF_FF_FF_FC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o1_777_777_777_777_777_777_774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =
                                                     S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.612"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("ff_ff_ff_ff_ff_ff_ff_fc"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("FF_FF_FF_FF_FF_FF_FF_FC"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("1_777_777_777_777_777_777_774"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111100")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_2] =
                                        {
                                            .value = UINT64_MAX - 2,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffffffffffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFFFFFFFFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o1777777777777777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffffffffffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFFFFFFFFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("1777777777777777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("fffffffffffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("FFFFFFFFFFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("          1777777777777777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0xfffffffffffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0xFFFFFFFFFFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000001777777777777777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffffffffffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFFFFFFFFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o1777777777777777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffffffffffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFFFFFFFFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("1777777777777777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("fffffffffffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("FFFFFFFFFFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000001777777777777777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("18446744073709551613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffffffffffffffd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFFFFFFFFFFFFFD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("1777777777777777777775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("18.446.744.073.709.551.613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d18.446.744.073.709.551.613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_ff_ff_ff_ff_ff_ff_fd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FF_FF_FF_FF_FF_FF_FD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o1_777_777_777_777_777_777_775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =
                                                     S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("1_777_777_777_777_777_777_775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("18.446.744.073.709.551.613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d18.446.744.073.709.551.613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_ff_ff_ff_ff_ff_ff_fd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FF_FF_FF_FF_FF_FF_FD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o1_777_777_777_777_777_777_775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =
                                                     S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.613"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("ff_ff_ff_ff_ff_ff_ff_fd"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("FF_FF_FF_FF_FF_FF_FF_FD"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("1_777_777_777_777_777_777_775"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111101")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX_MINUS_1] =
                                        {
                                            .value = UINT64_MAX - 1,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xfffffffffffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFFFFFFFFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o1777777777777777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("fffffffffffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFFFFFFFFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("1777777777777777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("fffffffffffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("FFFFFFFFFFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("          1777777777777777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0xfffffffffffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0xFFFFFFFFFFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000001777777777777777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xfffffffffffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFFFFFFFFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o1777777777777777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("fffffffffffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFFFFFFFFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("1777777777777777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("fffffffffffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("FFFFFFFFFFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000001777777777777777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("18446744073709551614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("fffffffffffffffe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFFFFFFFFFFFFFE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("1777777777777777777776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("18.446.744.073.709.551.614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d18.446.744.073.709.551.614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_ff_ff_ff_ff_ff_ff_fe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FF_FF_FF_FF_FF_FF_FE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o1_777_777_777_777_777_777_776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =
                                                     S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_fe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("1_777_777_777_777_777_777_776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("18.446.744.073.709.551.614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d18.446.744.073.709.551.614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_ff_ff_ff_ff_ff_ff_fe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FF_FF_FF_FF_FF_FF_FE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o1_777_777_777_777_777_777_776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =
                                                     S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.614"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("ff_ff_ff_ff_ff_ff_ff_fe"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("FF_FF_FF_FF_FF_FF_FF_FE"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("1_777_777_777_777_777_777_776"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111110")},
                                        },
                                    [(u64)UNSIGNED_TEST_CASE_NUMBER_UINT_MAX] =
                                        {
                                            .value = UINT64_MAX,
                                            .expected_results =
                                                {[(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT] = S8("18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL] = S8("0d18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] = S8("0xffffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] = S8("0xFFFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL] = S8("0o1777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] = S8("18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] = S8("18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] = S8("ffffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] = S8("FFFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] = S8("1777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] = S8("18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] = S8("18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] = S8("ffffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] = S8("FFFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] = S8("          1777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] = S8("18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] = S8("0d18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] = S8("0xffffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] = S8("0xFFFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] = S8("0o00000000001777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] = S8("18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] = S8("0d18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] = S8("0xffffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] = S8("0xFFFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] = S8("0o1777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =
                                                     S8("0b1111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] = S8("18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] = S8("18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] = S8("ffffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] = S8("FFFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] = S8("1777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] = S8("18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] = S8("18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] = S8("ffffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] = S8("FFFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] = S8("00000000001777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] = S8("18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] = S8("18446744073709551615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] = S8("ffffffffffffffff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] = S8("FFFFFFFFFFFFFFFF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] = S8("1777777777777777777777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =
                                                     S8("1111111111111111111111111111111111111111111111111111111111111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_DIGIT_GROUP] = S8("18.446.744.073.709.551.615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_DIGIT_GROUP] = S8("0d18.446.744.073.709.551.615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_DIGIT_GROUP] = S8("0xff_ff_ff_ff_ff_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_DIGIT_GROUP] = S8("0xFF_FF_FF_FF_FF_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_DIGIT_GROUP] = S8("0o1_777_777_777_777_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_DIGIT_GROUP] =
                                                     S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_DIGIT_GROUP] = S8("ff_ff_ff_ff_ff_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_DIGIT_GROUP] = S8("FF_FF_FF_FF_FF_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_DIGIT_GROUP] = S8("1_777_777_777_777_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_DIGIT_GROUP] = S8("18.446.744.073.709.551.615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0d18.446.744.073.709.551.615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xff_ff_ff_ff_ff_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0xFF_FF_FF_FF_FF_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_DIGIT_GROUP] = S8("0o1_777_777_777_777_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_DIGIT_GROUP] =
                                                     S8("0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111"),

                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] = S8("18.446.744.073.709.551.615"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("ff_ff_ff_ff_ff_ff_ff_ff"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("FF_FF_FF_FF_FF_FF_FF_FF"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("1_777_777_777_777_777_777_777"),
                                                 [(u64)UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_DIGIT_GROUP] =
                                                     S8("11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111")},
                                        },
                                },
                        };

#undef S8
#define S8(strlit) ((String8)S8_INITIALIZER(strlit))

            for (u64 type_i = 0; type_i < UNSIGNED_TEST_CASE_COUNT; type_i += 1)
            {
                for (u64 case_value_i = 0; case_value_i < UNSIGNED_TEST_CASE_NUMBER_COUNT; case_value_i += 1)
                {
                    const UnsignedTestCase* uint_case = &cases[type_i][case_value_i];
                    u64 value = uint_case->value;

                    for (u64 case_i = 0; case_i < UNSIGNED_FORMAT_TEST_CASE_COUNT; case_i += 1)
                    {
                        String8 format_string = format_strings[type_i][case_i];
                        String8 expected_string = uint_case->expected_results[case_i];
                        UnsignedTestCaseId test_type = (UnsignedTestCaseId)type_i;

                        String8 result_string;
                        switch (test_type)
                        {
                            break;
                        case UNSIGNED_TEST_CASE_U8:
                            result_string = string_format(arena, format_string, (u8)value);
                            break;
                        case UNSIGNED_TEST_CASE_U16:
                            result_string = string_format(arena, format_string, (u16)value);
                            break;
                        case UNSIGNED_TEST_CASE_U32:
                            result_string = string_format(arena, format_string, (u32)value);
                            break;
                        case UNSIGNED_TEST_CASE_U64:
                            result_string = string_format(arena, format_string, (u64)value);
                            break;
                        default:
                            BUSTER_UNREACHABLE();
                        }

                        BUSTER_STRING_TEST(arguments, result_string, expected_string);
                    }
                }
            }
        }
    }

    // string_first_sequence
    {
        // Basic match at start
        BUSTER_TEST(arguments, string_first_sequence(S8("hello world"), S8("hello")) == 0);
        // Match in middle
        BUSTER_TEST(arguments, string_first_sequence(S8("hello world"), S8("world")) == 6);
        // Match at end
        BUSTER_TEST(arguments, string_first_sequence(S8("hello.txt"), S8(".txt")) == 5);
        // No match
        BUSTER_TEST(arguments, string_first_sequence(S8("hello world"), S8("foo")) == BUSTER_STRING_NO_MATCH);
        // Empty substring matches at 0
        BUSTER_TEST(arguments, string_first_sequence(S8("hello"), S8("")) == 0);
        // Empty string with empty substring
        BUSTER_TEST(arguments, string_first_sequence(S8(""), S8("")) == 0);
        // Empty string with non-empty substring
        BUSTER_TEST(arguments, string_first_sequence(S8(""), S8("a")) == BUSTER_STRING_NO_MATCH);
        // Substring longer than string
        BUSTER_TEST(arguments, string_first_sequence(S8("hi"), S8("hello")) == BUSTER_STRING_NO_MATCH);
        // Exact match
        BUSTER_TEST(arguments, string_first_sequence(S8("abc"), S8("abc")) == 0);
        // Multiple occurrences - should return first
        BUSTER_TEST(arguments, string_first_sequence(S8("abcabc"), S8("abc")) == 0);
        // Single character match
        BUSTER_TEST(arguments, string_first_sequence(S8("hello"), S8("l")) == 2);
        // Partial match should not count
        BUSTER_TEST(arguments, string_first_sequence(S8("abcd"), S8("abd")) == BUSTER_STRING_NO_MATCH);
    }

    // string_ends_with_sequence
    {
        BUSTER_TEST(arguments, string_ends_with_sequence(S8("hello.txt"), S8(".txt")));
        BUSTER_TEST(arguments, string_ends_with_sequence(S8("test.vert.spv"), S8(".vert.spv")));
        BUSTER_TEST(arguments, string_ends_with_sequence(S8("abc"), S8("abc")));
        BUSTER_TEST(arguments, string_ends_with_sequence(S8("hello"), S8("")));
        BUSTER_TEST(arguments, !string_ends_with_sequence(S8("hello.txt"), S8(".c")));
        BUSTER_TEST(arguments, !string_ends_with_sequence(S8("ab"), S8("abc")));
        BUSTER_TEST(arguments, !string_ends_with_sequence(S8("hi"), S8("hello")));
        BUSTER_TEST(arguments, string_ends_with_sequence(S8(""), S8("")));
        BUSTER_TEST(arguments, !string_ends_with_sequence(S8(""), S8("a")));
        BUSTER_TEST(arguments, !string_ends_with_sequence(S8("abcde"), S8("cdf")));
        BUSTER_TEST(arguments, !string_ends_with_sequence(S8("txtfile"), S8("txt")));
    }

    {
        PosixChar first[] = "faa";
        PosixChar second[] = "fee";
        PosixChar empty[] = "";
        PosixChar* posix_string_list[] = {first, second, empty, 0};

        SliceString8 strings = slice_string_from_posix_string_list(arena, posix_string_list);
        BUSTER_TEST(arguments, strings.length == 3);
        BUSTER_TEST(arguments, string_equal(strings.pointer[0], S8("faa")));
        BUSTER_TEST(arguments, string_equal(strings.pointer[1], S8("fee")));
        BUSTER_TEST(arguments, string_equal(strings.pointer[2], S8("")));
    }

    {
        {
            String8 parts[] = {
                S8("faa"), S8("fee"), S8("fii"), S8("foo"), S8("fuu"),
            };
            const char16* win32_string_raw = windows_string_list_from_slice_string(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts));
            String16 win32_string = string16_from_pointer(win32_string_raw);
            char16 expected_win32_string_raw[] = {'f', 'a', 'a', ' ', 'f', 'e', 'e', ' ', 'f', 'i', 'i', ' ', 'f', 'o', 'o', ' ', 'f', 'u', 'u', 0};
            String16 expected_win32_string = (String16){
                .pointer = expected_win32_string_raw,
                .length = BUSTER_ARRAY_LENGTH(expected_win32_string_raw) - 1,
            };
            BUSTER_TEST(arguments, string16_equal(win32_string, expected_win32_string));
        }
        {
            String8 parts[] = {
                S8("program"), S8("two words"), S8("quote\"arg"), S8("slash\\"), S8("trail\\"), S8(""), S8("C:\\Program Files\\"), S8("a\\\"b"),
            };
            const char16* win32_string_raw = windows_string_list_from_slice_string(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts));
            String16 win32_string = string16_from_pointer(win32_string_raw);
            String16 expected_win32_string =
                string16_from_string8(arena, S8("program \"two words\" \"quote\\\"arg\" slash\\ trail\\ \"\" \"C:\\Program Files\\\\\" \"a\\\\\\\"b\""), false);
            BUSTER_TEST(arguments, string16_equal(win32_string, expected_win32_string));
        }
        {
            String16 command_line =
                string16_from_string8(arena, S8("program \"two words\" \"quote\\\"arg\" slash\\ trail\\ \"\" \"C:\\Program Files\\\\\" \"a\\\\\\\"b\""), true);
            SliceString8 parts = slice_string_from_windows_string_list(arena, command_line.pointer);
            BUSTER_TEST(arguments, parts.length == 8);
            BUSTER_TEST(arguments, string_equal(parts.pointer[0], S8("program")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[1], S8("two words")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[2], S8("quote\"arg")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[3], S8("slash\\")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[4], S8("trail\\")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[5], S8("")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[6], S8("C:\\Program Files\\")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[7], S8("a\\\"b")));
        }
        {
            String16 command_line = string16_from_string8(arena, S8(" \t program\targ  \"\"  final"), true);
            SliceString8 parts = slice_string_from_windows_string_list(arena, command_line.pointer);
            BUSTER_TEST(arguments, parts.length == 4);
            BUSTER_TEST(arguments, string_equal(parts.pointer[0], S8("program")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[1], S8("arg")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[2], S8("")));
            BUSTER_TEST(arguments, string_equal(parts.pointer[3], S8("final")));
        }
    }

    {
        const char16 environment_block[] = {
            'U', 'S', 'E',  'R', '=', 'd', 'a', 'v', 'i', 'd', 0,   'P', 'R', 'O', 'G', 'R', 'A', 'M', '_', 'F', 'I', 'L', 'E', 'S', '=',
            'C', ':', '\\', 'P', 'r', 'o', 'g', 'r', 'a', 'm', ' ', 'F', 'i', 'l', 'e', 's', 0,   'E', 'M', 'P', 'T', 'Y', '=', 0,   0,
        };
        SliceString8 environment = string16_environment_block_to_slice_string(arena, environment_block);
        BUSTER_TEST(arguments, environment.length == 3);
        BUSTER_TEST(arguments, string_equal(environment.pointer[0], S8("USER=david")));
        BUSTER_TEST(arguments, string_equal(environment.pointer[1], S8("PROGRAM_FILES=C:\\Program Files")));
        BUSTER_TEST(arguments, string_equal(environment.pointer[2], S8("EMPTY=")));
    }
    {
        String8 environment[] = {
            S8("USER=david"),
            S8("PROGRAM_FILES=C:\\Program Files"),
            S8("EMPTY="),
        };
        const char16* environment_block = windows_environment_block_from_slice_string(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(environment));
        const char16 expected_environment_block[] = {
            'U', 'S', 'E',  'R', '=', 'd', 'a', 'v', 'i', 'd', 0,   'P', 'R', 'O', 'G', 'R', 'A', 'M', '_', 'F', 'I', 'L', 'E', 'S', '=',
            'C', ':', '\\', 'P', 'r', 'o', 'g', 'r', 'a', 'm', ' ', 'F', 'i', 'l', 'e', 's', 0,   'E', 'M', 'P', 'T', 'Y', '=', 0,   0,
        };
        String16 environment_block_string = string16_from_pointer_length(environment_block, BUSTER_ARRAY_LENGTH(expected_environment_block) - 1);
        String16 expected_environment_block_string =
            string16_from_pointer_length(expected_environment_block, BUSTER_ARRAY_LENGTH(expected_environment_block) - 1);
        BUSTER_TEST(arguments, string16_equal(environment_block_string, expected_environment_block_string));

        SliceString8 round_trip = string16_environment_block_to_slice_string(arena, environment_block);
        BUSTER_TEST(arguments, round_trip.length == 3);
        BUSTER_TEST(arguments, string_equal(round_trip.pointer[0], S8("USER=david")));
        BUSTER_TEST(arguments, string_equal(round_trip.pointer[1], S8("PROGRAM_FILES=C:\\Program Files")));
        BUSTER_TEST(arguments, string_equal(round_trip.pointer[2], S8("EMPTY=")));
    }
    {
        SliceString8 environment = {0};
        const char16* environment_block = windows_environment_block_from_slice_string(arena, environment);
        BUSTER_TEST(arguments, environment_block[0] == 0);
    }

    {
        {
            String8 s = S8("hello");
            BUSTER_TEST(arguments, s.length == 5);
        }
        {
            String8 s = S8("ñ");
            BUSTER_TEST(arguments, s.length == 2);
        }
        {
            String8 s = S8("€");
            BUSTER_TEST(arguments, s.length == 3);
        }
        {
            String8 s = S8("😀");
            BUSTER_TEST(arguments, s.length == 4);
        }
    }

    {
        {
            char16 hello_raw[] = {'h', 'e', 'l', 'l', 'o', 0};
            String16 s = (String16){.pointer = hello_raw, .length = BUSTER_ARRAY_LENGTH(hello_raw) - 1};
            BUSTER_TEST(arguments, s.length == 5);
        }
        {
            char16 enye_raw[] = {0x00F1, 0};
            String16 s = (String16){.pointer = enye_raw, .length = BUSTER_ARRAY_LENGTH(enye_raw) - 1};
            BUSTER_TEST(arguments, s.length == 1);
        }
        {
            char16 euro_raw[] = {0x20AC, 0};
            String16 s = (String16){.pointer = euro_raw, .length = BUSTER_ARRAY_LENGTH(euro_raw) - 1};
            BUSTER_TEST(arguments, s.length == 1);
        }
        {
            char16 grinning_face_raw[] = {0xD83D, 0xDE00, 0};
            String16 s = (String16){.pointer = grinning_face_raw, .length = BUSTER_ARRAY_LENGTH(grinning_face_raw) - 1};
            BUSTER_TEST(arguments, s.length == 2);
        }
    }

    return result;
}

#undef BUSTER_UTF16_TO_UTF8_TEST
#undef BUSTER_UNICODE_ROUND_TRIP_TEST
#undef BUSTER_UNICODE_OS_TO_UTF8_TEST
