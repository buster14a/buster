#pragma once
#include <buster/base.h>
#include <buster/memory.h>

STRUCT(StringFormatResult)
{
    u64 real_buffer_index;
    u64 needed_code_unit_count;
    u64 real_format_index;
};

#define string_from_pointer(n, p) ((String ## n){ .pointer = (p), .length = string ## n ## _length(p) })
#define string_from_pointers(n, start, end) ((String ## n){ .pointer = (start), .length = (u64)((end) - (start)) }
#define string_from_pointer_length(n, p, l) ((String ## n){ .pointer = (p), .length = (l) })
#define string_from_indices(n, p, start, end) ((String ## n){ .pointer = (p) + (start), .length = (end) - (start) })
#define string_slice(n, slice, start, end) ((String ## n){ .pointer = (slice).pointer + (start), .length = (end) - (start) })

#define parsing_accumulate_hexadecimal(accumulator, code_point) ((accumulator) * 16 + (BUSTER_ASSUME(code_point_is_hexadecimal(code_point)), (code_point) - (code_point_is_decimal(code_point) ? '0' : (code_point_is_hexadecimal_alpha_upper(code_point) ? ('A' - 10) : code_point_is_hexadecimal_alpha_lower(code_point) ? ('a' - 10) : 0))))
#define parsing_accumulate_decimal(accumulator, code_point) (BUSTER_ASSUME(code_point_is_decimal(code_point)), ((accumulator) * 10 + ((code_point) - '0')))
#define parsing_accumulate_octal(accumulator, code_point) (BUSTER_ASSUME(code_point_is_octal(code_point)), (((accumulator) * 8) + ((code_point) - '0')))
#define parsing_accumulate_binary(accumulator, code_point) (BUSTER_ASSUME(code_point_is_binary(code_point)), (((accumulator) * 2) + ((code_point) - '0')))

#define string_first_code_point(s, code_point)                    \
({                                                                \
    u64 result_ = BUSTER_STRING_NO_MATCH;                         \
    for (u64 i_ = 0; i_ < (s).length; i_ += 1)                    \
    {                                                             \
        if ((s).pointer[i_] == (code_point))                      \
        {                                                         \
            result_ = i_;                                         \
            break;                                                \
        }                                                         \
    }                                                             \
    result_;                                                      \
})

#define string_equal(s1, s2)                                                     \
({                                                                               \
    typeof(s2) _s1_ = (s1);                                                   \
    typeof(s1) _s2_ = (s2);                                                   \
    string_generic_equal(_s1_.pointer, _s2_.pointer,                              \
                         _s1_.length, _s2_.length,                                \
                         sizeof(*_s1_.pointer));                                  \
})

#define string_code_point_count(s, code_point)\
({\
    u64 result_ = 0;\
    for (u64 i = 0; i < (s).length; i += 1)\
    {\
        result_ += (s).pointer[i] == (code_point);\
    }\
    result_;\
})

#define string_starts_with_sequence(n, s, beginning) \
({\
    bool result_ = (s).length >= (beginning).length;\
    if (result_)\
    {\
        let first_chunk = string_from_pointer_length(n, (s).pointer, (beginning).length);\
        result_ = memory_compare(first_chunk.pointer, (beginning).pointer, sizeof(*first_chunk.pointer) * first_chunk.length);\
    }\
    result_;\
})

BUSTER_DECL bool string_generic_equal(void* p1, void* p2, u64 l1, u64 l2, u64 element_size);
