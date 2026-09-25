#include <buster/tests/string_test.h>
#if BUSTER_INCLUDE_TESTS

#include <buster/lib/os.h>
#include <buster/lib/file.h>

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

BUSTER_GLOBAL_LOCAL u128 string_test_u128(u64 low, u64 high)
{
#if defined(__clang__)
    return ((u128)high << 64) | (u128)low;
#else
    return (u128){.v = {low, high}};
#endif
}

BUSTER_GLOBAL_LOCAL s128 string_test_s128(u64 low, u64 high)
{
#if defined(__clang__)
    return (s128)string_test_u128(low, high);
#else
    return (s128){.v = {low, high}};
#endif
}

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

// Malformed UTF-8 is replaced, so it cannot round trip. Specify the expected
// UTF-16 code units and the expected replaced UTF-8 bytes independently, and
// check the length, the terminator, the code-unit bound the allocation rests
// on, and the reclaimed arena tail the Windows builders append after.
#define BUSTER_UTF8_TO_UTF16_TEST(args, arena_value, utf8_value, utf16_value, replaced_utf8_value)                                                             \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        Arena* unicode_arena = (arena_value);                                                                                                                  \
        String8 unicode_utf8 = (utf8_value);                                                                                                                   \
        String16 unicode_expected_utf16 = (utf16_value);                                                                                                       \
        String16 unicode_utf16 = string16_from_string8(unicode_arena, unicode_utf8, true);                                                                     \
        BUSTER_TEST_RAW((args), unicode_utf16.length == unicode_expected_utf16.length, S8("string16_from_string8 code unit count mismatch"));                  \
        BUSTER_TEST_RAW((args), string16_equal(unicode_utf16, unicode_expected_utf16), S8("string16_from_string8 replacement mismatch"));                      \
        BUSTER_TEST_RAW((args), unicode_utf16.pointer[unicode_utf16.length] == 0, S8("string16_from_string8 did not write a terminator"));                     \
        BUSTER_TEST_RAW((args), unicode_utf16.length <= unicode_utf8.length, S8("string16_from_string8 exceeded its code unit bound"));                        \
        BUSTER_TEST_RAW((args), unicode_utf16.pointer + unicode_utf16.length + 1 == arena_get_current_pointer(unicode_arena, char16),                          \
                        S8("string16_from_string8 left an unreclaimed arena tail"));                                                                           \
        BUSTER_STRING_TEST((args), string8_from_string16(unicode_arena, unicode_utf16, true), (replaced_utf8_value));                                          \
    } while (0)

BUSTER_GLOBAL_LOCAL IntegerParsingU64 string_test_parse_u64(String8 string, u32 base)
{
    IntegerParsingU64 result;
    switch (base)
    {
    case 2:
        result = string8_parse_u64_binary(string);
        break;
    case 8:
        result = string8_parse_u64_octal(string);
        break;
    case 10:
        result = string8_parse_u64_decimal(string);
        break;
    case 16:
        result = string8_parse_u64_hexadecimal(string);
        break;
    default:
        BUSTER_UNREACHABLE();
    }
    return result;
}

