#pragma once
#if defined(_WIN32)
#include <buster/string16.h>
#define SOs(strlit) S16(strlit)
#define OS_STRING_DOUBLE_QUOTE "\\\""
#define os_string_from_pointer(pointer) string16_from_pointer(pointer)
#define os_string_first_code_point string16_first_code_point
#define os_string_to_string8(a, s) string16_to_string8(a, s)
#define os_string_equal(a, b) string16_equal(a, b)
#define os_string_starts_with(a, b) string16_starts_with(a, b)
#define os_string_from_pointer_length(pointer, length) string16_from_pointer_length(pointer, length)
#define os_string_parse_hexadecimal(p) string16_parse_hexadecimal(p)
#define os_string_parse_decimal(p) string16_parse_decimal(p)
#define os_string_parse_octal(p) string16_parse_octal(p)
#define os_string_parse_binary(p) string16_parse_binary(p)
#define os_string_length(s) string16_length(s)
#define arena_os_string_format(arena, format, ...) arena_string16_format(arena, true, format, __VA_ARGS__)
#define arena_join_os_string arena_join_string16
#define arena_duplicate_os_string string16_duplicate_arena
#else
#include <buster/string8.h>

#define SOs(strlit) S8(strlit)
#define OS_STRING_DOUBLE_QUOTE "\""
#define os_string_from_pointer(pointer) string8_from_pointer(pointer)
#define os_string_to_string8(a, s) s
#define os_string_equal(a, b) string_equal(a, b)
#define os_string_starts_with_sequence(a, b) string8_starts_with_sequence((a), (b))
#define os_string_from_pointer_length(pointer, length) string8_from_pointer_length(pointer, length)
#define os_string_first_code_point(s, sub) string8_first_code_point(s, sub)
#define os_string_parse_u64_hexadecimal(p) string8_parse_u64_hexadecimal(p)
#define os_string_parse_u64_decimal(p) string8_parse_u64_decimal(p)
#define os_string_parse_u64_octal(p) string8_parse_u64_octal(p)
#define os_string_parse_u64_binary(p) string8_parse_u64_binary(p)
#define os_string_length(s) string8_length(s)
#define arena_os_string_format(arena, format, ...) string8_format_arena((arena), true, (format), __VA_ARGS__)
#define string_os_join_arena string8_join_arena
#define arena_duplicate_os_string string8_duplicate_arena
#endif

#define os_string_to_c(s) (OsChar*)((s).pointer)

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

BUSTER_DECL OsArgumentBuilder* os_string_list_builder_create(Arena* arena, StringOs s);
BUSTER_DECL StringOsList os_string_list_builder_append(OsArgumentBuilder* builder, StringOs arg);
BUSTER_DECL StringOsList os_string_list_end(OsArgumentBuilder* restrict builder);
BUSTER_DECL void os_string_list_builder_destroy(OsArgumentBuilder* restrict builder);

BUSTER_DECL StringOsList os_string_list_create_from(Arena* arena, StringOsSlice arguments);
BUSTER_DECL StringOsListIterator os_string_list_iterator_initialize(StringOsList list);
BUSTER_DECL StringOs os_string_list_iterator_next(StringOsListIterator* iterator);
