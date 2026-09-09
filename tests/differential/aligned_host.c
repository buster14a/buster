#include "aligned.h"
#include <stdio.h>
// Keep this in a separate translation unit from the Buster callee: otherwise a
// reference compiler can fold the pointer check from the declared alignment.
int observe(void const *p, unsigned mask) { return ((unsigned long long)p & mask) != 0; }
int main(void)
{
    struct A32 a = {{7, 0, 0, 13}};
    struct A64 b = {{7, 0, 0, 0, 0, 0, 0, 19}};
    struct A128 c = {{7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 23}};
    int bad = aligned32(a) | (aligned64(b)<<1) | (aligned128(c)<<2) | (exhausted(1,2,3,4,5,6,b)<<3);
    printf("aligned=%d\n", bad);
    return bad;
}
