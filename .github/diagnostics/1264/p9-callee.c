#include <stdarg.h>
typedef struct __attribute__((packed)) { char c; long long x; } P9;
typedef struct { unsigned gp, fp; void *overflow, *save; } VaState;
_Static_assert(sizeof(P9) == 9, "P9 size");
_Static_assert(__builtin_offsetof(P9, x) == 1, "unaligned member");
_Static_assert(sizeof(VaState) == 24, "SysV va_list image");
int p9_probe(int marker, ...)
{
    va_list ap;
    VaState before, after;
    va_start(ap, marker);
    double first = va_arg(ap, double);
    __builtin_memcpy(&before, &ap, sizeof(before));
    P9 value = va_arg(ap, P9);
    __builtin_memcpy(&after, &ap, sizeof(after));
    int bad = marker != 17 || first != 1.5 ? 64 : 0;
    bad |= value.c != 3 || value.x != 0x1122334455667788ll ? 1 : 0;
    bad |= before.gp != 8 || after.gp != before.gp ? 2 : 0;
    bad |= before.fp != 64 || after.fp != before.fp ? 4 : 0;
    bad |= (unsigned long long)after.overflow != (unsigned long long)before.overflow + 16ull ? 8 : 0;
    bad |= after.save != before.save ? 16 : 0;
    bad |= va_arg(ap, long long) != 11ll ? 32 : 0;
    bad |= va_arg(ap, double) != 2.5 ? 64 : 0;
    va_end(ap);
    return bad;
}
