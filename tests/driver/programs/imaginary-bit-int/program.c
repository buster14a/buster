// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2219: imaginary bit-precise constants (5wbi, clang's _Complex _BitInt(N)) and the complex _BitInts and _Complex
// __int128 they make, of 3 to 256 bits, through arithmetic, comparisons, __real__ and __imag__, casts, initializers of
// static and automatic objects, and calls and returns, variadic ones among them. Each line prints a hash of values'
// bytes: clang's build and buster's at each tier must print the same. A part's bits past its width are left out of the
// hash, as clang stores a part as its width's bytes. C23, for the digit separators.
int printf(const char*, ...);

static unsigned long fold(unsigned long h, const void* p, unsigned bytes, unsigned bits)
{
    const unsigned char* b = (const unsigned char*)p;
    for (unsigned i = 0; i < bytes; i++)
    {
        unsigned char c = b[i];
        if (8 * i + 8 > bits)
        {
            c = 8 * i >= bits ? 0 : (unsigned char)(c & ((1u << (bits - 8 * i)) - 1));
        }
        h = (h ^ c) * 1099511628211ul;
    }
    return h;
}

#define FOLDC(x, bits) do { __typeof__(x) t_ = (x); __typeof__(__real__ t_) r_ = __real__ t_, i_ = __imag__ t_; h = fold(h, &r_, sizeof r_, bits); h = fold(h, &i_, sizeof i_, bits); } while (0)
#define FOLDR(x, bits) do { __typeof__(x) t_ = (x); h = fold(h, &t_, sizeof t_, bits); } while (0)

typedef __typeof__(5wbi) C4;
typedef __typeof__(5uwbi) U3;
typedef __typeof__(1wbi * (_BitInt(13))1) C13;
typedef __typeof__(1wbi * (_BitInt(64))1) C64;
typedef __typeof__(1wbi * (_BitInt(65))1) C65;
typedef __typeof__(1wbi * (unsigned _BitInt(100))1) U100;
typedef __typeof__(1wbi * (_BitInt(128))1) C128;
typedef __typeof__(1wbi * (__int128)1) CI128;
typedef __typeof__(1wbi * (_BitInt(200))1) C200;
typedef __typeof__(1wbi * (unsigned _BitInt(256))1) U256;

