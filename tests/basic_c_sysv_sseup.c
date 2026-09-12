#include "sysv_sseup.h"

// No vector arithmetic: this isolates aggregate ABI transport from the
// selector's separate narrow-vector arithmetic coverage.
SseupWrapper sseup_echo(SseupWrapper value) { return value; }
SseupNested sseup_nested(SseupNested value) { return value; }
SseupFloatOverlay sseup_float_overlay(SseupFloatOverlay value) { return value; }
SseupIntegerOverlay sseup_integer_overlay(SseupIntegerOverlay value) { return value; }
SseupHeadOverlay sseup_head_overlay(SseupHeadOverlay value) { return value; }

SseupWrapper sseup_mixed(SseupWord key, SseupWrapper first, double scalar, SseupWrapper second, SseupWord* observed)
{
    *observed = key + (SseupWord)scalar;
    SseupWrapper result = first;
    if (key) { result = second; }
    return result;
}

SseupWrapper sseup_ninth(SseupWrapper a, SseupWrapper b, SseupWrapper c, SseupWrapper d, SseupWrapper e,
                         SseupWrapper f, SseupWrapper g, SseupWrapper h, SseupWrapper i, double tail)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)tail;
    return i;
}

SseupWrapper sseup_variadic(int count, ...)
{
    __builtin_va_list list;
    __builtin_va_start(list, count);
    SseupWrapper result = __builtin_va_arg(list, SseupWrapper);
    for (int index = 1; index < count; index += 1) { result = __builtin_va_arg(list, SseupWrapper); }
    __builtin_va_list copy;
    __builtin_va_copy(copy, list);
    SseupWrapper last = __builtin_va_arg(copy, SseupWrapper);
    __builtin_va_end(copy);
    __builtin_va_end(list);
    if (count & 1) { result = last; }
    return result;
}

SseupWrapper sseup_after_doubles(int count, ...)
{
    __builtin_va_list list;
    __builtin_va_start(list, count);
    for (int index = 0; index < count; index += 1) { (void)__builtin_va_arg(list, double); }
    SseupWrapper result = __builtin_va_arg(list, SseupWrapper);
    __builtin_va_end(list);
    return result;
}

int sseup_call_host(void)
{
    SseupWrapper value = sseup_host_return(0x0123456789abcdefull, 0xfedcba9876543210ull);
    return sseup_host_observe(value, 29.0, 31) |
           sseup_host_variadic(10, value, value, value, value, value, value, value, value, value, value);
}
