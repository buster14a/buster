// Read-only opportunity-census inputs, deliberately retaining aggregate
// owners after direct scalar SSA and canonical local promotion.
struct Pair { int a; int b; };
union Overlay { long long wide; int word; unsigned char bytes[8]; };
extern void unknown_effect(void);
extern void escape_effect(int*);

int separated_store_forward(int x, int y)
{
    struct Pair p;
    p.a = x;
    p.b = y;
    return p.a;
}

int separated_redundant_load(int x, int y)
{
    struct Pair p;
    p.a = x;
    int first = p.a;
    p.b = y;
    int second = p.a;
    return first + second;
}

int separated_overwrite(int x, int y)
{
    struct Pair p;
    p.a = x;
    p.b = y;
    p.a = x + 1;
    return p.a + p.b;
}

int observation_prevents_overwrite(int x, int y)
{
    struct Pair p;
    p.a = x;
    int observed = p.a;
    p.a = y;
    return p.a + observed;
}

int call_barrier(int x)
{
    struct Pair p;
    p.a = x;
    unknown_effect();
    return p.a;
}

int escaped_root(int x)
{
    struct Pair p;
    p.a = x;
    escape_effect(&p.a);
    return p.a;
}

int narrow_overlap_barrier(int x)
{
    union Overlay p;
    p.word = x;
    p.bytes[0] = 3;
    return p.word;
}

int trapping_operation_barrier(int x, int y)
{
    struct Pair p;
    p.a = x;
    p.a = x / y;
    return p.a;
}

int atomic_barrier(int x)
{
    struct AtomicPair { int a; _Atomic int b; } p;
    p.a = x;
    p.b = 1;
    return p.a;
}

int volatile_barrier(int x)
{
    struct VolatilePair { int a; volatile int b; } p;
    p.a = x;
    p.b = 1;
    return p.a;
}

int assembly_barrier(int x)
{
    struct Pair p;
    p.a = x;
    __asm__ volatile("" ::: "memory");
    return p.a;
}

int dynamic_index_barrier(int x, int index)
{
    int p[4];
    p[0] = x;
    p[index] = 7;
    return p[0];
}

int union_partial_overlap(long long x, int y)
{
    union Overlay p;
    p.wide = x;
    p.word = y;
    return p.word;
}
