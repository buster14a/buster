// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// #3311: structs and unions passed and returned in registers (the psABI's INTEGER and SSE eightbytes, up to 16 bytes),
// whose fields Tier 1 keeps in registers: SROA widens their slots to eightbytes, a one-eightbyte result and register
// parameters become scalars. Every shape is checked against clang's output: fields of mixed widths, floats, padding,
// nesting, arrays, 9 to 16 bytes, fields left unread, a parameter whose address escapes, a parameter written before
// it is read, several returns and a union read by another member than it was written by.
#include <stdio.h>
#include <string.h>

typedef struct { int x, y, z, w; } I4;
typedef struct { int x, y; } I2;
typedef struct { long x, y; } L2;
typedef struct { char a; short b; int c; } Packed;
typedef struct { int a, b, c; } I3;
typedef struct { long a; char b; } LongChar;
typedef struct { float x, y; } F2;
typedef struct { float x, y, z, w; } F4;
typedef struct { double x, y; } D2;
typedef struct { int i; float f; } IntFloat;
typedef struct { double d; int i; } DoubleInt;
typedef struct { int i; double d; } IntDouble;
typedef struct { I2 inner; int tail; } Nested;
typedef struct { unsigned char bytes[12]; } Bytes;
typedef struct { short s[5]; } Shorts;
typedef struct { char c; } One;
typedef struct { void* p; long n; } Slice;
typedef union { long l; int i[2]; float f[2]; } Mixed;

volatile int vz;

__attribute__((noinline)) static I4 add_i4(I4 a, I4 b)
{
    return (I4){ a.x + b.x, a.y + b.y, a.z ^ b.z, a.w - b.w };
}

__attribute__((noinline)) static I2 add_i2(I2 a, I2 b)
{
    return (I2){ a.x + b.x, a.y ^ b.y };
}

__attribute__((noinline)) static L2 add_l2(L2 a, L2 b)
{
    return (L2){ a.x + b.x, a.y ^ b.y };
}

__attribute__((noinline)) static Packed bump_packed(Packed p, int k)
{
    p.a = (char)(p.a + k);
    p.b = (short)(p.b * k);
    p.c = p.c - k;
    return p;
}

__attribute__((noinline)) static I3 rotate_i3(I3 v)
{
    return (I3){ v.b, v.c, v.a };
}

__attribute__((noinline)) static LongChar mix_long_char(LongChar v, char c)
{
    v.a += v.b;
    v.b = c;
    return v;
}

__attribute__((noinline)) static F2 scale_f2(F2 v, float k)
{
    return (F2){ v.x * k, v.y + k };
}

__attribute__((noinline)) static F4 add_f4(F4 a, F4 b)
{
    return (F4){ a.x + b.x, a.y - b.y, a.z * b.z, a.w };
}

__attribute__((noinline)) static D2 swap_d2(D2 v)
{
    return (D2){ v.y, v.x };
}

__attribute__((noinline)) static IntFloat int_float(IntFloat v)
{
    return (IntFloat){ v.i * 3, v.f * 0.5f };
}

__attribute__((noinline)) static DoubleInt double_int(DoubleInt v)
{
    return (DoubleInt){ v.d + v.i, v.i + 1 };
}

__attribute__((noinline)) static IntDouble int_double(IntDouble v)
{
    return (IntDouble){ v.i - 1, v.d * 2 };
}

__attribute__((noinline)) static Nested nest(Nested v)
{
    v.inner.x += v.tail;
    v.tail = v.inner.y;
    return v;
}

__attribute__((noinline)) static Bytes reverse_bytes(Bytes v)
{
    Bytes r;
    for (int i = 0; i < 12; i++)
    {
        r.bytes[i] = v.bytes[11 - i];
    }
    return r;
}

__attribute__((noinline)) static Shorts shorts(Shorts v)
{
    v.s[0] = (short)(v.s[4] + v.s[1]);
    v.s[3] = (short)-v.s[3];
    return v;
}

__attribute__((noinline)) static One one(One v)
{
    v.c = (char)(v.c * 2 + 1);
    return v;
}

