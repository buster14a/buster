// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #1239 fixture. Print each result's representation, not a sum/hash that could hide a rounding difference.
// The first triple's exact product plus addend is -2^-46 (float) or -2^-104 (double); two roundings give positive zero.
// Keep the fixture outside the compiler's unity unit: the driver compiles this same file with Buster and Clang -O0.

#include <fenv.h>

int printf(const char* format, ...);

static unsigned visits[3];

static __attribute__((noinline)) float mark_f(float value, unsigned index)
{
    visits[index] += 1;
    return value;
}

static __attribute__((noinline)) double mark_d(double value, unsigned index)
{
    visits[index] += 1;
    return value;
}

// Parentheses, same-format casts, the comma's right value and __arithmetic_fence preserve Clang's contraction
// opportunity. Assignment, a statement expression, a conditional join and an actual narrowing conversion do not.
#define DEFINE_SHAPES(T, S) \
static __attribute__((noinline)) T shapes_##S(unsigned shape, T a, T b, T c, T d) \
{ \
    T result; \
    switch (shape) \
    { \
        case 0: result = a * b + c; break; \
        case 1: result = c + a * b; break; \
        case 2: result = a * b - c; break; \
        case 3: result = c - a * b; break; \
        case 4: result = -(a * b) + c; break; \
        case 5: result = -a * b + c; break; \
        case 6: result = -(a * b + c); break; \
        case 7: result = -(a * b) - c; break; \
        case 8: result = a * b + -c; break; \
        case 9: result = ((a * b)) + c; break; \
        case 10: result = (T)(a * b) + c; break; \
        case 11: { T product = a * b; result = product + c; } break; \
        case 12: { T product; result = (product = a * b) + c; } break; \
        case 13: result = ((void)0, a * b) + c; break; \
        case 14: result = ({ a * b; }) + c; break; \
        case 15: result = (a != 0 ? a * b : b) + c; break; \
        case 16: result = a * b + c * d; break; \
        case 17: result = a * b + c + d; break; \
        case 18: result = (a * b + c) * d + c; break; \
        case 19: result = __arithmetic_fence(a * b) + c; break; \
        case 20: { volatile T product = a * b; result = product + c; } break; \
        case 21: c += a * b; result = c; break; \
        case 22: a *= b; result = a + c; break; \
        case 23: result = mark_##S(a, 0) * mark_##S(b, 1) + mark_##S(c, 2); break; \
        case 24: result = a * b - c * d; break; \
        case 25: result = (1 ? a * b : (T)0) + c; break; \
        case 26: result = ((visits[0] += 1), a * b) + c; break; \
        case 27: result = c + ((visits[1] += 1), a * b); break; \
        case 28: result = (((visits[0] += 1), 1) ? a * b : (T)0) + c; break; \
        case 29: { const int selected = 1; result = (selected ? a * b : (T)0) + c; } break; \
        default: result = (T)0; break; \
    } \
    return result; \
}

DEFINE_SHAPES(float, f)
DEFINE_SHAPES(double, d)

#undef DEFINE_SHAPES

static __attribute__((noinline)) float narrowed(double a, double b, float c)
{
    return (float)(a * b) + c;
}

static __attribute__((noinline)) double widened(float a, float b, double c)
{
    return (double)(a * b) + c;
}

// The product is already a source constant. Clang rounds it during constant folding before adding a dynamic value;
// contraction must not resurrect its original multiplication from the graph after that rounding decision.
static float literal_f(float c) { return 0x1.000002p0f * 0x1.fffffcp-1f + c; }
static double literal_d(double c) { return 0x1.0000000000001p0 * 0x1.ffffffffffffep-1 + c; }
static float constant_f(float c) { const float a = 0x1.000002p0f, b = 0x1.fffffcp-1f; return a * b + c; }
static double constant_d(double c) { const double a = 0x1.0000000000001p0, b = 0x1.ffffffffffffep-1; return a * b + c; }

