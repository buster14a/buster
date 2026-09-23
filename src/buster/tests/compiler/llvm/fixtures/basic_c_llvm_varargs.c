#include <stdarg.h>

extern int clang_read_public(va_list* ap);
extern int clang_sum_ints(int count, ...);
extern int clang_check_promotions(int named, ...);

int llvm_sum_ints(int count, ...)
{
    va_list ap;
    va_start(ap, count);
    int result = 0;
    for (int index = 0; index < count; index += 1)
    {
        result += va_arg(ap, int);
    }
    va_end(ap);
    return result;
}

long long llvm_sum_wide(int count, ...)
{
    va_list ap;
    va_start(ap, count);
    long long result = 0;
    for (int index = 0; index < count; index += 1)
    {
        result += va_arg(ap, long long);
    }
    va_end(ap);
    return result;
}

double llvm_sum_doubles(int count, ...)
{
    va_list ap;
    va_start(ap, count);
    double result = 0.0;
    for (int index = 0; index < count; index += 1)
    {
        result += va_arg(ap, double);
    }
    va_end(ap);
    return result;
}

int llvm_mixed(int named, double fixed, ...)
{
    va_list ap;
    va_start(ap, fixed);
    int narrow = va_arg(ap, int);
    double promoted_float = va_arg(ap, double);
    void* pointer = va_arg(ap, void*);
    va_end(ap);
    return named + (int)fixed + narrow + (int)promoted_float + (pointer != 0);
}

int llvm_copy_cursors(int named, ...)
{
    va_list ap;
    va_list copy;
    va_start(ap, named);
    va_copy(copy, ap);
    int copied_first = va_arg(copy, int);
    int original_first = va_arg(ap, int);
    int copied_second = va_arg(copy, int);
    int original_second = va_arg(ap, int);
    va_end(copy);
    va_end(ap);
    return copied_first == original_first && copied_second == original_second && copied_first == 13 && copied_second == 17;
}

int llvm_read_public(va_list* ap)
{
    int first = va_arg(*ap, int);
    int second = va_arg(*ap, int);
    return first * 10 + second;
}

int llvm_to_clang_public(int named, ...)
{
    va_list ap;
    va_start(ap, named);
    int result = clang_read_public(&ap);
    va_end(ap);
    return result;
}

int llvm_call_clang(void)
{
    char narrow = 7;
    float single = 1.25f;
    return clang_sum_ints(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 55 &&
           clang_check_promotions(41, narrow, single) == 49;
}

#ifdef BUSTER_LLVM_VARIADIC_UNSUPPORTED_ARG
__int128 llvm_wide_arg(int named, ...)
{
    va_list ap;
    va_start(ap, named);
    __int128 result = va_arg(ap, __int128);
    va_end(ap);
    return result;
}
#endif
