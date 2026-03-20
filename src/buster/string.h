#pragma once
#include <buster/base.h>
#include <buster/memory.h>

STRUCT(OsArgumentBuilder)
{
    StringOsList argv;
    Arena* arena;
    u64 arena_offset;
};

STRUCT(StringOsListIterator)
{
    StringOsList list;
    u64 position;
};

BUSTER_F_DECL String8 string8_from_pointer(const char8* pointer);
BUSTER_F_DECL bool string8_equal(String8 s1, String8 s2);
BUSTER_F_DECL void string8_print(String8 format, ...);
BUSTER_F_DECL String8 string8_format(Arena* arena, String8 format, ...);
BUSTER_F_DECL bool string8_ends_with_sequence(String8 string, String8 ending);
BUSTER_F_DECL u64 string8_first_sequence(String8 string, String8 sequence);
BUSTER_F_DECL String8 string8_slice(String8 slice, u64 start, u64 end);
BUSTER_F_DECL String8 string8_format_va(Arena* arena, String8 format, va_list variable_arguments);
BUSTER_F_DECL String8 string8_duplicate_arena(Arena* arena, String8 string, bool zero_terminate);

BUSTER_F_DECL bool string16_equal(String16 s1, String16 s2);
BUSTER_F_DECL bool string16_ends_with_sequence(String16 string, String16 ending);
BUSTER_F_DECL u64 string16_first_sequence(String16 string, String16 sequence);
BUSTER_F_DECL String16 string16_slice(String16 slice, u64 start, u64 end);
BUSTER_F_DECL String16 string16_format_va(Arena* arena, String16 format, va_list variable_arguments);
BUSTER_F_DECL String16 string16_duplicate_arena(Arena* arena, String16 string, bool zero_terminate);

BUSTER_F_DECL bool string_os_equal(StringOs s1, StringOs s2);
BUSTER_F_DECL StringOs string_os_from_pointer_length(CharOs* pointer, u64 length);
BUSTER_F_DECL bool string_os_starts_with_sequence(StringOs string, StringOs sequence);
BUSTER_F_DECL bool string_os_ends_with_sequence(StringOs string, StringOs ending);
BUSTER_F_DECL u64 string_os_first_sequence(StringOs string, StringOs sequence);
BUSTER_F_DECL StringOs string_os_slice(StringOs slice, u64 start, u64 end);
BUSTER_F_DECL StringOs string_os_format_va(Arena* arena, StringOs format, va_list variable_arguments);
BUSTER_F_DECL StringOs string_os_duplicate_arena(Arena* arena, StringOs string, bool zero_terminate);

