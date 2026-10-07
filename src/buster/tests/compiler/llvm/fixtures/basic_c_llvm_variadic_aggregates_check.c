#include <stdarg.h>

typedef struct VaPair VaPair;
struct VaPair
{
    char* pointer;
    unsigned long long length;
};

typedef struct VaDoubles VaDoubles;
struct VaDoubles
{
    double first;
    double second;
};

typedef struct VaMixed VaMixed;
struct VaMixed
{
    long long integer;
    double real;
};

typedef struct VaSmall VaSmall;
struct VaSmall
{
    int value;
};

typedef struct VaLarge VaLarge;
struct VaLarge
{
    long long first;
    long long second;
    long long third;
};

extern long long llvm_call_va_pair(void);
extern double llvm_call_va_doubles(void);
extern double llvm_call_va_mixed(void);
extern long long llvm_call_va_small_large(void);
extern long long llvm_call_va_exhausted(void);

long long clang_va_pair(int count, ...)
{
    va_list ap;
    va_start(ap, count);
    long long result = 0;
    for (int index = 0; index < count; index += 1)
    {
        VaPair pair = va_arg(ap, VaPair);
        result = result * 1000 + (long long)pair.length * 100 + pair.pointer[0];
    }
    va_end(ap);
    return result;
}

double clang_va_doubles(int count, ...)
{
    va_list ap;
    va_start(ap, count);
    double result = 0.0;
    for (int index = 0; index < count; index += 1)
    {
        VaDoubles pair = va_arg(ap, VaDoubles);
        result = result * 10.0 + pair.first + pair.second * 2.0;
    }
    va_end(ap);
    return result;
}

double clang_va_mixed(int count, ...)
{
    va_list ap;
    va_start(ap, count);
    double result = 0.0;
    for (int index = 0; index < count; index += 1)
    {
        VaMixed mixed = va_arg(ap, VaMixed);
        result = result * 100.0 + (double)mixed.integer + mixed.real;
    }
    va_end(ap);
    return result;
}

long long clang_va_small_large(int count, ...)
{
    va_list ap;
    va_start(ap, count);
    VaSmall small = va_arg(ap, VaSmall);
    VaLarge large = va_arg(ap, VaLarge);
    int tail = va_arg(ap, int);
    va_end(ap);
    return small.value + large.first + large.second * 10 + large.third * 100 + tail * 10000;
}

long long clang_va_exhausted(long long a, long long b, long long c, long long d, long long e, ...)
{
    va_list ap;
    va_start(ap, e);
    VaPair pair = va_arg(ap, VaPair);
    long long tail = va_arg(ap, long long);
    va_end(ap);
    return a + b + c + d + e + (long long)pair.length + pair.pointer[1] + tail;
}

int main(void)
{
    int failures[] = {
        llvm_call_va_pair() != (5 * 100 + 'h') * 1000 + 4 * 100 + 'e',
        llvm_call_va_doubles() != (1.5 + 5.0) * 10.0 + 3.0 + 8.0,
        llvm_call_va_mixed() != (7.5) * 100.0 + 10.25,
        llvm_call_va_small_large() != 41 + 1000 + 2000 + 3000 + 50000,
        llvm_call_va_exhausted() != 15 + 5 + 'e' + 100,
    };
    int result = 0;
    for (unsigned index = 0; index < sizeof(failures) / sizeof(failures[0]); index += 1)
    {
        if (!result && failures[index])
        {
            result = (int)index + 1;
        }
    }
    return result;
}
