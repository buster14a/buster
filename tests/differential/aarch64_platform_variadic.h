#ifndef AARCH64_PLATFORM_VARIADIC_H
#define AARCH64_PLATFORM_VARIADIC_H

typedef __builtin_va_list PlatformVaList;
#if defined(__aarch64__) && (defined(_WIN32) || defined(__APPLE__))
_Static_assert(sizeof(PlatformVaList) == sizeof(void*), "platform va_list is one pointer");
#endif
struct PlatformVaSmall1 { signed char value; };
struct PlatformVaSmall2 { short value; };
struct PlatformVaSmall4 { int value; };
struct PlatformVaSmall8 { long long value; };
struct PlatformVaPair { long long first; long long second; };
struct PlatformVaHfa { double first; double second; };

double platform_va_mixed(double named, int count, ...);
long long platform_va_copy(int count, ...);
long long platform_va_named_stack(long long a, long long b, long long c, long long d,
    long long e, long long f, long long g, long long h, double named, signed char narrow, int count, ...);
double platform_va_named_hfa(struct PlatformVaHfa named, double scalar, int count, ...);
long long platform_va_aggregates(int marker, ...);
long long platform_va_stacked_pair(int count, ...);
long long platform_va_fold(PlatformVaList arguments, int count);
long long platform_va_advance(PlatformVaList* arguments);
long long platform_va_forward(int count, ...);

#if BUSTER_PLATFORM_VA_EXTERNAL
double platform_va_host_mixed(double named, int count, ...);
long long platform_va_host_copy(int count, ...);
long long platform_va_host_named_stack(long long a, long long b, long long c, long long d,
    long long e, long long f, long long g, long long h, double named, signed char narrow, int count, ...);
double platform_va_host_named_hfa(struct PlatformVaHfa named, double scalar, int count, ...);
long long platform_va_host_aggregates(int marker, ...);
long long platform_va_host_stacked_pair(int count, ...);
long long platform_va_host_fold(PlatformVaList arguments, int count);
long long platform_va_host_advance(PlatformVaList* arguments);
long long platform_va_host_forward(int count, ...);
int platform_va_host_run(void);
#endif

#endif
