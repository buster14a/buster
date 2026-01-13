#pragma once

#include <buster/base.h>
#include <buster/string.h>

#define S16(strlit) ((struct String16) { .pointer = (char16*)(u ## strlit), .length = BUSTER_COMPILE_TIME_STRING_LENGTH(strlit) })
#define string16_from_pointer(p) string_from_pointer(16, p)
#define string16_from_pointers(p_start, p_end) string_from_pointers(16, p_start, p_end)
#define string16_from_pointer_length(p, l) string_from_pointer_length(16, p, l)
#define string16_from_pointer_start_end(p, start, end) string_from_pointer_length(16, p, start, end)
#define string16_slice(s, start, end) string_slice(16, s, start, end)
#define string16_starts_with_sequence(s, beginning) string_starts_with_sequence(16, (s), (beginning))

BUSTER_DECL u64 string16_length(const char16* s);
BUSTER_DECL u64 string16_parse_u64_decimal_assume_valid(String16 string);
BUSTER_DECL IntegerParsingU64 string16_parse_u64_hexadecimal(const char16* restrict p);
BUSTER_DECL IntegerParsingU64 string16_parse_u64_decimal(const char16* restrict p);
BUSTER_DECL IntegerParsingU64 string16_parse_u64_octal(const char16* restrict p);
BUSTER_DECL IntegerParsingU64 string16_parse_u64_binary(const char16* restrict p);
BUSTER_DECL String16 string16_format(String16 buffer_slice, String16 format, ...);
BUSTER_DECL StringFormatResult string16_format_va(String16 buffer_slice, String16 format, va_list variable_arguments);
BUSTER_DECL u64 string16_last_code_point(String16 s, char16 ch);
BUSTER_DECL u64 raw_string16_first_code_point(const char16* s, char16 ch);
BUSTER_DECL u64 raw_string16_last_code_point(const char16* s, char16 ch);
BUSTER_DECL u64 string16_first_sequence(String16 s, String16 sub);
BUSTER_DECL bool string16_ends_with_sequence(String16 s, String16 ending);
BUSTER_DECL u64 string16_code_point_count(String16 s, char16 ch);
BUSTER_DECL void string16_print(String16 format, ...);

BUSTER_DECL String16 string16_format_arena(Arena* arena, bool null_terminate, String16 format, ...);
BUSTER_DECL String16 string16_join_arena(Arena* arena, String16Slice strings, bool zero_terminate);
BUSTER_DECL String16 string16_duplicate_arena(Arena* arena, String16 string, bool zero_terminate);

BUSTER_DECL u64 string16_copy(String16 destination, String16 source);