BUSTER_F_DECL StringOsListIterator string_os_list_iterator_initialize(StringOsList list);
BUSTER_F_DECL StringOs string_os_list_iterator_next(StringOsListIterator* iterator);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult string_tests(UnitTestArguments* arguments);
#endif
//
// STRUCT(StringFormatResult)
// {
//     u64 real_buffer_index;
//     u64 needed_code_unit_count;
//     u64 real_format_index;
// };
//
// #define string_from_pointer(n, p) ((String ## n){ .pointer = (p), .length = string ## n ## _length(p) })
// #define string_from_pointers(n, start, end) ((String ## n){ .pointer = (start), .length = (u64)((end) - (start)) }
// #define string_from_pointer_length(n, p, l) ((String ## n){ .pointer = (p), .length = (l) })
// #define string_from_indices(n, p, start, end) ((String ## n){ .pointer = (p) + (start), .length = (end) - (start) })
//
// #define string_code_point_count(s, code_point)\
// ({\
//     u64 result_ = 0;\
//     for (u64 i = 0; i < (s).length; i += 1)\
//     {\
//         result_ += (s).pointer[i] == (code_point);\
//     }\
//     result_;\
// })
//
// #define string_starts_with_sequence(n, s, beginning) \
// ({\
//     bool result_ = (s).length >= (beginning).length;\
//     if (result_)\
//     {\
//         let first_chunk = string_from_pointer_length(n, (s).pointer, (beginning).length);\
//         result_ = memory_compare(first_chunk.pointer, (beginning).pointer, sizeof(*first_chunk.pointer) * first_chunk.length);\
//     }\
//     result_;\
// })
//
// BUSTER_F_DECL bool string_generic_equal(void* p1, void* p2, u64 l1, u64 l2, u64 element_size);
// BUSTER_F_DECL String8 string16_to_string8_arena(Arena* arena, String16 s, bool null_terminate);
// BUSTER_F_DECL String16 string8_to_string16_arena(Arena* arena, String8 s, bool null_terminate);
//
// #define string8_length(strlit) ((strlit) ? __builtin_strlen(strlit) : 0)
// #define string8_from_pointer(p) string_from_pointer(8, p)
// #define string8_from_pointers(p_start, p_end) string_from_pointers(8, p_start, p_end)
// #define string8_from_pointer_length(p, l) string_from_pointer_length(8, p, l)
// #define string8_from_pointer_start_end(p, start, end) string_from_pointer_length(8, p, start, end)
// #define string8_starts_with_sequence(s, beginning) string_starts_with_sequence(8, (s), (beginning))
//
// BUSTER_F_DECL u64 string8_parse_u64_decimal_assume_valid(String8 string);
// BUSTER_F_DECL IntegerParsingU64 string8_parse_u64_hexadecimal(const char8* restrict p);
// BUSTER_F_DECL IntegerParsingU64 string8_parse_u64_decimal(const char8* restrict p);
// BUSTER_F_DECL IntegerParsingU64 string8_parse_u64_octal(const char8* restrict p);
// BUSTER_F_DECL IntegerParsingU64 string8_parse_u64_binary(const char8* restrict p);
// BUSTER_F_DECL String8 string8_format(Arena* arena, String8 format, ...);
// BUSTER_F_DECL String8 string8_format_va(Arena* arena, String8 format, va_list variable_arguments);
// BUSTER_F_DECL u64 string8_last_code_point(String8 s, char8 ch);
// BUSTER_F_DECL u64 string8_first_sequence(String8 s, String8 sub);
// BUSTER_F_DECL bool string8_ends_with_sequence(String8 s, String8 ending);
// BUSTER_F_DECL u64 string8_code_point_count(String8 s, u8 ch);
// BUSTER_F_DECL void string8_print(String8 format, ...);
//
// BUSTER_F_DECL String8 string8_join_arena(Arena* arena, Slice<String8> strings, bool zero_terminate);
// BUSTER_F_DECL String8 string8_duplicate_arena(Arena* arena, String8 string, bool zero_terminate);
// BUSTER_F_DECL u64 string8_copy(String8 destination, String8 source);
//
//
// #define string16_from_pointer(p) string_from_pointer(16, p)
// #define string16_from_pointers(p_start, p_end) string_from_pointers(16, p_start, p_end)
// #define string16_from_pointer_length(p, l) string_from_pointer_length(16, p, l)
// #define string16_from_pointer_start_end(p, start, end) string_from_pointer_length(16, p, start, end)
// #define string16_slice(s, start, end) string_slice(16, s, start, end)
// #define string16_starts_with_sequence(s, beginning) string_starts_with_sequence(16, (s), (beginning))
//
// BUSTER_F_DECL u64 string16_length(const char16* s);
// BUSTER_F_DECL u64 string16_parse_u64_decimal_assume_valid(String16 string);
// BUSTER_F_DECL IntegerParsingU64 string16_parse_u64_hexadecimal(const char16* restrict p);
// BUSTER_F_DECL IntegerParsingU64 string16_parse_u64_decimal(const char16* restrict p);
// BUSTER_F_DECL IntegerParsingU64 string16_parse_u64_octal(const char16* restrict p);
// BUSTER_F_DECL IntegerParsingU64 string16_parse_u64_binary(const char16* restrict p);
// BUSTER_F_DECL String16 string16_format(String16 buffer_slice, String16 format, ...);
// BUSTER_F_DECL StringFormatResult string16_format_va(String16 buffer_slice, String16 format, va_list variable_arguments);
// BUSTER_F_DECL u64 string16_last_code_point(String16 s, char16 ch);
// BUSTER_F_DECL u64 raw_string16_first_code_point(const char16* s, char16 ch);
// BUSTER_F_DECL u64 raw_string16_last_code_point(const char16* s, char16 ch);
// BUSTER_F_DECL u64 string16_first_sequence(String16 s, String16 sub);
// BUSTER_F_DECL bool string16_ends_with_sequence(String16 s, String16 ending);
// BUSTER_F_DECL u64 string16_code_point_count(String16 s, char16 ch);
// BUSTER_F_DECL void string16_print(String16 format, ...);
//
// BUSTER_F_DECL String16 string16_format_arena(Arena* arena, bool null_terminate, String16 format, ...);
// BUSTER_F_DECL String16 string16_join_arena(Arena* arena, Slice<String16> strings, bool zero_terminate);
// BUSTER_F_DECL String16 string16_duplicate_arena(Arena* arena, String16 string, bool zero_terminate);
//
// BUSTER_F_DECL u64 string16_copy(String16 destination, String16 source);
//
// #if defined(_WIN32)
// #define SOs(strlit) S16(strlit)
// #define STRING_OS_DOUBLE_QUOTE "\\\""
// #define string_os_from_pointer(pointer) string16_from_pointer(pointer)
// #define string_os_first_code_point string16_first_code_point
// #define string_os_to_string8_arena(a, s) string16_to_string8_arena(a, s, true)
// #define string_os_equal(a, b) string16_equal(a, b)
// #define string_os_starts_with(a, b) string16_starts_with(a, b)
// #define string_os_from_pointer_length(pointer, length) string16_from_pointer_length(pointer, length)
// #define string_os_parse_u64_hexadecimal(p) string16_parse_u64_hexadecimal(p)
// #define string_os_parse_u64_decimal(p) string16_parse_u64_decimal(p)
// #define string_os_parse_u64_octal(p) string16_parse_u64_octal(p)
// #define string_os_parse_u64_binary(p) string16_parse_u64_binary(p)
// #define string_os_length(s) string16_length(s)
// #define string_os_format_arena(arena, format, ...) string16_format_arena(arena, true, format, __VA_ARGS__)
// #define string_os_join_arena string16_join_arena
// #define string_os_duplicate_arena string16_duplicate_arena
// #define string_os_starts_with_sequence string16_starts_with_sequence
// #define string_os_ends_with_sequence string16_ends_with_sequence
// #else
// #define STRING_OS_DOUBLE_QUOTE "\""
// #define string_os_from_pointer(pointer) string8_from_pointer(pointer)
// #define string_os_to_string8_arena(a, s) (s).pointer[(s).length] == 0 ? (s) : string8_duplicate_arena((a), (s), true)
// #define string_os_equal(a, b) string8_equal(a, b)
// #define string_os_starts_with_sequence(a, b) string8_starts_with_sequence((a), (b))
// #define string_os_from_pointer_length(pointer, length) string8_from_pointer_length(pointer, length)
// #define string_os_first_code_point(s, sub) string8_first_code_point(s, sub)
// #define string_os_parse_u64_hexadecimal(p) string8_parse_u64_hexadecimal(p)
// #define string_os_parse_u64_decimal(p) string8_parse_u64_decimal(p)
// #define string_os_parse_u64_octal(p) string8_parse_u64_octal(p)
// #define string_os_parse_u64_binary(p) string8_parse_u64_binary(p)
// #define string_os_length(s) string8_length(s)
// #define string_os_format(arena, format, ...) string8_format((arena), (format), __VA_ARGS__)
// #define string_os_join_arena(arena, parts, nt) string8_join_arena(arena, parts, nt)
// #define string_os_duplicate_arena(arena, s, nt) string8_duplicate_arena(arena, s, nt)
// #define string_os_ends_with_sequence string8_ends_with_sequence
// #endif
//
// #define string_os_to_c(s) (OsChar*)((s).pointer)
//
// #include <buster/base.h>
//
//
// BUSTER_F_DECL OsArgumentBuilder* string_os_list_builder_create(Arena* arena, StringOs s);
// BUSTER_F_DECL StringOsList string_os_list_builder_append(OsArgumentBuilder* builder, StringOs arg);
// BUSTER_F_DECL StringOsList string_os_list_end(OsArgumentBuilder* restrict builder);
// BUSTER_F_DECL void string_os_list_builder_destroy(OsArgumentBuilder* restrict builder);
//
// BUSTER_F_DECL StringOsList string_os_list_create_arena(Arena* arena, Slice<StringOs> arguments);
// BUSTER_F_DECL StringOsList string_os_list_duplicate_and_substitute_first_argument(Arena* arena, StringOsList old_arguments, StringOs new_first_argument, Slice<StringOs> extra_arguments);
//
// BUSTER_F_DECL StringOsListIterator string_os_list_iterator_initialize(StringOsList list);
// BUSTER_F_DECL StringOs string_os_list_iterator_next(StringOsListIterator* iterator);
// BUSTER_F_DECL StringOsList string_os_list_create_from(Arena* arena, Slice<StringOs> arguments);
