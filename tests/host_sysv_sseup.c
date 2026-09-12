#include "sysv_sseup.h"
#include <string.h>

static SseupWrapper sseup_make(SseupWord low, SseupWord high)
{
    SseupWord words[2] = {low, high};
    SseupWrapper value;
    memcpy(&value, words, sizeof(value));
    return value;
}

static int sseup_equal(SseupWrapper a, SseupWrapper b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

SseupWrapper sseup_host_return(SseupWord low, SseupWord high) { return sseup_make(low, high); }

int sseup_host_observe(SseupWrapper value, double tail, SseupWord key)
{
    return !sseup_equal(value, sseup_make(0x0123456789abcdefull, 0xfedcba9876543210ull)) || tail != 29.0 || key != 31;
}

int sseup_host_variadic(int count, ...)
{
    int failed = 0;
    __builtin_va_list list;
    __builtin_va_start(list, count);
    for (int index = 0; index < count; index += 1)
    {
        SseupWrapper value = __builtin_va_arg(list, SseupWrapper);
        failed |= sseup_host_observe(value, 29.0, 31);
    }
    __builtin_va_end(list);
    return failed;
}

int main(void)
{
    int failed = 0;
    for (unsigned int bit = 0; bit < 64; bit += 1)
    {
        SseupWrapper a = sseup_make(1ull << bit, ~(1ull << bit));
        SseupWrapper b = sseup_make(~(1ull << bit), 1ull << bit);
        failed |= !sseup_equal(sseup_echo(a), a);
        SseupWrapper (*volatile indirect)(SseupWrapper) = sseup_echo;
        failed |= !sseup_equal(indirect(b), b);
        SseupNested nested = {{a}};
        SseupNested nested_result = sseup_nested(nested);
        failed |= !sseup_equal(nested_result.values[0], a);
        SseupWord observed = 0;
        failed |= !sseup_equal(sseup_mixed(7, a, 11.0, b, &observed), b) || observed != 18;
        failed |= !sseup_equal(sseup_mixed(0, a, 13.0, b, &observed), a) || observed != 13;
        failed |= !sseup_equal(sseup_ninth(a, b, a, b, a, b, a, b, b, 17.0), b);
        failed |= !sseup_equal(sseup_variadic(1, a, b), b);
        failed |= !sseup_equal(sseup_variadic(2, a, b, a), b);
        failed |= !sseup_equal(sseup_variadic(9, a, b, a, b, a, b, a, b, a, b), b);
        failed |= !sseup_equal(sseup_after_doubles(9, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, b), b);
        SseupIntegerOverlay integer;
        memcpy(&integer, &a, sizeof(integer));
        SseupIntegerOverlay integer_result = sseup_integer_overlay(integer);
        failed |= memcmp(&integer, &integer_result, sizeof(integer)) != 0;
        SseupHeadOverlay head;
        memcpy(&head, &a, sizeof(head));
        SseupHeadOverlay head_result = sseup_head_overlay(head);
        failed |= memcmp(&head, &head_result, sizeof(head)) != 0;
    }
    SseupFloatOverlay floating;
    floating.halves[0] = 17.0;
    floating.halves[1] = -29.0;
    SseupFloatOverlay floating_result = sseup_float_overlay(floating);
    failed |= floating_result.halves[0] != 17.0 || floating_result.halves[1] != -29.0;
    failed |= sseup_call_host();
    return failed;
}