UnitTestResult string_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;

    {
        SliceString8 empty = {0};
        PosixStringList empty_list = posix_string_list_from_slice_string(arena, empty);
        PosixStringList empty_environment = posix_environment_from_keys_and_values(arena, empty, empty);
        BUSTER_TEST(arguments, empty_list[0] == 0);
        BUSTER_TEST(arguments, empty_environment[0] == 0);

        u64 large_count = 1024;
        String8* parts = arena_allocate(arena, String8, large_count);
        String8* keys = arena_allocate(arena, String8, large_count);
        String8* values = arena_allocate(arena, String8, large_count);
        for (u64 index = 0; index < large_count; index += 1)
        {
            parts[index] = S8("entry");
            keys[index] = S8("KEY");
            values[index] = S8("VALUE");
        }

        SliceString8 part_slice = {.pointer = parts, .length = large_count};
        SliceString8 key_slice = {.pointer = keys, .length = large_count};
        SliceString8 value_slice = {.pointer = values, .length = large_count};
        PosixStringList large_list = posix_string_list_from_slice_string(arena, part_slice);
        PosixStringList large_environment = posix_environment_from_keys_and_values(arena, key_slice, value_slice);
        bool list_matches = large_list[large_count] == 0;
        bool environment_matches = large_environment[large_count] == 0;
        for (u64 index = 0; index < large_count; index += 1)
        {
            list_matches &= large_list[index] == parts[index].pointer;
            environment_matches &= string_equal(string_from_pointer(large_environment[index]), S8("KEY=VALUE"));
        }
        BUSTER_TEST(arguments, list_matches);
        BUSTER_TEST(arguments, environment_matches);
    }

    {
        String8 empty = {0};
        String8 invalid = {.length = 1};
        String8 x8 = S8("x");
        BUSTER_TEST(arguments, string_equal(x8, x8));
        BUSTER_TEST(arguments, string_equal(empty, S8("")));
        BUSTER_TEST(arguments, string_equal(S8(""), empty));
        BUSTER_TEST(arguments, !string_equal(empty, S8("x")));
        BUSTER_TEST(arguments, !string_equal(S8("x"), empty));
        BUSTER_TEST(arguments, !string_equal(invalid, S8("x")));
        BUSTER_TEST(arguments, !string_equal(S8("x"), invalid));
        BUSTER_TEST(arguments, !string_equal(invalid, invalid));

        char16 x[] = {'x'};
        String16 empty16 = {0};
        String16 invalid16 = {.length = 1};
        String16 x16 = {.pointer = x, .length = BUSTER_ARRAY_LENGTH(x)};
        BUSTER_TEST(arguments, string16_equal(x16, x16));
        BUSTER_TEST(arguments, string16_equal(empty16, (String16){.pointer = x}));
        BUSTER_TEST(arguments, string16_equal((String16){.pointer = x}, empty16));
        BUSTER_TEST(arguments, !string16_equal(invalid16, x16));
        BUSTER_TEST(arguments, !string16_equal(x16, invalid16));
        BUSTER_TEST(arguments, !string16_equal(invalid16, invalid16));
    }

    // Every parser sees the same bounds and status contract, including a
    // digit occupying the last accessible byte and empty input at the guard.
    {
        typedef struct StringIntegerParserCase StringIntegerParserCase;
        struct StringIntegerParserCase
        {
            u32 base;
            String8 maximum;
            String8 overflow;
            String8 mixed_case;
            u64 mixed_value;
            String8 invalid;
        };
        StringIntegerParserCase parsers[] = {
            {10, S8("18446744073709551615"), S8("18446744073709551616"), S8("123"), 123, S8("a")},
            {16, S8("ffffffffffffffff"), S8("10000000000000000"), S8("aBcD"), 43981, S8("g")},
            {8, S8("1777777777777777777777"), S8("2000000000000000000000"), S8("123"), 83, S8("8")},
            {2, S8("1111111111111111111111111111111111111111111111111111111111111111"),
             S8("10000000000000000000000000000000000000000000000000000000000000000"), S8("101"), 5, S8("2")},
        };
        u64 page_size = os_get_page_size();
        // Windows requires a valid protection even for a reservation; leaving
        // the second page uncommitted keeps it inaccessible. POSIX maps both
        // pages without access before os_commit enables only the first one.
        ProtectionFlags reserve_protection = {.read = BUSTER_WINDOWS, .write = BUSTER_WINDOWS};
        MapFlags reserve_flags = {.priv = 1, .anonymous = 1, .no_reserve = 1};
        char8* pages = (char8*)os_reserve(0, page_size * 2, reserve_protection, reserve_flags);
        BUSTER_TEST(arguments, pages != 0);
        bool committed = pages && os_commit(pages, page_size, (ProtectionFlags){.read = 1, .write = 1}, false);
        BUSTER_TEST(arguments, committed);
        {
            char8 suffixed[] = {'f', 'o', 'o', 'b', 'a', 'r'};
            String8Z bounded_copy = {0};
            bool bounded_valid = string8z_copy_arena(arguments->arena, (String8){suffixed, 3}, &bounded_copy);
            String8 bounded_text = {.pointer = bounded_copy.pointer, .length = bounded_copy.length};
            BUSTER_TEST(arguments, bounded_valid && string_equal(bounded_text, S8("foo")) && bounded_copy.pointer[bounded_copy.length] == 0);

            char8 embedded_nul[] = {'f', 'o', 0, 'o'};
            String8Z rejected = bounded_copy;
            bool embedded_valid = string8z_copy_arena(arguments->arena, (String8){embedded_nul, BUSTER_ARRAY_LENGTH(embedded_nul)}, &rejected);
            BUSTER_TEST(arguments, !embedded_valid && !rejected.pointer && !rejected.length);
            bool null_valid = string8z_copy_arena(arguments->arena, (String8){.length = 1}, &rejected);
            BUSTER_TEST(arguments, !null_valid && !rejected.pointer && !rejected.length);

            String16Z wide_copy = {0};
            bool wide_valid = string16z_from_string8_arena(arguments->arena, (String8){suffixed, 3}, &wide_copy);
            String16 wide_text = {.pointer = wide_copy.pointer, .length = wide_copy.length};
            String16 expected_wide = string16_from_string8(arguments->arena, S8("foo"), false);
            BUSTER_TEST(arguments, wide_valid && string16_equal(wide_text, expected_wide) && wide_copy.pointer[wide_copy.length] == 0);

            if (committed)
            {
                char8* last_bytes = pages + page_size - 3;
                last_bytes[0] = 'e';
                last_bytes[1] = 'n';
                last_bytes[2] = 'd';
                String8Z guarded_copy = {0};
                bool guarded_valid = string8z_copy_arena(arguments->arena, (String8){last_bytes, 3}, &guarded_copy);
                String8 guarded_text = {.pointer = guarded_copy.pointer, .length = guarded_copy.length};
                BUSTER_TEST(arguments, guarded_valid && string_equal(guarded_text, S8("end")) && guarded_copy.pointer[guarded_copy.length] == 0);
            }
        }
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(parsers); i += 1)
        {
            StringIntegerParserCase parser = parsers[i];
            // Independent explicit ranges cover every byte, including NUL,
            // high-bit bytes and the punctuation gap between '9' and 'A'.
            for (u32 byte = 0; byte <= UINT8_MAX; byte += 1)
            {
                u32 digit = parser.base;
                if (byte >= '0' && byte <= '9')
                {
                    digit = byte - '0';
                }
                else if (parser.base == 16 && byte >= 'A' && byte <= 'F')
                {
                    digit = byte - 'A' + 10;
                }
                else if (parser.base == 16 && byte >= 'a' && byte <= 'f')
                {
                    digit = byte - 'a' + 10;
                }
                bool valid = digit < parser.base;
                IntegerParsingStatus status = valid ? INTEGER_PARSING_SUCCESS : INTEGER_PARSING_INVALID;
                u64 value = valid ? digit : 0;
                u64 length = valid ? 1 : 0;
                char8 code_unit = (char8)byte;
                IntegerParsingU64 parsed = string_test_parse_u64((String8){.pointer = &code_unit, .length = 1}, parser.base);
                BUSTER_TEST(arguments, parsed.status == status && parsed.value == value && parsed.length == length);
                if (committed)
                {
                    pages[page_size - 1] = code_unit;
                    parsed = string_test_parse_u64((String8){.pointer = pages + page_size - 1, .length = 1}, parser.base);
                    BUSTER_TEST(arguments, parsed.status == status && parsed.value == value && parsed.length == length);
                }
                // A trailing digit must not resume parsing after an invalid byte.
                char8 prefixed[] = {'1', code_unit, '1'};
                u64 prefix_value = valid ? (u64)parser.base * parser.base + digit * parser.base + 1 : 1;
                u64 prefix_length = valid ? BUSTER_ARRAY_LENGTH(prefixed) : 1;
                parsed = string_test_parse_u64((String8){.pointer = prefixed, .length = BUSTER_ARRAY_LENGTH(prefixed)}, parser.base);
                BUSTER_TEST(arguments, parsed.status == INTEGER_PARSING_SUCCESS && parsed.value == prefix_value && parsed.length == prefix_length);
            }
            String8 empty_inputs[] = {S8(""), {0}, {.length = UINT64_MAX}};
            for (u64 j = 0; j < BUSTER_ARRAY_LENGTH(empty_inputs); j += 1)
            {
                IntegerParsingU64 parsed = string_test_parse_u64(empty_inputs[j], parser.base);
                BUSTER_TEST(arguments, parsed.status == INTEGER_PARSING_INVALID && parsed.value == 0 && parsed.length == 0);
            }
            // Neither the largest value nor an overflow may consume punctuation.
            // The longest prefix has 65 binary digits, then a separator and digit.
            String8 boundary_prefixes[] = {parser.maximum, parser.overflow};
            for (u64 j = 0; j < BUSTER_ARRAY_LENGTH(boundary_prefixes); j += 1)
            {
                char8 with_tail[67];
                String8 prefix = boundary_prefixes[j];
                if (BUSTER_REQUIRE(arguments, prefix.length <= BUSTER_ARRAY_LENGTH(with_tail) - 2))
                {
                    memcpy(with_tail, prefix.pointer, prefix.length);
                    with_tail[prefix.length + 1] = '1';
                    IntegerParsingStatus status = j == 0 ? INTEGER_PARSING_SUCCESS : INTEGER_PARSING_OVERFLOW;
                    for (u32 punctuation = ':'; punctuation <= '?'; punctuation += 1)
                    {
                        with_tail[prefix.length] = (char8)punctuation;
                        IntegerParsingU64 parsed = string_test_parse_u64((String8){.pointer = with_tail, .length = prefix.length + 2}, parser.base);
                        BUSTER_TEST(arguments, parsed.status == status && parsed.value == UINT64_MAX && parsed.length == prefix.length);
                    }
                }
            }
            String8 inputs[] = {
                (String8){0}, (String8){.length = 1}, parser.invalid, S8("+1"), S8("-1"),
                S8("0"), S8("000000000000000000000000000000000000000000000000000000000000000000001"),
                S8("10!11"), parser.mixed_case, parser.maximum, parser.overflow,
            };
            u64 expected_values[] = {0, 0, 0, 0, 0, 0, 1, parser.base,
                                     parser.mixed_value, UINT64_MAX, UINT64_MAX};
            for (u64 j = 0; j < BUSTER_ARRAY_LENGTH(inputs); j += 1)
            {
                String8 input = inputs[j];
                IntegerParsingStatus status = j < 5 ? INTEGER_PARSING_INVALID : j == 10 ? INTEGER_PARSING_OVERFLOW : INTEGER_PARSING_SUCCESS;
                u64 length = j < 5 ? 0 : j == 7 ? 2 : input.length;
                IntegerParsingU64 parsed = string_test_parse_u64(input, parser.base);
                BUSTER_TEST(arguments, parsed.status == status && parsed.value == expected_values[j] && parsed.length == length);
                if (committed && input.pointer)
                {
                    char8* guarded = pages + page_size - input.length;
                    memcpy(guarded, input.pointer, input.length);
                    parsed = string_test_parse_u64((String8){.pointer = guarded, .length = input.length}, parser.base);
                    BUSTER_TEST(arguments, parsed.status == status && parsed.value == expected_values[j] && parsed.length == length);
                }
            }
            IntegerParsingU64 bounded = string_test_parse_u64((String8){.pointer = "111", .length = 1}, parser.base);
            BUSTER_TEST(arguments, bounded.status == INTEGER_PARSING_SUCCESS && bounded.value == 1 && bounded.length == 1);
            if (committed)
            {
                IntegerParsingU64 empty_guard = string_test_parse_u64((String8){.pointer = pages + page_size}, parser.base);
                BUSTER_TEST(arguments, empty_guard.status == INTEGER_PARSING_INVALID && empty_guard.value == 0 && empty_guard.length == 0);
                // Overflow still consumes the complete digit prefix and stops
                // before a tail instead of returning a plausible wrapped value.
                memset(pages + page_size - 100, '1', 99);
                pages[page_size - 1] = '!';
                IntegerParsingU64 long_overflow = string_test_parse_u64((String8){.pointer = pages + page_size - 100, .length = 100}, parser.base);
                BUSTER_TEST(arguments, long_overflow.status == INTEGER_PARSING_OVERFLOW &&
                                       long_overflow.value == UINT64_MAX && long_overflow.length == 99);
            }
        }
        if (pages)
        {
            BUSTER_TEST(arguments, os_unreserve(pages, page_size * 2));
        }
    }

    // Synthetic lengths must fail before touching their deliberately tiny
    // backing storage, and failed validation must leave the arena untouched.
    {
        String8 alias = S8("same slice");
        BUSTER_TEST(arguments, string_join_arena_attempt(arena, (SliceString8){.pointer = &alias, .length = 1}, true, &alias));
        BUSTER_STRING_TEST(arguments, alias, S8("same slice"));
        BUSTER_TEST(arguments, alias.pointer[alias.length] == 0);
        alias = (String8){.pointer = "x", .length = UINT64_MAX};
        u64 alias_before = arena->position;
        BUSTER_TEST(arguments, !string_join_arena_attempt(arena, (SliceString8){.pointer = &alias, .length = 1}, true, &alias));
        BUSTER_TEST(arguments, !alias.pointer && !alias.length && arena->position == alias_before);
        String8 overflow[] = {{.pointer = "x", .length = UINT64_MAX}, S8("x")};
        String8 terminator[] = {{.pointer = "x", .length = UINT64_MAX}};
        String8 malformed[] = {S8("prefix"), {.length = 1}};
        String8 capacity[] = {{.pointer = "x", .length = arena->reserved_size - arena->position + 1}};
        SliceString8 invalid[] = {
            (SliceString8)BUSTER_ARRAY_TO_SLICE(overflow),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(terminator),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(malformed),
            {.length = 1},
            (SliceString8)BUSTER_ARRAY_TO_SLICE(terminator),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(capacity),
        };
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
        {
            u64 before = arena->position;
            String8 output = S8("old output");
            bool joined = string_join_arena_attempt(arena, invalid[i], i == 1, &output);
            BUSTER_TEST(arguments, !joined && !output.pointer && !output.length);
            BUSTER_TEST(arguments, arena->position == before);
        }
    }

    // Null-empty strings are valid slices, not valid memcpy arguments. Both
    // termination policies and mixed joins must avoid zero-count null copies.
    for (u32 terminated = 0; terminated < 2; terminated += 1)
    {
        String8 empty = {0};
        String8 copy = string_duplicate_arena(arena, empty, terminated != 0);
        BUSTER_TEST(arguments, copy.length == 0);
        BUSTER_TEST(arguments, !terminated || (copy.pointer && copy.pointer[0] == 0));
        String8 pieces[] = {empty, S8("ab"), empty, S8("cd"), empty};
        String8 joined = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(pieces), terminated != 0);
        BUSTER_STRING_TEST(arguments, joined, S8("abcd"));
        BUSTER_TEST(arguments, !terminated || (joined.pointer && joined.pointer[joined.length] == 0));
        String8 empties[] = {empty, empty, empty};
        joined = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(empties), terminated != 0);
        BUSTER_TEST(arguments, joined.length == 0);
        BUSTER_TEST(arguments, !terminated || (joined.pointer && joined.pointer[0] == 0));
        joined = string_join_arena(arena, (SliceString8){0}, terminated != 0);
        BUSTER_TEST(arguments, joined.length == 0);
        BUSTER_TEST(arguments, !terminated || (joined.pointer && joined.pointer[0] == 0));
    }

    // Fatal string wrappers deliberately terminate for malformed input.
    // A child-mode hook lets the parent test that behavior without terminating
    // the main test process.
    String8 failure_mode = os_get_environment_variable(S8("BUSTER_STRING_FORMAT_FAILURE"));
    if (failure_mode.length)
    {
        if (string_equal(failure_mode, S8("string_join_fail_overflow")))
        {
            String8 parts[] = {{.pointer = "x", .length = UINT64_MAX}, S8("x")};
            string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), false);
            os_exit(0);
        }
        if (string_equal(failure_mode, S8("string_duplicate_fail_null")))
        {
            string_duplicate_arena(arena, (String8){.length = 1}, true);
            os_exit(0);
        }
        if (string_equal(failure_mode, S8("string_format_fail_width_overflow")))
        {
            string_format(arena, S8("{u8:width=[0,18446744073709551617]}"), (u8)1);
            os_exit(0);
        }
        if (string_equal(failure_mode, S8("string_format_fail_brace")))
        {
            string_format(arena, S8("{"));
            os_exit(0);
        }
        if (string_equal(failure_mode, S8("string_format_fail_type")))
        {
            string_format(arena, S8("{unknown}"));
            os_exit(0);
        }
        if (string_equal(failure_mode, S8("string_format_fail_modifier")))
        {
            string_format(arena, S8("{u8:not_a_modifier}"), (u8)1);
            os_exit(0);
        }
    }

    // #106: the parent passes a malformed UTF-8 environment value and reads
    // back what survived the Windows UTF-16 environment block in both
    // directions. Echo before any test below spawns a child, so a child never
    // reaches the spawning code itself.
    String8 boundary_environment_value = os_get_environment_variable(S8("BUSTER_STRING_UNICODE_BOUNDARY"));
    if (boundary_environment_value.length)
    {
        string_print(S8("BUSTER_UNICODE_BOUNDARY_VALUE[{S8}]\n"), boundary_environment_value);
        os_exit(0);
    }

    // string8_format
    {
        // A two-slot aggregate must move wholly to the stack when
        // only x7 remains; it cannot be split across the boundary.
        {
            String8 formatted = string_format(arena, S8("{S8}{S8}{S8}"), S8("a"), S8("b"), S8("c"));
            BUSTER_STRING_TEST(arguments, formatted, S8("abc"));
        }
        {
            String8 formatted =
                string_format(arena, S8("{u64}{u64}{u64}{u64}{S8}"), (u64)1, (u64)2, (u64)3, (u64)4, S8("x"));
            BUSTER_STRING_TEST(arguments, formatted, S8("1234x"));
        }
        // After arguments spill, u128 still starts at a 16-byte
        // stack boundary even when one u64 immediately precedes it.
        {
            u128 seven = string_test_u128(7, 0);
            String8 formatted = string_format(arena, S8("{u64}{u64}{u64}{u64}{u64}{u64}{u128}"), (u64)1, (u64)2,
                                              (u64)3, (u64)4, (u64)5, (u64)6, seven);
            BUSTER_STRING_TEST(arguments, formatted, S8("1234567"));
        }
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
            char16 utf16_bmp[] = {0x00E9};
            char8 expected_utf8[] = {(char8)0xC3, (char8)0xA9};
            String8 formatted = string_format(arena, S8("{S16}"), (String16){.pointer = utf16_bmp, .length = 1});
            BUSTER_STRING_TEST(arguments, formatted, string_from_pointer_length(expected_utf8, 2));
        }
        {
            char16 utf16_pair[] = {0xD83D, 0xDE00};
            char8 expected_utf8[] = {(char8)0xF0, (char8)0x9F, (char8)0x98, (char8)0x80};
            String8 formatted = string_format(arena, S8("{S16}"), (String16){.pointer = utf16_pair, .length = 2});
            BUSTER_STRING_TEST(arguments, formatted, string_from_pointer_length(expected_utf8, 4));
        }
        {
            char16 utf16_invalid[] = {0xD83D, 0xDE00, 0xD83D, 0x0041, 0xDE00};
            char8 expected_utf8[] = {
                (char8)0xF0, (char8)0x9F, (char8)0x98, (char8)0x80,
                (char8)0xEF, (char8)0xBF, (char8)0xBD,
                'A',
                (char8)0xEF, (char8)0xBF, (char8)0xBD,
            };
            String8 formatted = string_format(arena, S8("{S16}"), (String16){.pointer = utf16_invalid, .length = 5});
            BUSTER_STRING_TEST(arguments, formatted, string_from_pointer_length(expected_utf8, 11));
        }
        {
#if defined(_WIN32)
            WindowsChar os_list_raw[] = {'o', 'n', 'e', ' ', '"', 't', 'w', 'o', ' ', 'w', 'o', 'r', 'd', 's', '"', 0};
            StringOsList os_list = os_list_raw;
            String8 formatted = string_format(arena, S8("{SOsL}|{CharOs}"), os_list, (CharOs)0x20AC);
            char8 expected_utf8[] = {'o', 'n', 'e', ' ', 't', 'w', 'o', ' ', 'w', 'o', 'r', 'd', 's', '|',
                                     (char8)0xE2, (char8)0x82, (char8)0xAC};
            BUSTER_STRING_TEST(arguments, formatted, string_from_pointer_length(expected_utf8, 17));
#else
            PosixChar first[] = "one";
            PosixChar second[] = "two words";
            PosixChar* os_list_raw[] = {first, second, 0};
            StringOsList os_list = os_list_raw;
            String8 formatted = string_format(arena, S8("{SOsL}|{CharOs}"), os_list, (CharOs)'@');
            BUSTER_STRING_TEST(arguments, formatted, S8("one two words|@"));
#endif
        }
        {
            u128 zero = string_test_u128(0, 0);
            u128 maximum = string_test_u128(UINT64_MAX, UINT64_MAX);
            u128 mixed = string_test_u128(0xFEDCBA9876543210ull, 0x0123456789ABCDEFull);
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{u128}"), zero), S8("0"));
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{u128}"), maximum), S8("340282366920938463463374607431768211455"));
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{u128:d}"), mixed), S8("0d1512366075204170947332355369683137040"));
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{u128:x}"), mixed), S8("0x123456789abcdeffedcba9876543210"));
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{u128:X}"), mixed), S8("0x123456789ABCDEFFEDCBA9876543210"));
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{u128:o,no_prefix}"), mixed), S8("11064254742325715737773345651416625031020"));
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{u128:b,no_prefix}"), mixed),
                               S8("1001000110100010101100111100010011010101111001101111011111111111011011100101110101001100001110110010101000011001000010000"));
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{u128:x,digit_group,no_prefix}"), mixed),
                               S8("1_23_45_67_89_ab_cd_ef_fe_dc_ba_98_76_54_32_10"));
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{u128:x,width=[0,x]}"), mixed), S8("0x0123456789abcdeffedcba9876543210"));
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{u128:x,width=[0,32],digit_group}"), zero),
                               S8("0x00000000000000000000000000000000"));
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{u128}{u32}"), maximum, (u32)7),
                               S8("3402823669209384634633746074317682114557"));
        }
        {
            s128 minimum = string_test_s128(0, (u64)1 << 63);
            s128 negative_one = string_test_s128(UINT64_MAX, UINT64_MAX);
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{s128}"), minimum), S8("-170141183460469231731687303715884105728"));
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{s128:x,no_prefix}"), minimum), S8("80000000000000000000000000000000"));
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{s128:X,no_prefix}"), negative_one), S8("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"));
            BUSTER_STRING_TEST(arguments, string_format(arena, S8("{s128:digit_group}"), minimum),
                               S8("-170.141.183.460.469.231.731.687.303.715.884.105.728"));
        }
        {
#if BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS
            String8 failure_modes[] = {
                S8("string_format_fail_brace"),
                S8("string_format_fail_type"),
                S8("string_format_fail_modifier"),
                S8("string_format_fail_width_overflow"),
                S8("string_join_fail_overflow"),
                S8("string_duplicate_fail_null"),
            };
            String8 executable = program_state->input.arguments.pointer[0];
            for (u64 mode_index = 0; mode_index < BUSTER_ARRAY_LENGTH(failure_modes); mode_index += 1)
            {
                String8 child_arguments[] = {
                    executable,
                    S8("test"),
                };
                String8 environment_keys[] = {S8("BUSTER_STRING_FORMAT_FAILURE")};
                String8 environment_values[] = {failure_modes[mode_index]};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
                                                             (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_keys),
                                                             (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_values),
                                                             (ProcessSpawnOptions){.capture = (u64)1 << STANDARD_STREAM_ERROR});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    ProcessWaitResult wait_result = os_process_wait_sync(arena, spawn);
                    String8 error = (String8){.pointer = (char8*)wait_result.streams[STANDARD_STREAM_ERROR].pointer,
                                             .length = wait_result.streams[STANDARD_STREAM_ERROR].length};
                    BUSTER_TEST(arguments, wait_result.result == PROCESS_RESULT_FAILED);
                    BUSTER_TEST(arguments, string_first_sequence(error, S8("TODO")) == BUSTER_STRING_NO_MATCH);
                }
            }