__attribute__((noinline)) static Slice advance(Slice s, long k)
{
    return (Slice){ (char*)s.p + k, s.n - k };
}

__attribute__((noinline)) static Mixed punned(Mixed m)
{
    Mixed r;
    r.i[0] = m.i[1];
    r.i[1] = m.i[0] + 1;
    return r;
}

__attribute__((noinline)) static float union_float(Mixed m)
{
    return m.f[0] + m.f[1];
}

// Only one field read; the others are padding to the callee.
__attribute__((noinline)) static int first_only(I4 v)
{
    return v.z;
}

// The address of a parameter escapes: its copy must stay in memory.
__attribute__((noinline)) static void touch(int* p)
{
    *p += 100;
}

__attribute__((noinline)) static I2 escaped(I2 v)
{
    touch(&v.y);
    return v;
}

// Several returns of different objects, and one through a conditional.
__attribute__((noinline)) static I2 choose(I2 a, I2 b, int which)
{
    if (which > 1)
    {
        return a;
    }
    I2 c = { a.x * b.x, a.y * b.y };
    return which ? b : c;
}

__attribute__((noinline)) static L2 loop_sum(L2 v, int n)
{
    L2 acc = { 0, 0 };
    for (int i = 0; i < n; i++)
    {
        acc.x += v.x * i;
        acc.y ^= v.y + i;
    }
    return acc;
}

// A struct read through a pointer to it, copied whole.
__attribute__((noinline)) static I3 from_pointer(const I3* p)
{
    I3 copy = *p;
    copy.b += 1;
    return copy;
}

__attribute__((noinline)) static long sum_args(I2 a, L2 b, long c, F2 d, I3 e)
{
    return a.x + a.y + b.x + b.y + c + (long)(d.x * 10.0f) + (long)(d.y * 10.0f) + e.a + e.b + e.c;
}

typedef struct { short a, b; } S2;

__attribute__((noinline)) static S2 swap_s2(S2 v)
{
    return (S2){ v.b, (short)(v.a + 1) };
}

// Call sites (#3311): register-class arguments passed as their parts and one-part results returned as values, the
// caller's objects then kept in registers, in loops and through branches.
__attribute__((noinline)) static long callers(int n)
{
    I2 i2 = { 1, 2 };
    L2 l2 = { 3, 4 };
    F2 f2 = { 1.0f, 2.0f };
    One one_value = { 1 };
    S2 s2 = { 5, -6 };
    D2 d2 = { 0.5, 1.5 };
    IntFloat ifl = { 1, 1.0f };
    long total = 0;
    for (int i = 0; i < n; i++)
    {
        i2 = add_i2(i2, (I2){ i, 1 });
        l2 = add_l2(l2, (L2){ i, 2 });
        f2 = scale_f2(f2, 0.5f);
        one_value = one(one_value);
        s2 = swap_s2(s2);
        d2 = swap_d2(d2);
        ifl = int_float((IntFloat){ ifl.i % 1000, ifl.f });
        if (i & 1)
        {
            I2 c = choose(i2, (I2){ i, -i }, i % 3);
            total += c.x - c.y;
        }
        total += first_only((I4){ i, 2, i * 3, 4 });
    }
    return total + i2.x + i2.y + l2.x + l2.y + (long)(f2.x * 1000.0f) + (long)(f2.y * 1000.0f) + one_value.c + s2.a + s2.b + (long)d2.x + (long)d2.y + ifl.i + (long)ifl.f;
}

