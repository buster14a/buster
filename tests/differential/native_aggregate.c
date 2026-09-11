typedef __builtin_va_list va_list;
struct NativeAggregate3 { unsigned char bytes[3]; };
struct NativeAggregate5 { unsigned char bytes[5]; };
struct NativeAggregate7 { unsigned char bytes[7]; };
struct NativeAggregate12 { unsigned words[3]; };
struct NativeAggregate16 { long long words[2]; };
struct NativeAggregate24 { long long words[3]; };
struct NativeAggregate32 { long long words[4]; };
struct NativeAggregateAligned { _Alignas(16) long long words[2]; };

long long native_aggregate_mix(volatile struct NativeAggregate3 a, double b, volatile struct NativeAggregate24 c, long long d,
                             volatile struct NativeAggregate5 e, double f, volatile struct NativeAggregate7 g,
                             volatile struct NativeAggregate12 h, volatile struct NativeAggregate32 i)
{
    long long result = (long long)(b * 2 + f * 3) + d;
    for (int n = 0; n < 3; n += 1) { result += (n + 1) * a.bytes[n] + (n + 4) * c.words[n] + (n + 7ll) * h.words[n]; }
    for (int n = 0; n < 5; n += 1) { result += (n + 10) * e.bytes[n]; }
    for (int n = 0; n < 7; n += 1) { result += (n + 15) * g.bytes[n]; }
    for (int n = 0; n < 4; n += 1) { result += (n + 22) * i.words[n]; }
    // Force stores into by-value parameters so the caller must provide copies.
    a.bytes[0] = 99; c.words[2] = 99; e.bytes[4] = 99;
    g.bytes[6] = 99; h.words[2] = 99; i.words[3] = 99;
    return result;
}

struct NativeAggregate24 native_aggregate_result(struct NativeAggregate16 a, double b, struct NativeAggregate24 c, int d,
                                                struct NativeAggregateAligned e)
{
    struct NativeAggregate24 result = {{a.words[0] + a.words[1], (long long)b + c.words[2] + d, e.words[0] + e.words[1]}};
    return result;
}

long long native_aggregate_variadic(struct NativeAggregate16 first, int count, ...)
{
    va_list ap;
    __builtin_va_start(ap, count);
    struct NativeAggregate3 a = __builtin_va_arg(ap, struct NativeAggregate3);
    double b = __builtin_va_arg(ap, double);
    struct NativeAggregate16 c = __builtin_va_arg(ap, struct NativeAggregate16);
    struct NativeAggregate7 d = __builtin_va_arg(ap, struct NativeAggregate7);
    long long tail = __builtin_va_arg(ap, long long);
    __builtin_va_end(ap);
    return first.words[0] + 2 * first.words[1] + count + a.bytes[0] + a.bytes[2] + (long long)b +
           3 * c.words[0] + 4 * c.words[1] + d.bytes[0] + d.bytes[6] + tail;
}

struct NativeAggregate24 native_aggregate_dispatch(
    struct NativeAggregate24 (*function)(struct NativeAggregate16, double, struct NativeAggregate24, int, struct NativeAggregateAligned),
    struct NativeAggregate16 a, struct NativeAggregate24 b, struct NativeAggregateAligned c)
{
    return function(a, 7.0, b, 32, c);
}

long long native_aggregate_host_mix(struct NativeAggregate3, double, struct NativeAggregate24, long long, struct NativeAggregate5,
                                  double, struct NativeAggregate7, struct NativeAggregate12, struct NativeAggregate32);
struct NativeAggregate24 native_aggregate_host_result(struct NativeAggregate16, double, struct NativeAggregate24, int,
                                                     struct NativeAggregateAligned);
long long native_aggregate_host_variadic(struct NativeAggregate16, int, ...);
long long native_aggregate_host_variadic_large(int, ...);


int native_aggregate_call_host(void)
{
    struct NativeAggregate3 a = {{1, 2, 3}};
    struct NativeAggregate5 e = {{4, 5, 6, 7, 8}};
    struct NativeAggregate7 g = {{9, 10, 11, 12, 13, 14, 15}};
    struct NativeAggregate12 h = {{16, 17, 18}};
    struct NativeAggregate16 p = {{19, 20}};
    struct NativeAggregate24 c = {{21, 22, 23}};
    struct NativeAggregate32 i = {{24, 25, 26, 27}};
    struct NativeAggregateAligned aligned = {{28, 29}};
    int bad = 0;
    for (int call = 0; call < 3; call += 1)
    {
        bad |= native_aggregate_host_mix(a, 2.5, c, 31ll, e, 3.5, g, h, i) != 5114;
        bad |= a.bytes[0] != 1 || c.words[2] != 23 || e.bytes[4] != 8 || g.bytes[6] != 15 || h.words[2] != 18 || i.words[3] != 27;
        struct NativeAggregate24 result = native_aggregate_dispatch(native_aggregate_host_result, p, c, aligned);
        bad |= result.words[0] != 39 || result.words[1] != 62 || result.words[2] != 57;
        bad |= native_aggregate_host_variadic(p, 5, a, 11.0, p, g, 33ll) != 273;
        bad |= native_aggregate_host_variadic_large(11, 2.0, i, c, 3.0, p) != 271;
    }
    return bad;
}