#endif
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
        {
            // #106: the Windows UTF-16 boundary replaces malformed UTF-8 with
            // U+FFFD instead of reinterpreting the byte as the scalar with
            // that value. Every case below states the expected code units and
            // the expected replaced bytes independently of the conversion.
            {
                // Empty input allocates nothing but its terminator.
                char16 utf16_bytes[] = {0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, S8(""), string16_from_pointer_length(utf16_bytes, 0), S8(""));
            }
            {
                // ASCII is untouched.
                char16 utf16_bytes[] = {'A', 'Z', '0', '9', 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, S8("AZ09"), string16_from_pointer_length(utf16_bytes, 4), S8("AZ09"));
            }
            {
                // The shortest form at each sequence length, and the last
                // scalar below the surrogate block and the first above it.
                char8 utf8_bytes[] = {(char8)0xC2, (char8)0x80, (char8)0xE0, (char8)0xA0, (char8)0x80,
                                      (char8)0xF0, (char8)0x90, (char8)0x80, (char8)0x80, 0};
                char16 utf16_bytes[] = {0x0080, 0x0800, 0xD800, 0xDC00, 0};
                BUSTER_UNICODE_ROUND_TRIP_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 9), string16_from_pointer_length(utf16_bytes, 4));
            }
            {
                char8 utf8_bytes[] = {(char8)0xED, (char8)0x9F, (char8)0xBF, (char8)0xEE, (char8)0x80, (char8)0x80, 0};
                char16 utf16_bytes[] = {0xD7FF, 0xE000, 0};
                BUSTER_UNICODE_ROUND_TRIP_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 6), string16_from_pointer_length(utf16_bytes, 2));
            }
            {
                // Invalid leading bytes: 0xFF and 0xFE begin no sequence.
                char8 utf8_bytes[] = {'a', (char8)0xFF, (char8)0xFE, 'b', 0};
                char16 utf16_bytes[] = {'a', 0xFFFD, 0xFFFD, 'b', 0};
                char8 replaced_bytes[] = {'a', (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, 'b', 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 4), string16_from_pointer_length(utf16_bytes, 4),
                                          string_from_pointer_length(replaced_bytes, 8));
            }
            {
                // Continuation bytes with no leader in front of them.
                char8 utf8_bytes[] = {(char8)0x80, (char8)0xBF, 0};
                char16 utf16_bytes[] = {0xFFFD, 0xFFFD, 0};
                char8 replaced_bytes[] = {(char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 2), string16_from_pointer_length(utf16_bytes, 2),
                                          string_from_pointer_length(replaced_bytes, 6));
            }
            {
                // Overlong two-byte forms for U+0000 and U+007F.
                char8 utf8_bytes[] = {(char8)0xC0, (char8)0x80, (char8)0xC1, (char8)0xBF, 0};
                char16 utf16_bytes[] = {0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0};
                char8 replaced_bytes[] = {(char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD,
                                          (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 4), string16_from_pointer_length(utf16_bytes, 4),
                                          string_from_pointer_length(replaced_bytes, 12));
            }
            {
                // An overlong three-byte form for U+002F, the encoding a path
                // separator would hide behind.
                char8 utf8_bytes[] = {(char8)0xE0, (char8)0x80, (char8)0xAF, 0};
                char16 utf16_bytes[] = {0xFFFD, 0xFFFD, 0xFFFD, 0};
                char8 replaced_bytes[] = {(char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 3), string16_from_pointer_length(utf16_bytes, 3),
                                          string_from_pointer_length(replaced_bytes, 9));
            }
            {
                // An overlong four-byte form for U+0000.
                char8 utf8_bytes[] = {(char8)0xF0, (char8)0x80, (char8)0x80, (char8)0x80, 0};
                char16 utf16_bytes[] = {0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0};
                char8 replaced_bytes[] = {(char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD,
                                          (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 4), string16_from_pointer_length(utf16_bytes, 4),
                                          string_from_pointer_length(replaced_bytes, 12));
            }
            {
                // A two-byte sequence truncated by the end of the input.
                char8 utf8_bytes[] = {'x', (char8)0xC3, 0};
                char16 utf16_bytes[] = {'x', 0xFFFD, 0};
                char8 replaced_bytes[] = {'x', (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 2), string16_from_pointer_length(utf16_bytes, 2),
                                          string_from_pointer_length(replaced_bytes, 4));
            }
            {
                // A three-byte sequence truncated by the end of the input.
                char8 utf8_bytes[] = {(char8)0xE2, (char8)0x82, 0};
                char16 utf16_bytes[] = {0xFFFD, 0xFFFD, 0};
                char8 replaced_bytes[] = {(char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 2), string16_from_pointer_length(utf16_bytes, 2),
                                          string_from_pointer_length(replaced_bytes, 6));
            }
            {
                // A four-byte sequence truncated by the end of the input.
                char8 utf8_bytes[] = {(char8)0xF0, (char8)0x9F, (char8)0x98, 0};
                char16 utf16_bytes[] = {0xFFFD, 0xFFFD, 0xFFFD, 0};
                char8 replaced_bytes[] = {(char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 3), string16_from_pointer_length(utf16_bytes, 3),
                                          string_from_pointer_length(replaced_bytes, 9));
            }
            {
                // A bad continuation inside a three-byte sequence. Only the
                // leader is replaced; the ASCII byte that interrupted it keeps
                // its own meaning.
                char8 utf8_bytes[] = {(char8)0xE2, '(', (char8)0xA1, 0};
                char16 utf16_bytes[] = {0xFFFD, '(', 0xFFFD, 0};
                char8 replaced_bytes[] = {(char8)0xEF, (char8)0xBF, (char8)0xBD, '(', (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 3), string16_from_pointer_length(utf16_bytes, 3),
                                          string_from_pointer_length(replaced_bytes, 7));
            }
            {
                // A bad continuation inside a four-byte sequence.
                char8 utf8_bytes[] = {(char8)0xF0, (char8)0x9F, 'A', (char8)0x80, 0};
                char16 utf16_bytes[] = {0xFFFD, 0xFFFD, 'A', 0xFFFD, 0};
                char8 replaced_bytes[] = {(char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD,
                                          'A',        (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 4), string16_from_pointer_length(utf16_bytes, 4),
                                          string_from_pointer_length(replaced_bytes, 10));
            }
            {
                // UTF-16 surrogates encoded as UTF-8: U+D800 and U+DFFF.
                char8 utf8_bytes[] = {(char8)0xED, (char8)0xA0, (char8)0x80, (char8)0xED, (char8)0xBF, (char8)0xBF, 0};
                char16 utf16_bytes[] = {0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0};
                char8 replaced_bytes[] = {(char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD,
                                          (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 6), string16_from_pointer_length(utf16_bytes, 6),
                                          string_from_pointer_length(replaced_bytes, 18));
            }
            {
                // U+110000, one past the last scalar, and a leader that can
                // only ever encode an out-of-range scalar.
                char8 utf8_bytes[] = {(char8)0xF4, (char8)0x90, (char8)0x80, (char8)0x80, (char8)0xF5, (char8)0x80, (char8)0x80, (char8)0x80, 0};
                char16 utf16_bytes[] = {0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0};
                char8 replaced_bytes[] = {(char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF,
                                          (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF,
                                          (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 8), string16_from_pointer_length(utf16_bytes, 8),
                                          string_from_pointer_length(replaced_bytes, 24));
            }
            {
                // Valid ASCII, BMP and supplementary text either side of the
                // replaced bytes keeps its own encoding.
                char8 utf8_bytes[] = {'A',         (char8)0xC3, (char8)0xA9, (char8)0xFF, (char8)0xF0,
                                      (char8)0x9F, (char8)0x98, (char8)0x80, (char8)0x80, 0};
                char16 utf16_bytes[] = {'A', 0x00E9, 0xFFFD, 0xD83D, 0xDE00, 0xFFFD, 0};
                char8 replaced_bytes[] = {'A',         (char8)0xC3, (char8)0xA9, (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xF0,
                                          (char8)0x9F, (char8)0x98, (char8)0x80, (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF8_TO_UTF16_TEST(arguments, arena, string_from_pointer_length(utf8_bytes, 9), string16_from_pointer_length(utf16_bytes, 6),
                                          string_from_pointer_length(replaced_bytes, 13));
            }
        }
        {
            // Isolated UTF-16 surrogates are replaced one code unit at a time,
            // including a high surrogate standing at the end of the input and
            // one standing directly in front of a well-formed pair.
            {
                char16 utf16_bytes[] = {'A', 0xD83D, 0};
                char8 utf8_bytes[] = {'A', (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF16_TO_UTF8_TEST(arguments, arena, string16_from_pointer_length(utf16_bytes, 2), string_from_pointer_length(utf8_bytes, 4));
            }
            {
                char16 utf16_bytes[] = {0xDBFF, 0};
                char8 utf8_bytes[] = {(char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                BUSTER_UTF16_TO_UTF8_TEST(arguments, arena, string16_from_pointer_length(utf16_bytes, 1), string_from_pointer_length(utf8_bytes, 3));
            }
            {
                char16 utf16_bytes[] = {0xD83D, 0xD83D, 0xDE00, 0};
                char8 utf8_bytes[] = {(char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xF0, (char8)0x9F, (char8)0x98, (char8)0x80, 0};
                BUSTER_UTF16_TO_UTF8_TEST(arguments, arena, string16_from_pointer_length(utf16_bytes, 3), string_from_pointer_length(utf8_bytes, 7));
            }
        }
        {
            // The Windows list builders append each converted fragment at the
            // arena cursor, so replacement has to leave that cursor exactly at
            // the end of the text it wrote. Check the whole block, both
            // terminators included, against independently written code units.
            char8 key_bytes[] = {'K', (char8)0xFF, 0};
            char8 value_bytes[] = {(char8)0xC3, 'v', 0};
            String8 keys[] = {string_from_pointer_length(key_bytes, 2), S8("B")};
            String8 values[] = {string_from_pointer_length(value_bytes, 2), S8("2")};
            const char16 expected_block[] = {'K', 0xFFFD, '=', 0xFFFD, 'v', 0, 'B', '=', '2', 0, 0};
            WindowsStringList block = windows_environment_from_keys_and_values(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(keys),
                                                                               (SliceString8)BUSTER_ARRAY_TO_SLICE(values));
            BUSTER_TEST(arguments, string16_equal(string16_from_pointer_length(block, BUSTER_ARRAY_LENGTH(expected_block)),
                                                  string16_from_pointer_length(expected_block, BUSTER_ARRAY_LENGTH(expected_block))));
        }
        {
            // The same for the command-line builder. These bytes need no
            // quoting, so the replacement is the only thing that changes.
            char8 argument_bytes[] = {'a', (char8)0xE2, (char8)0x82, 'z', 0};
            String8 parts[] = {string_from_pointer_length(argument_bytes, 4), S8("b")};
            const char16 expected_list[] = {'a', 0xFFFD, 0xFFFD, 'z', ' ', 'b', 0};
            WindowsStringList list = windows_string_list_from_slice_string(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts));
            BUSTER_TEST(arguments, string16_equal(string16_from_pointer_length(list, BUSTER_ARRAY_LENGTH(expected_list)),
                                                  string16_from_pointer_length(expected_list, BUSTER_ARRAY_LENGTH(expected_list))));
        }
#if BUSTER_WINDOWS
        {
            // Native acceptance. The conversions above are portable, so they
            // cannot show what Windows itself receives. Drive a real path
            // through the wide file API and a real argument and environment
            // value through CreateProcessW (#106).
            {
                // A file created through a malformed UTF-8 path is the file the
                // replaced path names: both reach the OS as the same UTF-16.
                // The two paths share a root and a process id and differ only
                // in the suffix written here.
                char8 malformed_suffix_bytes[] = {'-', (char8)0xFF, (char8)0xC3, '.', 'b', 'i', 'n', 0};
                char8 replaced_suffix_bytes[] = {'-', (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, '.', 'b', 'i', 'n', 0};
                String8 malformed_path = buster_test_temporary_path(arena, S8("unicode-boundary"), string_from_pointer_length(malformed_suffix_bytes, 7));
                String8 replaced_path = buster_test_temporary_path(arena, S8("unicode-boundary"), string_from_pointer_length(replaced_suffix_bytes, 11));
                if (BUSTER_REQUIRE(arguments, malformed_path.length != 0 && replaced_path.length != 0))
                {
                    u8 payload_bytes[] = {'b', 'o', 'u', 'n', 'd', 'a', 'r', 'y'};
                    ByteSlice payload = {payload_bytes, BUSTER_ARRAY_LENGTH(payload_bytes)};
                    os_file_delete(replaced_path);
                    BUSTER_TEST(arguments, file_write(malformed_path, payload));
                    ByteSlice read_back = file_read(arena, replaced_path, (FileReadOptions){0});
                    if (BUSTER_REQUIRE(arguments, read_back.length == payload.length))
                    {
                        BUSTER_TEST(arguments, memory_compare(read_back.pointer, payload.pointer, payload.length));
                    }
                    BUSTER_TEST(arguments, os_file_delete(replaced_path));
                }
            }
            {
                // A malformed argument crosses into the child's command line
                // and back out of its own argv parse. The unsupported-option
                // diagnostic echoes exactly what the child ended up holding,
                // and the child fails before running any test.
                char8 malformed_argument_bytes[] = {'-', '-', (char8)0xFF, (char8)0xC2, 0};
                char8 replaced_argument_bytes[] = {'-', '-', (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, 0};
                String8 child_arguments[] = {
                    program_state->input.arguments.pointer[0],
                    S8("test"),
                    string_from_pointer_length(malformed_argument_bytes, 4),
                };
                String8 environment_keys[] = {S8("BUSTER_UNICODE_BOUNDARY_ARGUMENT_CHILD")};
                String8 environment_values[] = {S8("1")};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
                                                            (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_keys),
                                                            (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_values),
                                                            (ProcessSpawnOptions){.capture = (u64)1 << STANDARD_STREAM_OUTPUT});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    ProcessWaitResult wait_result = os_process_wait_sync(arena, spawn);
                    String8 output = (String8){.pointer = (char8*)wait_result.streams[STANDARD_STREAM_OUTPUT].pointer,
                                               .length = wait_result.streams[STANDARD_STREAM_OUTPUT].length};
                    String8 expected =
                        string_format(arena, S8("test: unsupported option: {S8}\n"), string_from_pointer_length(replaced_argument_bytes, 8));
                    BUSTER_TEST(arguments, wait_result.result == PROCESS_RESULT_FAILED);
                    BUSTER_TEST(arguments, string_first_sequence(output, expected) != BUSTER_STRING_NO_MATCH);
                }
            }
            {
                // A malformed environment value crosses the UTF-16 environment
                // block in both directions. The child inherits this process's
                // environment plus the echo key, so everything it runs before
                // the echo still has what it needs.
                char8 malformed_value_bytes[] = {'v', (char8)0xFF, (char8)0xE0, (char8)0x80, 'w', 0};
                char8 replaced_value_bytes[] = {'v',         (char8)0xEF, (char8)0xBF, (char8)0xBD, (char8)0xEF, (char8)0xBF,
                                                (char8)0xBD, (char8)0xEF, (char8)0xBF, (char8)0xBD, 'w',         0};
                String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test")};
                SliceString8 inherited_keys = program_state->input.environment_keys;
                SliceString8 inherited_values = program_state->input.environment_values;
                u64 inherited_count = BUSTER_MIN(inherited_keys.length, inherited_values.length);
                String8* child_keys = arena_allocate(arena, String8, inherited_count + 1);
                String8* child_values = arena_allocate(arena, String8, inherited_count + 1);
                for (u64 i = 0; i < inherited_count; i += 1)
                {
                    child_keys[i] = inherited_keys.pointer[i];
                    child_values[i] = inherited_values.pointer[i];
                }
                child_keys[inherited_count] = S8("BUSTER_STRING_UNICODE_BOUNDARY");
                child_values[inherited_count] = string_from_pointer_length(malformed_value_bytes, 5);
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
                                                            (SliceString8){.pointer = child_keys, .length = inherited_count + 1},
                                                            (SliceString8){.pointer = child_values, .length = inherited_count + 1},
                                                            (ProcessSpawnOptions){.capture = (u64)1 << STANDARD_STREAM_OUTPUT});
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (spawn.handle)
                {
                    ProcessWaitResult wait_result = os_process_wait_sync(arena, spawn);
                    String8 output = (String8){.pointer = (char8*)wait_result.streams[STANDARD_STREAM_OUTPUT].pointer,
                                               .length = wait_result.streams[STANDARD_STREAM_OUTPUT].length};
                    String8 expected =
                        string_format(arena, S8("BUSTER_UNICODE_BOUNDARY_VALUE[{S8}]\n"), string_from_pointer_length(replaced_value_bytes, 11));
                    BUSTER_TEST(arguments, wait_result.result == PROCESS_RESULT_SUCCESS);
                    BUSTER_TEST(arguments, string_first_sequence(output, expected) != BUSTER_STRING_NO_MATCH);
                }
            }
        }
#endif

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
        // Keep the parser's existing treatment of empty and unmatched quotes,
        // adjacent quoted/unquoted fragments, and non-separator whitespace.
        {
            typedef struct WindowsCommandLineCase WindowsCommandLineCase;
            struct WindowsCommandLineCase
            {
                String8 input;
                u64 count;
                String8 expected[3];
            };
            WindowsCommandLineCase cases[] = {
                {S8(""), 0, {{0}}},
                {S8(" \t \t"), 0, {{0}}},
                {S8("\"\""), 1, {S8("")}},
                {S8("\"\" \"\" \"\""), 3, {S8(""), S8(""), S8("")}},
                // Two quotes inside a quoted argument are one literal quote
                // (#637). The pair is consumed and quoting continues, so the
                // argument does not end here and the quote reaches argv.
                {S8("\"a\"\"b\""), 1, {S8("a\"b")}},
                {S8("a\" b\"c d"), 2, {S8("a bc"), S8("d")}},
                {S8("\"unterminated a b"), 1, {S8("unterminated a b")}},
                {S8("a\r\nb c"), 2, {S8("a\r\nb"), S8("c")}},
                {S8("\"\"a a\"\" \"\"\"\""), 3, {S8("a"), S8("a"), S8("\"")}},
                // The rule applies only while quoted: outside a quoted run the
                // same two characters open and immediately close one.
                {S8("a\"\"b"), 1, {S8("ab")}},
                // The three raw command lines reported in #637, with the
                // executable name in front as a real command line carries it.
                {S8("prog \"a\"\"b\""), 2, {S8("prog"), S8("a\"b")}},
                {S8("prog \"a\"\" b\" tail"), 3, {S8("prog"), S8("a\" b"), S8("tail")}},
                {S8("prog \"-DNAME=\"\"hello\"\"\""), 2, {S8("prog"), S8("-DNAME=\"hello\"")}},
                // A literal quote produced by the pair, standing at the end of
                // a quoted run and against an argument boundary.
                {S8("\"a\"\"\" b"), 2, {S8("a\""), S8("b")}},
                // Adjacent backslash runs. An even run before the pair decodes
                // to half as many backslashes and still leaves the argument
                // quoted; an odd run escapes its own quote and leaves quoting
                // untouched, so the pair that follows is still the literal one.
                {S8("\"a\\\\\"\"b\""), 1, {S8("a\\\"b")}},
                {S8("\"a\\\"\"\"b\""), 1, {S8("a\"\"b")}},
                // Empty arguments on both sides of a pair-bearing argument.
                {S8("\"\" \"a\"\"b\" \"\""), 3, {S8(""), S8("a\"b"), S8("")}},
            };
            u64 before_null = arena->position;
            SliceString8 null_parts = slice_string_from_windows_string_list(arena, 0);
            BUSTER_TEST(arguments, null_parts.length == 0 && null_parts.pointer == 0 && arena->position == before_null);
            for (u64 case_i = 0; case_i < BUSTER_ARRAY_LENGTH(cases); case_i += 1)
            {
                WindowsCommandLineCase test_case = cases[case_i];
                String16 command_line = string16_from_string8(arena, test_case.input, true);
                SliceString8 parts = slice_string_from_windows_string_list(arena, command_line.pointer);
                BUSTER_TEST(arguments, parts.length == test_case.count);
                for (u64 i = 0; i < BUSTER_MIN(parts.length, test_case.count); i += 1)
                {
                    BUSTER_STRING_TEST(arguments, parts.pointer[i], test_case.expected[i]);
                    BUSTER_TEST(arguments, parts.pointer[i].pointer[parts.pointer[i].length] == 0);
                }
            }
        }
        // A backslash run is literal unless followed by a quote. Before a
        // quote, pairs decode to slashes and the odd slash escapes the quote;
        // an even run toggles quoting. Check both initial quote states.
        for (u64 quoted = 0; quoted < 2; quoted += 1)
        {
            for (u64 quote_after = 0; quote_after < 2; quote_after += 1)
            {
                for (u64 slash_count = 0; slash_count <= 65; slash_count += 1)
                {
                    char16 command_line[80];
                    char8 expected[80];
                    u64 input_length = 0;
                    u64 expected_length = 0;
                    if (quoted)
                    {
                        command_line[input_length++] = '"';
                    }
                    command_line[input_length++] = 'x';
                    expected[expected_length++] = 'x';
                    for (u64 i = 0; i < slash_count; i += 1)
                    {
                        command_line[input_length++] = '\\';
                    }
                    for (u64 i = 0; i < (quote_after ? slash_count / 2 : slash_count); i += 1)
                    {
                        expected[expected_length++] = '\\';
                    }
                    if (quote_after)
                    {
                        command_line[input_length++] = '"';
                        if (slash_count & 1)
                        {
                            expected[expected_length++] = '"';
                        }
                    }
                    command_line[input_length++] = ' ';
                    command_line[input_length++] = 'y';
                    command_line[input_length] = 0;
                    bool space_is_quoted = (quoted != 0) != (quote_after && !(slash_count & 1));
                    if (space_is_quoted)
                    {
                        expected[expected_length++] = ' ';
                        expected[expected_length++] = 'y';
                    }
                    SliceString8 parts = slice_string_from_windows_string_list(arena, command_line);
                    BUSTER_TEST(arguments, parts.length == (space_is_quoted ? 1u : 2u));
                    if (parts.length)
                    {
                        BUSTER_STRING_TEST(arguments, parts.pointer[0], string_from_pointer_length(expected, expected_length));
                    }
                    if (!space_is_quoted && parts.length == 2)
                    {
                        BUSTER_STRING_TEST(arguments, parts.pointer[1], S8("y"));
                    }
                    for (u64 i = 0; i < parts.length; i += 1)
                    {
                        BUSTER_TEST(arguments, parts.pointer[i].pointer[parts.pointer[i].length] == 0);
                    }
                }
            }
        }
        {
            // UTF-8 output must survive reuse of the UTF-16 decode buffer,
            // including surrogate pairs, replacement characters and empties.
            char16 command_line[] = {'"', 0x00E9, ' ', 0xD83D, 0xDE00, '"', ' ', 0xD83D, 'A', ' ', 0xDE00, ' ', '"', '"', 0};
            String8 expected[] = {S8("\xC3\xA9 \xF0\x9F\x98\x80"), S8("\xEF\xBF\xBD" "A"), S8("\xEF\xBF\xBD"), S8("")};
            SliceString8 parts = slice_string_from_windows_string_list(arena, command_line);
            BUSTER_TEST(arguments, parts.length == BUSTER_ARRAY_LENGTH(expected));
            for (u64 i = 0; i < BUSTER_MIN(parts.length, BUSTER_ARRAY_LENGTH(expected)); i += 1)
            {
                BUSTER_STRING_TEST(arguments, parts.pointer[i], expected[i]);
                BUSTER_TEST(arguments, parts.pointer[i].pointer[parts.pointer[i].length] == 0);
            }
        }
        {
            // CreateProcessW allows 32,767 UTF-16 units including the NUL.
            // Geometric inputs reach that limit. A fresh, bounded arena
            // includes descriptors, decode scratch and all UTF-8 output in
            // both the retained and high-water measurements.
            const u64 command_line_units = 32767;
            const u64 counts[] = {4096, 8192, (command_line_units - 1) / 2};
            bool report_storage = os_get_environment_variable(S8("BUSTER_WINDOWS_ARGV_STORAGE")).length != 0;
            for (u64 shape = 0; shape < 3; shape += 1)
            {
                for (u64 case_i = 0; case_i < BUSTER_ARRAY_LENGTH(counts); case_i += 1)
                {
                    u64 input_length = 2 * counts[case_i];
                    char16* command_line = arena_allocate(arena, char16, input_length + 1);
                    for (u64 i = 0; i < input_length; i += 1)
                    {
                        // Many one-character arguments, consecutive empty quoted
                        // arguments, and one maximally expanding BMP argument.
                        if (shape == 2)
                        {
                            command_line[i] = 0x20AC;
                        }
                        else if (shape == 1)
                        {
                            command_line[i] = i % 3 == 2 ? ' ' : '"';
                        }
                        else
                        {
                            command_line[i] = (i & 1) ? ((i & 2) ? '\t' : ' ') : (char16)('a' + (i / 2) % 26);
                        }
                    }
                    command_line[input_length] = 0;
                    Arena* parse_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .initial_size = BUSTER_KB(64), .flags = {.no_pool = 1}});
                    BUSTER_TEST(arguments, parse_arena != 0);
                    if (parse_arena)
                    {
                        u64 before = parse_arena->position;
                        SliceString8 parts = slice_string_from_windows_string_list(parse_arena, command_line);
                        u64 retained = parse_arena->position - before;
                        u64 peak = arena_dirty_position(parse_arena) - before;
                        // 24 bytes per input unit permits descriptors, UTF-16
                        // scratch, worst-case UTF-8 and terminators/alignment.
                        u64 bound = 24 * (input_length + 1);
                        BUSTER_TEST(arguments, retained <= bound && peak <= bound);
                        u64 expected_count = shape == 2 ? 1 : shape == 1 ? (input_length + 2) / 3 : counts[case_i];
                        BUSTER_TEST(arguments, parts.length == expected_count);
                        for (u64 i = 0; i < parts.length; i += 1)
                        {
                            String8 part = parts.pointer[i];
                            BUSTER_TEST(arguments, part.pointer[part.length] == 0);
                            if (shape == 2)
                            {
                                BUSTER_TEST(arguments, part.length == 3 * input_length);
                                for (u64 byte_i = 0; byte_i + 2 < part.length; byte_i += 3)
                                {
                                    BUSTER_TEST(arguments, (u8)part.pointer[byte_i] == 0xE2 && (u8)part.pointer[byte_i + 1] == 0x82 && (u8)part.pointer[byte_i + 2] == 0xAC);
                                }
                            }
                            else
                            {
                                BUSTER_TEST(arguments, shape == 1 ? part.length == 0 : part.length == 1 && part.pointer[0] == 'a' + i % 26);
                            }
                        }
                        if (report_storage)
                        {
                            string_print(S8("WINDOWS_ARGV_STORAGE shape={u64} units={u64} arguments={u64} before={u64} retained={u64} peak={u64}\n"),
                                         shape, input_length, parts.length, before, retained, peak);
                        }
                        BUSTER_TEST(arguments, arena_destroy(parse_arena, 1));
                    }
                }
            }
        }
    }

    {
        // #638: an argument builder flushes a valid slice at every initial
        // arena alignment, including the empty one. `start` used to round its
        // saved position up while leaving the cursor where it was, so an empty
        // flush subtracted the larger saved start from the smaller cursor and
        // the unsigned byte count underflowed. Walk every residue of String8's
        // alignment on an already dirty arena and check the empty,
        // single-argument and multi-argument shapes against exact lengths,
        // exact bytes, and the cursor the builder is supposed to leave behind.
        String8 appended[] = {
            S8("driver"), S8("--flag"), S8(""), S8("value with spaces"),
        };
        for (u64 residue = 0; residue < BUSTER_ALIGN_OF(String8); residue += 1)
        {
            for (u64 count = 0; count <= BUSTER_ARRAY_LENGTH(appended); count += 1)
            {
                u64 restore = arena->position;
                u64 unaligned = align_forward(arena->position, BUSTER_ALIGN_OF(String8)) + residue;
                (void)arena_allocate(arena, char8, unaligned - arena->position);
                BUSTER_TEST(arguments, arena->position == unaligned);

                OsArgumentBuilder builder = os_argument_builder_start(arena);
                for (u64 i = 0; i < count; i += 1)
                {
                    os_argument_builder_append(&builder, appended[i]);
                }
                SliceString8 flushed = os_argument_builder_flush(&builder);
                u64 expected_start = align_forward(unaligned, BUSTER_ALIGN_OF(String8));

                BUSTER_TEST(arguments, flushed.length == count);
                BUSTER_TEST(arguments, arena->position == expected_start + count * sizeof(String8));
                BUSTER_TEST(arguments, (u8*)flushed.pointer == arena_get_byte_pointer_align(arena, expected_start, BUSTER_ALIGN_OF(String8)));
                for (u64 i = 0; i < BUSTER_MIN(flushed.length, count); i += 1)
                {
                    BUSTER_STRING_TEST(arguments, flushed.pointer[i], appended[i]);
                }
                arena_set_position(arena, restore);
            }
        }
        // Two builders in sequence on the same arena: the second starts from
        // the cursor the first one left and neither observes the other's
        // elements. The reused arena is dirty from every case above.
        {
            u64 restore = arena->position;
            (void)arena_allocate(arena, char8, 1);
            OsArgumentBuilder first = os_argument_builder_start(arena);
            os_argument_builder_append(&first, S8("first"));
            SliceString8 first_flushed = os_argument_builder_flush(&first);
            OsArgumentBuilder second = os_argument_builder_start(arena);
            SliceString8 second_flushed = os_argument_builder_flush(&second);
            OsArgumentBuilder third = os_argument_builder_start(arena);
            os_argument_builder_append(&third, S8("third"));
            os_argument_builder_append(&third, S8("fourth"));
            SliceString8 third_flushed = os_argument_builder_flush(&third);

            BUSTER_TEST(arguments, first_flushed.length == 1);
            BUSTER_TEST(arguments, second_flushed.length == 0);
            BUSTER_TEST(arguments, third_flushed.length == 2);
            if (first_flushed.length == 1 && third_flushed.length == 2)
            {
                BUSTER_STRING_TEST(arguments, first_flushed.pointer[0], S8("first"));
                BUSTER_STRING_TEST(arguments, third_flushed.pointer[0], S8("third"));
                BUSTER_STRING_TEST(arguments, third_flushed.pointer[1], S8("fourth"));
            }
            arena_set_position(arena, restore);
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
#endif
