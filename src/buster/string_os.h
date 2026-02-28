#pragma once

#if defined(_WIN32)
#include <buster/string16.h>
#define SOs(strlit) S16(strlit)
#define STRING_OS_DOUBLE_QUOTE "\\\""
#define string_os_from_pointer(pointer) string16_from_pointer(pointer)
#define string_os_first_code_point string16_first_code_point
#define string_os_to_string8_arena(a, s) string16_to_string8_arena(a, s, true)
#define string_os_equal(a, b) string16_equal(a, b)
#define string_os_starts_with(a, b) string16_starts_with(a, b)
#define string_os_from_pointer_length(pointer, length) string16_from_pointer_length(pointer, length)
#define string_os_parse_u64_hexadecimal(p) string16_parse_u64_hexadecimal(p)
#define string_os_parse_u64_decimal(p) string16_parse_u64_decimal(p)
#define string_os_parse_u64_octal(p) string16_parse_u64_octal(p)
#define string_os_parse_u64_binary(p) string16_parse_u64_binary(p)
#define string_os_length(s) string16_length(s)
#define string_os_format_arena(arena, format, ...) string16_format_arena(arena, true, format, __VA_ARGS__)
#define string_os_join_arena string16_join_arena
#define string_os_duplicate_arena string16_duplicate_arena
#define string_os_starts_with_sequence string16_starts_with_sequence
#else
#include <buster/string8.h>

#define SOs(strlit) S8(strlit)
#define STRING_OS_DOUBLE_QUOTE "\""
#define string_os_from_pointer(pointer) string8_from_pointer(pointer)
#define string_os_to_string8_arena(a, s) (s).pointer[(s).length] == 0 ? (s) : string8_duplicate_arena((a), (s), true)
#define string_os_equal(a, b) string_equal(a, b)
#define string_os_starts_with_sequence(a, b) string8_starts_with_sequence((a), (b))
#define string_os_from_pointer_length(pointer, length) string8_from_pointer_length(pointer, length)
#define string_os_first_code_point(s, sub) string8_first_code_point(s, sub)
#define string_os_parse_u64_hexadecimal(p) string8_parse_u64_hexadecimal(p)
#define string_os_parse_u64_decimal(p) string8_parse_u64_decimal(p)
#define string_os_parse_u64_octal(p) string8_parse_u64_octal(p)
#define string_os_parse_u64_binary(p) string8_parse_u64_binary(p)
#define string_os_length(s) string8_length(s)
#define string_os_format_arena(arena, format, ...) string8_format_arena((arena), true, (format), __VA_ARGS__)
#define string_os_join_arena(arena, parts, nt) string8_join_arena(arena, parts, nt)
#define string_os_duplicate_arena(arena, s, nt) string8_duplicate_arena(arena, s, nt)
#endif

#define string_os_to_c(s) (OsChar*)((s).pointer)

#include <buster/base.h>

STRUCT(StringOsListIterator)
{
    StringOsList list;
    u64 position;
};

STRUCT(OsArgumentBuilder)
{
    StringOsList argv;
    Arena* arena;
    u64 arena_offset;
};

BUSTER_DECL OsArgumentBuilder* string_os_list_builder_create(Arena* arena, StringOs s);
BUSTER_DECL StringOsList string_os_list_builder_append(OsArgumentBuilder* builder, StringOs arg);
BUSTER_DECL StringOsList string_os_list_end(OsArgumentBuilder* restrict builder);
BUSTER_DECL void string_os_list_builder_destroy(OsArgumentBuilder* restrict builder);

BUSTER_DECL StringOsList string_os_list_create_arena(Arena* arena, StringOsSlice arguments);
BUSTER_DECL StringOsList string_os_list_duplicate_and_substitute_first_argument(Arena* arena, StringOsList old_arguments, StringOs new_first_argument, StringOsSlice extra_arguments);

BUSTER_DECL StringOsListIterator string_os_list_iterator_initialize(StringOsList list);
BUSTER_DECL StringOs string_os_list_iterator_next(StringOsListIterator* iterator);
BUSTER_DECL StringOsList string_os_list_create_from(Arena* arena, StringOsSlice arguments);
