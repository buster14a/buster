// Selecting a case leaves one scalar function for the bounded eBPF test VM.
// The default exports every case for independent Wasm/native execution.
#if !defined(LOCAL_AGGREGATE_CASE) || LOCAL_AGGREGATE_CASE == 0
#if defined(LOCAL_AGGREGATE_CASE)
#define aggregate_sum probe
#endif
unsigned long long aggregate_sum(unsigned long long x, unsigned long long y)
{
    struct Values { unsigned long long lane[2]; };
    struct Values original = {{x, y}};
    struct Values copy = original;
    return copy.lane[0] + copy.lane[1];
}
#endif

#if !defined(LOCAL_AGGREGATE_CASE) || LOCAL_AGGREGATE_CASE == 1
#if defined(LOCAL_AGGREGATE_CASE)
#define aggregate_mutation probe
#endif
unsigned long long aggregate_mutation(unsigned long long x, unsigned long long y)
{
    struct Values { unsigned long long first, second; };
    struct Values original = {x, y};
    struct Values copy = original;
    original.first = 31;
    copy.second += 1;
    return copy.first + original.second + copy.second;
}
#endif

#if !defined(LOCAL_AGGREGATE_CASE) || LOCAL_AGGREGATE_CASE == 2
#if defined(LOCAL_AGGREGATE_CASE)
#define aggregate_packed probe
#endif
unsigned long long aggregate_packed(unsigned long long x, unsigned long long y)
{
    struct __attribute__((packed)) Values { unsigned char tag; unsigned long long value; unsigned char tail; };
    struct Values original = {5, x, 7};
    struct Values copy = original;
    original.value = y;
    return copy.value + original.value + copy.tag + copy.tail;
}
#endif

#if !defined(LOCAL_AGGREGATE_CASE) || LOCAL_AGGREGATE_CASE == 3
#if defined(LOCAL_AGGREGATE_CASE)
#define aggregate_nested probe
#endif
unsigned long long aggregate_nested(unsigned long long x, unsigned long long y)
{
    struct Inner { unsigned long long lane[2]; };
    struct Outer { unsigned char tag; struct Inner inner; unsigned short tail; };
    struct Outer original = {7, {{x, y}}, 9};
    struct Outer copy = original;
    copy.inner.lane[0] = 17;
    return original.inner.lane[0] + copy.inner.lane[1] + original.tag + copy.tail;
}
#endif

#if !defined(LOCAL_AGGREGATE_CASE) || LOCAL_AGGREGATE_CASE == 4
#if defined(LOCAL_AGGREGATE_CASE)
#define aggregate_union probe
#endif
unsigned long long aggregate_union(unsigned long long x, unsigned long long y)
{
    union Values { unsigned long long value; unsigned char bytes[8]; };
    union Values original = {.value = x};
    union Values copy = original;
    original.value = y;
    return copy.value;
}
#endif
