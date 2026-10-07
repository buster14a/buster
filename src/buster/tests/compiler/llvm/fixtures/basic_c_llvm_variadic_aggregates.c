// Aggregates passed through "..." by the LLVM backend to a Clang-built callee.
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

extern long long clang_va_pair(int count, ...);
extern double clang_va_doubles(int count, ...);
extern double clang_va_mixed(int count, ...);
extern long long clang_va_small_large(int count, ...);
extern long long clang_va_exhausted(long long a, long long b, long long c, long long d, long long e, ...);

static char llvm_va_text[] = "hello";

long long llvm_call_va_pair(void)
{
    VaPair first = {llvm_va_text, 5};
    VaPair second = {llvm_va_text + 1, 4};
    return clang_va_pair(2, first, second);
}

double llvm_call_va_doubles(void)
{
    VaDoubles first = {1.5, 2.5};
    VaDoubles second = {3.0, 4.0};
    return clang_va_doubles(2, first, second);
}

double llvm_call_va_mixed(void)
{
    VaMixed first = {7, 0.5};
    VaMixed second = {9, 1.25};
    return clang_va_mixed(2, first, second);
}

long long llvm_call_va_small_large(void)
{
    VaSmall small = {41};
    VaLarge large = {1000, 200, 30};
    return clang_va_small_large(2, small, large, 5);
}

long long llvm_call_va_exhausted(void)
{
    VaPair pair = {llvm_va_text, 5};
    return clang_va_exhausted(1, 2, 3, 4, 5, pair, 100LL);
}
