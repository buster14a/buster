#include <stdarg.h>

extern int llvm_sum_ints(int count, ...);
extern long long llvm_sum_wide(int count, ...);
extern double llvm_sum_doubles(int count, ...);
extern int llvm_mixed(int named, double fixed, ...);
extern int llvm_copy_cursors(int named, ...);
extern int llvm_read_public(va_list* ap);
extern int llvm_to_clang_public(int named, ...);
extern int llvm_call_clang(void);

int clang_read_public(va_list* ap)
{
    int first = va_arg(*ap, int);
    int second = va_arg(*ap, int);
    return first * 10 + second;
}

int clang_sum_ints(int count, ...)
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

int clang_check_promotions(int named, ...)
{
    va_list ap;
    va_start(ap, named);
    int narrow = va_arg(ap, int);
    double single = va_arg(ap, double);
    va_end(ap);
    return named + narrow + (int)single;
}

int clang_to_llvm_public(int named, ...)
{
    va_list ap;
    va_start(ap, named);
    int result = llvm_read_public(&ap);
    va_end(ap);
    return result;
}

int main(void)
{
    int item = 19;
    int result = 0;
    int failures[] = {
        llvm_sum_ints(0) != 0,
        llvm_sum_ints(4, 3, 5, 7, 11) != 26,
        llvm_sum_ints(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 55,
        llvm_sum_wide(7, 0x100000000LL, 2LL, 3LL, 4LL, 5LL, 6LL, 7LL) != 0x10000001BLL,
        llvm_sum_doubles(0) != 0.0,
        llvm_sum_doubles(10, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0) != 27.5,
        llvm_mixed(2, 3.0, (short)4, (float)5.0, (void*)&item) != 15,
        !llvm_copy_cursors(0, 13, 17),
        llvm_to_clang_public(0, 2, 3) != 23,
        clang_to_llvm_public(0, 4, 5) != 45,
        !llvm_call_clang(),
    };
    for (unsigned index = 0; index < sizeof(failures) / sizeof(failures[0]); index += 1)
    {
        if (!result && failures[index])
        {
            result = (int)index + 1;
        }
    }
    return result;
}
