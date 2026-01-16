#pragma once

#include <buster/base.h>
#include <buster/string.h>

#define string8_length(strlit) ((strlit) ? __builtin_strlen(strlit) : 0)
#define S8(strlit) ((struct String8) { .pointer = (char8*)(strlit), .length = BUSTER_COMPILE_TIME_STRING_LENGTH(strlit) })
#define string8_from_pointer(p) string_from_pointer(8, p)
#define string8_from_pointers(p_start, p_end) string_from_pointers(8, p_start, p_end)
#define string8_from_pointer_length(p, l) string_from_pointer_length(8, p, l)
#define string8_from_pointer_start_end(p, start, end) string_from_pointer_length(8, p, start, end)
#define string8_slice(s, start, end) string_slice(8, s, start, end)
#define string8_starts_with_sequence(s, beginning) string_starts_with_sequence(8, (s), (beginning))

BUSTER_DECL u64 string8_parse_u64_decimal_assume_valid(String8 string);
BUSTER_DECL IntegerParsingU64 string8_parse_u64_hexadecimal(const char8* restrict p);
BUSTER_DECL IntegerParsingU64 string8_parse_u64_decimal(const char8* restrict p);
BUSTER_DECL IntegerParsingU64 string8_parse_u64_octal(const char8* restrict p);
BUSTER_DECL IntegerParsingU64 string8_parse_u64_binary(const char8* restrict p);
BUSTER_DECL String8 string8_format(String8 buffer_slice, String8 format, ...);
BUSTER_DECL StringFormatResult string8_format_va(String8 buffer_slice, String8 format, va_list variable_arguments);
BUSTER_DECL u64 string8_last_code_point(String8 s, char8 ch);
BUSTER_DECL u64 string8_first_sequence(String8 s, String8 sub);
BUSTER_DECL bool string8_ends_with_sequence(String8 s, String8 ending);
BUSTER_DECL u64 string8_code_point_count(String8 s, u8 ch);
BUSTER_DECL void string8_print(String8 format, ...);

BUSTER_DECL String8 string8_format_arena(Arena* arena, bool null_terminate, String8 format, ...);
BUSTER_DECL String8 string8_join_arena(Arena* arena, String8Slice strings, bool zero_terminate);
BUSTER_DECL String8 string8_duplicate_arena(Arena* arena, String8 string, bool zero_terminate);
BUSTER_DECL u64 string8_copy(String8 destination, String8 source);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_IMPL UnitTestResult string8_tests(UnitTestArguments* arguments);
#endif