// Clang's O0 constant-emission boundary is narrower than arbitrary readonly memory. Array elements, record fields
// and an explicitly dereferenced address still participate in runtime contraction, unlike the direct scalar reads.
static float array_f(float c) { static const float values[] = { 0x1.000002p0f, 0x1.fffffcp-1f }; return values[0] * values[1] + c; }
static double array_d(double c) { static const double values[] = { 0x1.0000000000001p0, 0x1.ffffffffffffep-1 }; return values[0] * values[1] + c; }
static float field_f(float c) { static const struct { float a, b; } values = { 0x1.000002p0f, 0x1.fffffcp-1f }; return values.a * values.b + c; }
static double field_d(double c) { static const struct { double a, b; } values = { 0x1.0000000000001p0, 0x1.ffffffffffffep-1 }; return values.a * values.b + c; }
static float address_f(float c) { const float a = 0x1.000002p0f, b = 0x1.fffffcp-1f; return *(&a) * b + c; }
static double address_d(double c) { const double a = 0x1.0000000000001p0, b = 0x1.ffffffffffffep-1; return *(&a) * b + c; }
static float sequence_f(float c) { return ((visits[0] += 1), 0x1.000002p0f) * 0x1.fffffcp-1f + c; }
static double sequence_d(double c) { return ((visits[0] += 1), 0x1.0000000000001p0) * 0x1.ffffffffffffep-1 + c; }
static float statement_literal_f(float c) { return ({ 0x1.000002p0f; }) * 0x1.fffffcp-1f + c; }
static double statement_literal_d(double c) { return ({ 0x1.0000000000001p0; }) * 0x1.ffffffffffffep-1 + c; }
static float statement_all_literal_f(void) { return ({ 0x1.000002p0f; }) * 0x1.fffffcp-1f - 1.0f; }
static double statement_all_literal_d(void) { return ({ 0x1.0000000000001p0; }) * 0x1.ffffffffffffep-1 - 1.0; }
static int statement_compare_f(void) { return (({ 0x1.000002p0f; }) * 0x1.fffffcp-1f - 1.0f) < 0.0f; }
static int statement_compare_d(void) { return (({ 0x1.0000000000001p0; }) * 0x1.ffffffffffffep-1 - 1.0) < 0.0; }
static float statement_add_f(void) { return (({ 0x1.000002p0f; }) * 0x1.fffffcp-1f - 1.0f) + 0.0f; }
static double statement_add_d(void) { return (({ 0x1.0000000000001p0; }) * 0x1.ffffffffffffep-1 - 1.0) + 0.0; }
static int statement_choose_f(void) { return (({ 0x1.000002p0f; }) * 0x1.fffffcp-1f - 1.0f) ? 11 : 13; }
static int statement_choose_d(void) { return (({ 0x1.0000000000001p0; }) * 0x1.ffffffffffffep-1 - 1.0) ? 11 : 13; }
static float assignment_constant_f(float c) { const float a = 0x1.000002p0f; float copy; return (copy = a) * 0x1.fffffcp-1f + c; }
static double assignment_constant_d(double c) { const double a = 0x1.0000000000001p0; double copy; return (copy = a) * 0x1.ffffffffffffep-1 + c; }
static float integer_constant_f(float c) { const int k = -8388610; const float b = 0x1.fffffcp-1f; return (float)(~k) * b + c; }
static double integer_constant_d(double c) { const long k = -4503599627370498l; const double b = 0x1.ffffffffffffep-1; return (double)(~k) * b + c; }
static float compound_constant_f(float c) { return (const float){ 0x1.000002p0f } * 0x1.fffffcp-1f + c; }
static double compound_constant_d(double c) { return (const double){ 0x1.0000000000001p0 } * 0x1.ffffffffffffep-1 + c; }
__attribute__((weak)) const float weak_value_f = 0x1.000002p0f;
__attribute__((weak)) const double weak_value_d = 0x1.0000000000001p0;
static float weak_constant_f(float c) { return weak_value_f * 0x1.fffffcp-1f + c; }
static double weak_constant_d(double c) { return weak_value_d * 0x1.ffffffffffffep-1 + c; }

#pragma STDC FP_CONTRACT OFF
static float stdc_off_f(float a, float b, float c) { return a * b + c; }
static double stdc_off_d(double a, double b, double c) { return a * b + c; }

#pragma STDC FP_CONTRACT ON
#if 0
#pragma STDC FP_CONTRACT OFF
#endif
static float stdc_on_f(float a, float b, float c) { return a * b + c; }
static double stdc_on_d(double a, double b, double c) { return a * b + c; }

#pragma STDC FP_CONTRACT DEFAULT
static float stdc_default_f(float a, float b, float c) { return a * b + c; }
static double stdc_default_d(double a, double b, double c) { return a * b + c; }

