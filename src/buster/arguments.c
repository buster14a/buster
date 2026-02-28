#pragma once

#include <buster/arguments.h>
#include <buster/assertion.h>
#include <buster/string_os.h>

BUSTER_IMPL void flag_set(u64* flag_pointer, u64 flag_count, u64 flag_index, bool flag_value)
{
    BUSTER_CHECK(flag_index < flag_count);
    u64 divisor = sizeof(*flag_pointer);
    let element_index = flag_index / divisor;
    let bit_index = flag_index % divisor;
    flag_pointer[element_index] = (flag_pointer[element_index] & ~((u64)1 << bit_index)) | ((u64)flag_value << bit_index);
}

BUSTER_IMPL bool flag_get(u64* flag_pointer, u64 flag_count, u64 flag_index)
{
    BUSTER_CHECK(flag_index < flag_count);
    u64 divisor = sizeof(*flag_pointer);
    let element_index = flag_index / divisor;
    let bit_index = flag_index % divisor;
    return (flag_pointer[element_index] & ((u64)1 << bit_index)) != 0;
}

BUSTER_IMPL BooleanArgumentProcessResult boolean_argument_process(StringOs* flag_string_start_pointer, u64 flag_string_start_count, u64* flag_pointer, u64 flag_count, StringOs argument)
{
    BUSTER_CHECK(flag_string_start_count == flag_count);

    BooleanArgumentProcessResult result = {};
    
    for (result.index = 0; result.index < flag_string_start_count; result.index += 1)
    {
        let flag_start = flag_string_start_pointer[result.index];
        if (string_os_starts_with_sequence(argument, flag_start))
        {
            if (argument.length == flag_start.length + 1)
            {
                let v = argument.pointer[flag_start.length];
                switch (v)
                {
                    break; case '0': case '1':
                    {
                        bool flag_value = v == '1';
                        flag_set(flag_pointer, flag_count, result.index, flag_value);
                        result.valid = true;
                    }
                    break; default: {}
                }
            }

            break;
        }
    }

    return result;
}