_Static_assert(sizeof(5wbi) == 2 && sizeof(0xffff'ffff'ffff'ffffwbi) == 32 && sizeof(C200) == 64, "the complex _BitInts' sizes");
_Static_assert(__imag__ 5wbi == 5 && __real__ 5wbi == 0 && __imag__ 5iuwb == 5u, "the constants' parts");

// Static objects: the constants and their folds, of each width.
static C4 s4 = 5wbi;
static C4 s4n = -3wbi;
static U3 su3 = 5uwbi;
static C13 s13 = 5wbi * 300wbi + 7;
static C64 s64 = 0x7fff'ffff'ffff'ffffwbi;
static C65 s65 = 0xffff'ffff'ffff'ffffwbi;
static U100 s100 = 0x1'0000'0000'0000'0000'0000uwbi + 3;
static C128 s128 = -0x1234'5678'9abc'def0'1234'5678wbi;
static CI128 si128 = 5wbi * (__int128)7 - 9;
static C200 s200 = 0x1'0000'0000'0000'0000'0000'0000'0000'0000wbi * 3wbi - 11;
static U256 s256 = 7uwbi * (unsigned _BitInt(256))-1;
static const C65 c65 = ~(2 + 0x8'0000'0000'0000'0000wbi);
static int sr = __real__ (5wbi * 2wbi);
static int si = __imag__ (3wbi + 4wbi);
static _Bool sb = 5wbi;
static _Bool sz = 0wbi;
static long sl = (long)(5wbi * 3wbi);
static double sd = __imag__ (C200)(2.5 + 3wbi);
static _Complex double scd = 0x10'0000'0000'0000'0000wbi + 0.5;
static C4 arr[3] = { 1wbi, -2wbi, 3wbi + 1 };
static struct S { char c; C200 z; U100 u; C4 n; } st = { 1, 0x1234wbi * 0x1234wbi, 5uwbi, -1wbi };
static int sizes[__imag__ 4wbi];

static C200 add200(C200 a, C200 b) { return a + b; }
static C65 mul65(C65 a, C65 b) { return a * b; }
static U100 div100(U100 a, U100 b) { return a / b; }
static C4 neg4(C4 a) { return -a; }
static U3 conjugate3(U3 a) { return ~a; }
static C128 pass128(int n, C128 a, C4 b, C200 c) { return a + b + c + n; }
static C65 sum65(int n, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    C65 r = 0;
    for (int i = 0; i < n; i++)
    {
        r += __builtin_va_arg(ap, C65);
    }
    __builtin_va_end(ap);
    return r;
}

static unsigned long arithmetic(int n)
{
    unsigned long h = 2166136261u;
    C4 a4 = 5wbi;
    C4 b4 = n % 3 + 1wbi;
    C13 a13 = 0x7ffwbi + n;
    C64 a64 = 0x7fff'ffff'ffff'fff0wbi * n + 1;
    C65 a65 = 0xffff'ffff'ffff'ffffwbi + n;
    C65 b65 = (C65)(n + 2) + 3wbi;
    U100 a100 = 0x8'0000'0000'0000'0000'0000uwbi;
    U100 b100 = (U100)((n & 7) + 1);
    C128 a128 = -0x7fff'ffff'ffff'ffff'ffff'ffff'ffff'ffffwbi + n;
    CI128 ai = 5wbi * (__int128)n;
    C200 a200 = 0x1'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000wbi + n;
    C200 b200 = n + 7wbi;
    U256 a256 = 0x1'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000uwbi * n;
    FOLDC(a4 + b4, 4); FOLDC(a4 - b4, 4); FOLDC(a4 * b4, 4); FOLDC(a4 / b4, 4); FOLDC(-a4, 4); FOLDC(~a4, 4);
    FOLDC(a13 * a13, 13); FOLDC(a13 / 3wbi, 13);
    FOLDC(a64 + a64, 64); FOLDC(a64 * 2wbi, 64);
    FOLDC(a65 * b65, 65); FOLDC(a65 - b65, 65); FOLDC(a65 / b65, 65);
    FOLDC(a100 / b100, 100); FOLDC(~a100, 100); FOLDC(a100 * 3uwbi, 100);
    FOLDC(a128 + 1wbi, 128); FOLDC(a128 * 2wbi, 128);
    FOLDC(ai * ai, 128); FOLDC(ai + 3wbi, 128);
    FOLDC(a200 * b200, 200); FOLDC(a200 / b200, 200); FOLDC(-a200, 200); FOLDC(~a200, 200);
    FOLDC(a256 + 1uwbi, 256); FOLDC(a256 * 3uwbi, 256);
    // Updates: compound assignments, increments and parts as lvalues.
    C65 z = a65;
    z += 5wbi; z *= 2wbi; z -= n; z /= 1 + 1wbi; ++z; z--;
    __real__ z = 3; __imag__ z += 4wb;
    FOLDC(z, 65);
    C4 y = b4;
    y *= y; y++;
    FOLDC(y, 4);
    return h;
}

static unsigned long statics(void)
{
    unsigned long h = 2166136261u;
    FOLDC(s4, 4); FOLDC(s4n, 4); FOLDC(su3, 3); FOLDC(s13, 13); FOLDC(s64, 64); FOLDC(s65, 65); FOLDC(s100, 100); FOLDC(s128, 128);
    FOLDC(si128, 128); FOLDC(s200, 200); FOLDC(s256, 256); FOLDC(c65, 65); FOLDC(st.z, 200); FOLDC(st.u, 100); FOLDC(st.n, 4);
    FOLDC(arr[0], 4); FOLDC(arr[1], 4); FOLDC(arr[2], 4); FOLDC(scd, 64);
    h += (unsigned long)sr * 7 + (unsigned long)si * 11 + sb * 13 + sz * 17 + (unsigned long)sl * 19 + (unsigned long)(sd * 4) + sizeof sizes;
    return h;
}

static unsigned long calls(int n)
{
    unsigned long h = 2166136261u;
    C4 a4 = n - 2wbi;
    C65 a65 = 0xffff'ffff'ffff'ffffwbi + n;
    C65 b65 = (C65)(n + 2) + 3wbi;
    U100 a100 = 0x8'0000'0000'0000'0000'0000uwbi;
    C128 a128 = -0x7fff'ffff'ffff'ffff'ffff'ffff'ffff'ffffwbi + n;
    C200 a200 = 0x1'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000wbi + n;
    FOLDC(add200(a200, 7wbi), 200); FOLDC(mul65(a65, b65), 65); FOLDC(div100(a100, 3uwbi + n), 100); FOLDC(neg4(a4), 4);
    FOLDC(conjugate3(5uwbi), 3); FOLDC(pass128(n, a128, a4, a200), 128); FOLDC(sum65(3, a65, b65, 0x1'0000'0000'0000'0000wbi), 65);
    return h;
}

static unsigned long conversions(int n)
{
    unsigned long h = 2166136261u;
    C4 a4 = 5wbi + n;
    C65 a65 = 0xffff'ffff'ffff'ffffwbi - n;
    C200 a200 = 0x1'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000wbi + n;
    FOLDR((int)a4, 32); FOLDR((long)a200, 64); FOLDR((double)a65, 64); FOLDR((_Bool)a200, 8); FOLDR((_BitInt(200))a65, 200);
    FOLDR((_Bool)(a4 - n - 5wbi), 8); FOLDR((unsigned char)a65, 8);
    FOLDC((_Complex double)a200, 64); FOLDC((_Complex int)a65, 32); FOLDC((C200)a4, 200); FOLDC((C4)a200, 4); FOLDC((C65)1.5, 65);
    FOLDC((C4)(n + 0.5), 4); FOLDC(n ? 5wbi : 3, 32); FOLDC(n & 1 ? a65 : 0x7wbi, 65);
    return h;
}

static unsigned long comparisons(int n)
{
    C4 a4 = 5wbi;
    C4 b4 = n % 3 + 1wbi;
    C65 a65 = 0xffff'ffff'ffff'ffffwbi + n;
    C65 b65 = (C65)(n + 2) + 3wbi;
    U100 a100 = 0x8'0000'0000'0000'0000'0000uwbi * (n & 1);
    C128 a128 = -0x7fff'ffff'ffff'ffff'ffff'ffff'ffff'ffffwbi + n;
    C200 a200 = 0x1'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000wbi + n;
    C200 b200 = n + 7wbi;
    U256 a256 = 0x1'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000uwbi * n;
    unsigned long h = (unsigned long)(a4 == b4) + (a4 != b4) * 2ul + (a65 != b65) * 4ul + !a100 * 8ul + (a128 == 0) * 16ul + (5wbi == 5i) * 32ul +
                      (a200 == b200) * 64ul + (a200 != 0x1'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000wbi) * 128ul + (b4 == 1 + 1wbi) * 256ul +
                      (a65 == 0xffff'ffff'ffff'ffffwbi) * 512ul;
    h += a4 ? 3 : 0;
    h += !a256 ? 5 : 0;
    h += a100 && a200 ? 7 : 0;
    h += __real__ a4 + __imag__ a4 * 3 + (unsigned long)__imag__ s200 + (unsigned long)__real__ s65;
    return h;
}

int main(void)
{
    printf("statics %lu\n", statics());
    for (int n = 0; n < 6; n++)
    {
        printf("%d arithmetic %lu calls %lu conversions %lu comparisons %lu\n", n, arithmetic(n), calls(n), conversions(n), comparisons(n));
    }
    return 0;
}
