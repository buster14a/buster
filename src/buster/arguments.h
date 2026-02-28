#pragma once
#include <buster/base.h>

BUSTER_DECL void flag_set(u64* flag_pointer, u64 flag_count, u64 flag_index, bool flag_value);
BUSTER_DECL bool flag_get(u64* flag_pointer, u64 flag_count, u64 flag_index);

STRUCT(BooleanArgumentProcessResult)
{
    u64 index;
    bool valid;
    u8 reserved[7];
};
BUSTER_DECL BooleanArgumentProcessResult boolean_argument_process(StringOs* flag_string_start_pointer, u64 flag_string_start_count, u64* flag_pointer, u64 flag_count, StringOs argument);