#pragma float_control(push)
#pragma clang fp contract(off)
static float pushed_off_f(float a, float b, float c) { return a * b + c; }
static double pushed_off_d(double a, double b, double c) { return a * b + c; }
#pragma float_control(push)
#pragma clang fp contract(on)
static float pushed_on_f(float a, float b, float c) { return a * b + c; }
static double pushed_on_d(double a, double b, double c) { return a * b + c; }
#pragma float_control(pop)
static float popped_off_f(float a, float b, float c) { return a * b + c; }
static double popped_off_d(double a, double b, double c) { return a * b + c; }
#pragma float_control(pop)
static float popped_default_f(float a, float b, float c) { return a * b + c; }
static double popped_default_d(double a, double b, double c) { return a * b + c; }

#define DEFINE_SCOPES(T, S) \
static T scoped_##S(T a, T b, T c, unsigned nested) \
{ \
    _Pragma("clang fp contract(off)") \
    T result; \
    if (nested != 0) \
    { \
        _Pragma("clang fp contract(on)") \
        result = a * b + c; \
    } \
    else \
    { \
        result = a * b + c; \
    } \
    return result; \
} \
static T fast_##S(T a, T b, T c) \
{ \
    _Pragma("clang fp contract(fast)") \
    return a * b + c; \
} \
static T strict_##S(T a, T b, T c) \
{ \
    _Pragma("STDC FENV_ACCESS ON") \
    return a * b + c; \
}

DEFINE_SCOPES(float, f)
DEFINE_SCOPES(double, d)

#undef DEFINE_SCOPES

static unsigned bits_f(float value)
{
    union { float value; unsigned bits; } result = { .value = value };
    return result.bits;
}

static unsigned long bits_d(double value)
{
    union { double value; unsigned long bits; } result = { .value = value };
    return result.bits;
}

