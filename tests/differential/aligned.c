// No explicit local-alignment request exists on these parameter locals. The
// backend must honor natural type alignment when copying incoming ABI values.
#include "aligned.h"
int aligned32(struct A32 value) { return observe(&value, 31) || value.v[0] != 7 || value.v[3] != 13; }
int aligned64(struct A64 value) { return observe(&value, 63) || value.v[0] != 7 || value.v[7] != 19; }
int aligned128(struct A128 value) { return observe(&value, 127) || value.v[0] != 7 || value.v[15] != 23; }
int exhausted(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d,
              unsigned long long e, unsigned long long f, struct A64 value)
{
    return observe(&value, 63) || value.v[0] != 7 || value.v[7] != 19 || a+b+c+d+e+f != 21;
}
