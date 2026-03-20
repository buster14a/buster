#pragma once
#include <buster/base.h>

BUSTER_F_DECL void flag_set_ex(u64* flag_pointer, u64 flag_count, u64 flag_index, bool flag_value);
BUSTER_F_DECL bool flag_get_ex(u64* flag_pointer, u64 flag_count, u64 flag_index);

#define flag_get(arr, e) flag_get_ex((arr), (__underlying_type(decltype(e))) decltype(e)::Count, (__underlying_type(decltype(e)))(e))
#define flag_set(arr, e, v) flag_set_ex((arr), (__underlying_type(decltype(e))) decltype(e)::Count, (__underlying_type(decltype(e)))(e), (v))

STRUCT(BooleanArgumentProcessResult)
{
    u64 index;
    bool valid;
    u8 reserved[7];
};
BUSTER_F_DECL BooleanArgumentProcessResult boolean_argument_process(StringOs* flag_string_start_pointer, u64 flag_string_start_count, u64* flag_pointer, u64 flag_count, StringOs argument);
