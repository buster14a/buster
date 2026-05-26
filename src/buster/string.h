#pragma once
#include <buster/base.h>

typedef struct OsArgumentBuilder OsArgumentBuilder;
struct OsArgumentBuilder
{
    Arena* arena;
    u64 position;
};

typedef struct StringOsListIterator StringOsListIterator;
struct StringOsListIterator
{
    StringOsList list;
    u64 position;
};

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult string_tests(UnitTestArguments* arguments);
#endif

BUSTER_F_DECL String8 string_from_pointer(const char8* pointer);
BUSTER_F_DECL bool string_equal(String8 s1, String8 s2);
BUSTER_F_DECL void string_print(String8 format, ...);
BUSTER_F_DECL String8 string_format(Arena* arena, String8 format, ...);
BUSTER_F_DECL bool string_ends_with_sequence(String8 string, String8 ending);
BUSTER_F_DECL u64 string_first_code_unit(String8 string, char8 code_unit);
BUSTER_F_DECL u64 string_first_sequence(String8 string, String8 sequence);
BUSTER_F_DECL String8 string_slice(String8 slice, u64 start, u64 end);
BUSTER_F_DECL String8 string_format_va(Arena* arena, String8 format, va_list variable_arguments);
BUSTER_F_DECL String8 string_duplicate_arena(Arena* arena, String8 string, bool zero_terminate);
BUSTER_F_DECL bool string_starts_with_sequence(String8 string, String8 sequence);
BUSTER_F_DECL String8 string_from_pointer_length(const char8* pointer, u64 length);
BUSTER_F_DECL String8 string_join_arena(Arena* arena, SliceString8 strings, bool zero_terminate);
BUSTER_F_DECL String8 string_format_z(Arena* arena, String8 format, ...);
BUSTER_F_DECL u64 string_array_match(SliceString8 names, String8 name);
BUSTER_F_DECL bool code_unit_is_decimal(char8 code_unit);

BUSTER_F_DECL bool string16_equal(String16 s1, String16 s2);
BUSTER_F_DECL u64 string16_length(const char16* s);
BUSTER_F_DECL u64 string16_first_code_unit(String16 string, char16 code_unit);
BUSTER_F_DECL SliceString8 string16_environment_block_to_slice_string(Arena* arena, const char16* environment_block);

// BUSTER_F_DECL bool string_os_equal(StringOs s1, StringOs s2);
// BUSTER_F_DECL String8 string_os_from_pointer_length(CharOs* pointer, u64 length);
// BUSTER_F_DECL bool string_os_starts_with_sequence(StringOs string, StringOs sequence);
// BUSTER_F_DECL bool string_os_ends_with_sequence(StringOs string, StringOs ending);
// BUSTER_F_DECL u64 string_os_first_sequence(StringOs string, StringOs sequence);
// BUSTER_F_DECL StringOs string_os_slice(StringOs slice, u64 start, u64 end);
// BUSTER_F_DECL StringOs string_os_format_va(Arena* arena, StringOs format, va_list variable_arguments);
// BUSTER_F_DECL StringOs string_os_duplicate_arena(Arena* arena, StringOs string, bool zero_terminate);

// BUSTER_F_DECL StringOsListIterator string_os_list_iterator_initialize(StringOsList list);
// BUSTER_F_DECL StringOs string_os_list_iterator_next(StringOsListIterator* iterator);
BUSTER_F_DECL char** slice_string8_to_null_terminated_array_char(Arena* arena, SliceString8 strings);

BUSTER_F_DECL SliceString8 slice_string_from_posix_string_list(Arena* arena, PosixStringList string_os_list);
BUSTER_F_DECL SliceString8 slice_string_from_windows_string_list(Arena* arena, WindowsStringList string_os_list);
BUSTER_F_DECL WindowsStringList windows_string_list_from_slice_string(Arena* arena, SliceString8 parts);
BUSTER_F_DECL WindowsStringList windows_environment_block_from_slice_string(Arena* arena, SliceString8 environment);
BUSTER_F_DECL PosixStringList posix_string_list_from_slice_string(Arena* arena, SliceString8 parts);
PosixStringList posix_environment_from_keys_and_values(Arena* arena, SliceString8 keys, SliceString8 values);
WindowsStringList windows_environment_from_keys_and_values(Arena* arena, SliceString8 keys, SliceString8 values);

BUSTER_F_DECL IntegerParsingU64 string8_parse_u64_hexadecimal(const char8* restrict p);
BUSTER_F_DECL IntegerParsingU64 string8_parse_u64_decimal(const char8* restrict p);
BUSTER_F_DECL IntegerParsingU64 string8_parse_u64_octal(const char8* restrict p);
BUSTER_F_DECL IntegerParsingU64 string8_parse_u64_binary(const char8* restrict p);
BUSTER_F_DECL String16 string16_from_string8(Arena* arena, String8 string, bool null_terminate);
BUSTER_F_DECL String8 string8_from_string16(Arena* arena, String16 s, bool null_terminate);

BUSTER_F_DECL OsArgumentBuilder os_argument_builder_start(Arena* arena);
BUSTER_F_DECL void os_argument_builder_append(OsArgumentBuilder* builder, String8 string);
BUSTER_F_DECL SliceString8 os_argument_builder_flush(OsArgumentBuilder* const builder);
