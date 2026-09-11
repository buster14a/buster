// Independent host caller and callees; never inspect aggregate padding.
#include "qualified_aggregate.h"
#include <stdio.h>

int qualified_host_mutate(volatile struct QualifiedSmall value)
{
    value.bytes[0] = 9;
    return value.bytes[0] + 10 * value.bytes[1] + 100 * value.bytes[2];
}

int qualified_host_read(const volatile struct QualifiedSmall value)
{
    return value.bytes[0] + 10 * value.bytes[1] + 100 * value.bytes[2];
}

int main(void)
{
    struct QualifiedSmall small = {{1, 2, 3}};
    volatile struct QualifiedSmall observed = {{1, 2, 3}};
    struct QualifiedLarge large = {11, 13, 17};
    volatile int pointed = 20;
    struct QualifiedNested nested = {&pointed, small.bytes};
    QualifiedFunction function = qualified_mutate;
    int bad = 0;
    bad |= qualified_mutate(small) != 327;
    bad |= function(observed) != 327;
    bad |= qualified_read(observed) != 321;
    bad |= qualified_const(small) != 321;
    bad |= qualified_large(large, 7) != 142;
    bad |= large.a != 11 || large.b != 13 || large.c != 17;
    bad |= qualified_nested(nested) != 21 || pointed != 21;
    bad |= qualified_loop(small, 4) != 2592;
    bad |= qualified_call_host(small) != 650;
    bad |= small.bytes[0] != 1 || small.bytes[1] != 2 || small.bytes[2] != 3;
    bad |= observed.bytes[0] != 1 || observed.bytes[1] != 2 || observed.bytes[2] != 3;
    printf("qualified-aggregate=%d\n", bad);
    return bad;
}
