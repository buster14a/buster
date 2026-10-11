#ifndef LARGE_VA_HOST
#define LARGE_VA_HOST 0
#endif

#ifndef LARGE_VA_MIXED
#define LARGE_VA_MIXED 0
#endif

#define LARGE_VA_JOIN_INNER(left, right) left##right
#define LARGE_VA_JOIN(left, right) LARGE_VA_JOIN_INNER(left, right)

#if LARGE_VA_HOST
#define LARGE_VA_PREFIX host_large_va_
#else
#define LARGE_VA_PREFIX buster_large_va_
#endif

#define NAME(name) LARGE_VA_JOIN(LARGE_VA_PREFIX, name)

enum
{
    LARGE_VA_BIG_COUNT = 32,
};

typedef struct A32 A32;
struct A32
{
    _Alignas(32) long long a[4];
};

typedef struct S24 S24;
struct S24
{
    long long a[3];
};

typedef struct S20 S20;
struct S20
{
    int a[5];
};

typedef struct H4 H4;
struct H4
{
    double a[4];
};

typedef struct Big Big;
struct Big
{
    long long a[LARGE_VA_BIG_COUNT];
};

int NAME(check)(int count, ...)
{
    (void)count;
    __builtin_va_list arguments;
    __builtin_va_list saved;
    __builtin_va_start(arguments, count);
    int bad = 0;
    int marker = __builtin_va_arg(arguments, int);
    A32 a32 = __builtin_va_arg(arguments, A32);
    __builtin_va_copy(saved, arguments);
    S24 copied_s24 = __builtin_va_arg(saved, S24);
    S24 s24 = __builtin_va_arg(arguments, S24);
    S20 s20 = __builtin_va_arg(arguments, S20);
    H4 h4 = __builtin_va_arg(arguments, H4);
    Big big = __builtin_va_arg(arguments, Big);
    long long tail = __builtin_va_arg(arguments, long long);
    __builtin_va_end(saved);
    __builtin_va_end(arguments);
    if (marker != 91)
    {
        bad |= 1;
    }
    if (((unsigned long long)&a32 & 31u) != 0)
    {
        bad |= 2;
    }
    if (a32.a[0] != 101 || a32.a[1] != 102 || a32.a[2] != 103 || a32.a[3] != 104)
    {
        bad |= 4;
    }
    if (copied_s24.a[0] != 201 || copied_s24.a[1] != 202 || copied_s24.a[2] != 203)
    {
        bad |= 8;
    }
    if (s24.a[0] != 201 || s24.a[1] != 202 || s24.a[2] != 203)
    {
        bad |= 16;
    }
    if (s20.a[0] != 301 || s20.a[1] != 302 || s20.a[2] != 303 || s20.a[3] != 304 || s20.a[4] != 305)
    {
        bad |= 32;
    }
    if (h4.a[0] != 401.5 || h4.a[1] != 402.5 || h4.a[2] != 403.5 || h4.a[3] != 404.5)
    {
        bad |= 64;
    }
    for (int index = 0; index < LARGE_VA_BIG_COUNT; index += 1)
    {
        if (big.a[index] != 501 + index)
        {
            bad |= 128;
        }
    }
    if (tail != 701)
    {
        bad |= 256;
    }
    return bad;
}

int NAME(call)(int (*check)(int, ...))
{
    A32 a32 = {{101, 102, 103, 104}};
    S24 s24 = {{201, 202, 203}};
    S20 s20 = {{301, 302, 303, 304, 305}};
    H4 h4 = {{401.5, 402.5, 403.5, 404.5}};
    Big big;
    for (int index = 0; index < LARGE_VA_BIG_COUNT; index += 1)
    {
        big.a[index] = 501 + index;
    }
    return check(0, 91, a32, s24, s20, h4, big, 701LL);
}

#if !LARGE_VA_HOST
#if LARGE_VA_MIXED
extern int host_large_va_check(int count, ...);
extern int host_large_va_call(int (*check)(int, ...));
#endif

int main(void)
{
    int bad = NAME(call)(NAME(check));
#if LARGE_VA_MIXED
    bad |= NAME(call)(host_large_va_check) << 1;
    bad |= host_large_va_call(NAME(check)) << 2;
#endif
    return bad;
}
#endif
