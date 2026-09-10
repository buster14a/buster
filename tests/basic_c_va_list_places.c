// Simulate the CRT declaration that precedes stdarg.h through stdint.h.
#if defined(_WIN32)
#ifndef _VA_LIST_DEFINED
#define _VA_LIST_DEFINED
typedef char *va_list;
#endif
#endif
#include <stdarg.h>

// Every list lives entirely in one compiler; this checks frontend places
// without depending on the separate public AArch64 va_list ABI issue.
typedef __builtin_va_list custom_list;
typedef custom_list nested_list;
struct State { nested_list ap; };
static int destination_calls;
static int source_calls;
static struct State *destination(struct State *state)
{
    destination_calls++;
    return state;
}
static nested_list *source(nested_list *ap)
{
    source_calls++;
    return ap;
}
static int direct(int count, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, count);
    int value = __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return value;
}
static int places(int count, ...)
{
    struct State state;
    nested_list copies[2];
    typedef nested_list local_list;
    local_list final;
    int index = 0;
    __builtin_va_start(destination(&state)->ap, count);
    __builtin_va_copy(copies[index++], *source(&state.ap));
    __builtin_va_copy(*source(&final), copies[0]);
    int a = __builtin_va_arg(state.ap, int);
    int b = __builtin_va_arg(copies[0], int);
    int c = __builtin_va_arg(*source(&final), int);
    int d = __builtin_va_arg(final, int);
    __builtin_va_end(state.ap);
    __builtin_va_end(copies[0]);
    __builtin_va_end(final);
    __builtin_va_start(copies[index++], count);
    int e = __builtin_va_arg(copies[1], int);
    __builtin_va_end(copies[1]);
    __builtin_va_start(*source(&final), count);
    int f = __builtin_va_arg(final, int);
    __builtin_va_end(final);
    return index != 2 || destination_calls != 1 || source_calls != 4 || a != 10 || b != 10 || c != 10 || d != 20 || e != 10 || f != 10;
}
static int public_list(int count, ...)
{
    struct { va_list ap; } state;
    va_list copies[1];
    int index = 0;
    va_start(state.ap, count);
    va_copy(copies[index++], state.ap);
    int first = va_arg(state.ap, int);
    int second = va_arg(copies[0], int);
    va_end(state.ap);
    va_end(copies[0]);
    return index != 1 || first != 42 || second != 42;
}

int main(void)
{
    _Static_assert(sizeof(custom_list) == sizeof(__builtin_va_list), "alias layout");
    return direct(1, 42) != 42 || places(2, 10, 20) || public_list(1, 42);
}