int main(void)
{
    I4 acc4 = { 0 };
    const I4 d4 = { 1, 2, 3, 4 + vz };
    for (int i = 0; i < 1000; i++)
    {
        acc4 = add_i4(acc4, d4);
    }
    printf("i4 %d %d %d %d\n", acc4.x, acc4.y, acc4.z, acc4.w);
    I2 acc2 = { 0, 0 };
    for (int i = 0; i < 1000; i++)
    {
        acc2 = add_i2(acc2, (I2){ i, 2 * i + vz });
    }
    printf("i2 %d %d\n", acc2.x, acc2.y);
    L2 l2 = add_l2((L2){ 1L << 40, -5 }, (L2){ 7, 1L << 33 });
    printf("l2 %ld %ld\n", l2.x, l2.y);
    Packed p = bump_packed((Packed){ 10, -300, 70000 }, 3 + vz);
    printf("packed %d %d %d\n", p.a, p.b, p.c);
    I3 r3 = rotate_i3((I3){ 1, 2, 3 });
    printf("i3 %d %d %d\n", r3.a, r3.b, r3.c);
    LongChar lc = mix_long_char((LongChar){ 1L << 50, 9 }, 'z');
    printf("lc %ld %d\n", lc.a, lc.b);
    F2 f2 = scale_f2((F2){ 1.5f, -2.25f }, 4.0f);
    printf("f2 %g %g\n", (double)f2.x, (double)f2.y);
    F4 f4 = add_f4((F4){ 1, 2, 3, 4 }, (F4){ 0.5f, 0.25f, 2, 8 });
    printf("f4 %g %g %g %g\n", (double)f4.x, (double)f4.y, (double)f4.z, (double)f4.w);
    D2 d2 = swap_d2((D2){ 3.5, -1e300 });
    printf("d2 %g %g\n", d2.x, d2.y);
    IntFloat ifl = int_float((IntFloat){ -7, 3.0f });
    printf("if %d %g\n", ifl.i, (double)ifl.f);
    DoubleInt di = double_int((DoubleInt){ 0.25, 41 });
    printf("di %g %d\n", di.d, di.i);
    IntDouble id = int_double((IntDouble){ 5, -0.5 });
    printf("id %d %g\n", id.i, id.d);
    Nested n = nest((Nested){ { 1, 2 }, 3 });
    printf("nested %d %d %d\n", n.inner.x, n.inner.y, n.tail);
    Bytes b;
    for (int i = 0; i < 12; i++)
    {
        b.bytes[i] = (unsigned char)(i * 17 + vz);
    }
    Bytes rb = reverse_bytes(b);
    for (int i = 0; i < 12; i++)
    {
        printf("%d%c", rb.bytes[i], i == 11 ? '\n' : ' ');
    }
    Shorts s = shorts((Shorts){ { 1, 2, 3, 4, 5 } });
    printf("shorts %d %d %d %d %d\n", s.s[0], s.s[1], s.s[2], s.s[3], s.s[4]);
    One o = one((One){ 21 });
    printf("one %d\n", o.c);
    char text[16];
    memcpy(text, "abcdefghijklmno", 16);
    Slice sl = advance((Slice){ text, 15 }, 4);
    printf("slice %s %ld\n", (char*)sl.p, sl.n);
    Mixed m = { .i = { 3, -4 } };
    Mixed pm = punned(m);
    printf("union %ld %d %d\n", pm.l, pm.i[0], pm.i[1]);
    Mixed mf = { .f = { 1.25f, 2.5f } };
    printf("union float %g\n", (double)union_float(mf));
    printf("first %d\n", first_only((I4){ 1, 2, 33, 4 }));
    I2 e = escaped((I2){ 5, 6 });
    printf("escaped %d %d\n", e.x, e.y);
    for (int w = 0; w < 3; w++)
    {
        I2 c = choose((I2){ 2, 3 }, (I2){ 4, 5 }, w);
        printf("choose %d %d %d\n", w, c.x, c.y);
    }
    L2 ls = loop_sum((L2){ 3, 0x55 }, 20);
    printf("loop %ld %ld\n", ls.x, ls.y);
    const I3 source = { 7, 8, 9 };
    I3 fp = from_pointer(&source);
    printf("pointer %d %d %d\n", fp.a, fp.b, fp.c);
    printf("sum %ld\n", sum_args((I2){ 1, 2 }, (L2){ 3, 4 }, 5, (F2){ 0.5f, 0.7f }, (I3){ 6, 7, 8 }));
    printf("callers %ld %ld\n", callers(10), callers(37 + vz));
    return 0;
}