int main(void)
{
    static const unsigned inputs_f[][3] = {
        { 0x3f800001u, 0x3f7ffffeu, 0xbf800000u },
        { 0x3f800001u, 0x3f7ffffeu, 0x3f800000u },
        { 0x7f7fffffu, 0x3f800001u, 0xff7fffffu },
        { 0x00800000u, 0x3effffffu, 0x00000001u },
        { 0x00000000u, 0x80000000u, 0x80000000u },
        { 0x80000000u, 0x3f800000u, 0x00000000u },
        { 0x7f800000u, 0x00000000u, 0x3f800000u },
        { 0x3f800000u, 0x40000000u, 0xff800000u },
        { 0x7fc01234u, 0x40000000u, 0x3f800000u },
        { 0x3f800000u, 0xffc05678u, 0x3f000000u },
        { 0x3f800000u, 0x40000000u, 0x7fc09abcu },
        { 0x7f801234u, 0x40000000u, 0x3f800000u },
        { 0x3f800000u, 0xff805678u, 0x3f000000u },
        { 0x3f800000u, 0x40000000u, 0x7f809abcu },
    };
    static const unsigned long inputs_d[][3] = {
        { 0x3ff0000000000001ul, 0x3feffffffffffffeul, 0xbff0000000000000ul },
        { 0x3ff0000000000001ul, 0x3feffffffffffffeul, 0x3ff0000000000000ul },
        { 0x7feffffffffffffful, 0x3ff0000000000001ul, 0xffeffffffffffffful },
        { 0x0010000000000000ul, 0x3fdffffffffffffful, 0x0000000000000001ul },
        { 0x0000000000000000ul, 0x8000000000000000ul, 0x8000000000000000ul },
        { 0x8000000000000000ul, 0x3ff0000000000000ul, 0x0000000000000000ul },
        { 0x7ff0000000000000ul, 0x0000000000000000ul, 0x3ff0000000000000ul },
        { 0x3ff0000000000000ul, 0x4000000000000000ul, 0xfff0000000000000ul },
        { 0x7ff8000000001234ul, 0x4000000000000000ul, 0x3ff0000000000000ul },
        { 0x3ff0000000000000ul, 0xfff8000000005678ul, 0x3fe0000000000000ul },
        { 0x3ff0000000000000ul, 0x4000000000000000ul, 0x7ff8000000009abcul },
        { 0x7ff0000000001234ul, 0x4000000000000000ul, 0x3ff0000000000000ul },
        { 0x3ff0000000000000ul, 0xfff0000000005678ul, 0x3fe0000000000000ul },
        { 0x3ff0000000000000ul, 0x4000000000000000ul, 0x7ff0000000009abcul },
    };
    for (unsigned input = 0; input < sizeof(inputs_f) / sizeof(inputs_f[0]); input += 1)
    {
        union { unsigned bits; float value; } af = { inputs_f[input][0] }, bf = { inputs_f[input][1] }, cf = { inputs_f[input][2] };
        union { unsigned long bits; double value; } ad = { inputs_d[input][0] }, bd = { inputs_d[input][1] }, cd = { inputs_d[input][2] };
        for (unsigned shape = 0; shape < 30; shape += 1)
        {
            visits[0] = visits[1] = visits[2] = 0;
            const unsigned f = bits_f(shapes_f(shape, af.value, bf.value, cf.value, 1.5f));
            const unsigned long d = bits_d(shapes_d(shape, ad.value, bd.value, cd.value, 1.5));
            printf("shape %u %u %08x %016lx %u %u %u\n", input, shape, f, d, visits[0], visits[1], visits[2]);
        }
        printf("narrow %u %08x\n", input, bits_f(narrowed(ad.value, bd.value, cf.value)));
        printf("widen %u %016lx\n", input, bits_d(widened(af.value, bf.value, cd.value)));
        printf("folded %u %08x %016lx %08x %016lx\n", input, bits_f(literal_f(cf.value)), bits_d(literal_d(cd.value)),
               bits_f(constant_f(cf.value)), bits_d(constant_d(cd.value)));
        printf("readonly %u %08x %016lx %08x %016lx %08x %016lx\n", input, bits_f(array_f(cf.value)), bits_d(array_d(cd.value)),
               bits_f(field_f(cf.value)), bits_d(field_d(cd.value)), bits_f(address_f(cf.value)), bits_d(address_d(cd.value)));
        visits[0] = 0;
        const unsigned sequence_float = bits_f(sequence_f(cf.value));
        const unsigned long sequence_double = bits_d(sequence_d(cd.value));
        printf("constant-sequence %u %08x %016lx %u\n", input, sequence_float, sequence_double, visits[0]);
        printf("constant-boundaries %u %08x %016lx %08x %016lx\n", input,
               bits_f(statement_literal_f(cf.value)), bits_d(statement_literal_d(cd.value)),
               bits_f(assignment_constant_f(cf.value)), bits_d(assignment_constant_d(cd.value)));
        printf("constant-objects %u %08x %016lx %08x %016lx\n", input,
               bits_f(compound_constant_f(cf.value)), bits_d(compound_constant_d(cd.value)),
               bits_f(weak_constant_f(cf.value)), bits_d(weak_constant_d(cd.value)));
#define PRINT_POLICY(NAME) printf(#NAME " %u %08x %016lx\n", input, bits_f(NAME##_f(af.value, bf.value, cf.value)), bits_d(NAME##_d(ad.value, bd.value, cd.value)))
        PRINT_POLICY(stdc_off);
        PRINT_POLICY(stdc_on);
        PRINT_POLICY(stdc_default);
        PRINT_POLICY(pushed_off);
        PRINT_POLICY(pushed_on);
        PRINT_POLICY(popped_off);
        PRINT_POLICY(popped_default);
        PRINT_POLICY(fast);
#undef PRINT_POLICY
        for (unsigned nested = 0; nested < 2; nested += 1)
        {
            printf("scope %u %u %08x %016lx\n", input, nested, bits_f(scoped_f(af.value, bf.value, cf.value, nested)),
                   bits_d(scoped_d(ad.value, bd.value, cd.value, nested)));
        }
    }
    printf("statement-all-literal %08x %016lx\n", bits_f(statement_all_literal_f()), bits_d(statement_all_literal_d()));
    printf("statement-ancestors %d %d %08x %016lx %d %d\n", statement_compare_f(), statement_compare_d(),
           bits_f(statement_add_f()), bits_d(statement_add_d()), statement_choose_f(), statement_choose_d());
    printf("constant-integer %08x %016lx\n", bits_f(integer_constant_f(-0x1p23f)), bits_d(integer_constant_d(-0x1p52)));
    static const int modes[] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    for (unsigned mode = 0; mode < sizeof(modes) / sizeof(modes[0]); mode += 1)
    {
        fesetround(modes[mode]);
        feclearexcept(FE_ALL_EXCEPT);
        const unsigned f = bits_f(strict_f(0x1.000002p0f, 0x1.fffffcp-1f, -1.0f));
        const int ff = fetestexcept(FE_ALL_EXCEPT);
        feclearexcept(FE_ALL_EXCEPT);
        const unsigned long d = bits_d(strict_d(0x1.0000000000001p0, 0x1.ffffffffffffep-1, -1.0));
        const int df = fetestexcept(FE_ALL_EXCEPT);
        fesetround(FE_TONEAREST);
        printf("round %u %08x %016lx %d %d\n", mode, f, d, ff, df);
    }
    return 0;
}
