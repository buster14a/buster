// Parameter objects retain qualifiers; values crossing calls do not.
#include "qualified_aggregate.h"

int qualified_mutate(volatile struct QualifiedSmall value)
{
    volatile struct QualifiedSmall* address = &value;
    address->bytes[0] = 7;
    return value.bytes[0] + 10 * value.bytes[1] + 100 * value.bytes[2];
}

int qualified_read(const volatile struct QualifiedSmall value)
{
    return value.bytes[0] + 10 * value.bytes[1] + 100 * value.bytes[2];
}

int qualified_const(const struct QualifiedSmall value)
{
    return value.bytes[0] + 10 * value.bytes[1] + 100 * value.bytes[2];
}

unsigned long long qualified_large(volatile struct QualifiedLarge value, unsigned delta)
{
    value.a += delta;
    return value.a + 3 * value.b + 5 * value.c;
}

int qualified_nested(struct QualifiedNested value)
{
    *value.pointer += value.bytes[0];
    return *value.pointer;
}

int qualified_loop(struct QualifiedSmall value, unsigned count)
{
    int result = 0;
    QualifiedFunction function = qualified_read;
    for (unsigned index = 0; index < count; index += 1)
    {
        if (index & 1)
        {
            function = qualified_mutate;
        }
        else
        {
            function = qualified_read;
        }
        result += function(value);
        result += ((int (*)(volatile struct QualifiedSmall))function)(value);
    }
    return result;
}

int qualified_call_host(struct QualifiedSmall value)
{
    QualifiedFunction function = qualified_host_mutate;
    int result = qualified_host_read(value) + function(value);
    return result;
}
