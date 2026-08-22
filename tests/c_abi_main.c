// The handwritten half of the Zig side of the test/c_abi suite: everything in
// ziglang/zig test/c_abi/main.zig outside the machine-regular middle that
// tools/port_zig_c_abi.py translates into c_abi_main_generated.c. Test order,
// values, and the zig_ symbol names match upstream so the pair links against
// c_abi_cfuncs.c and stays diffable; see tests/c_abi.h for the pairing and
// gate contract. Failure faults the process after recording the failing test
// name in c_abi_current_test (and writing it to stderr where write() exists),
// so the harness only ever checks for a clean exit.
#include "c_abi.h"

const char* c_abi_current_test = "startup";

#if !defined(_WIN32)
long write(int, const void*, unsigned long);
#endif

static unsigned long c_abi_string_length(const char* text)
{
    unsigned long length = 0;
    while (text[length])
    {
        length += 1;
    }
    return length;
}

void zig_panic(void)
{
#if !defined(_WIN32)
    write(2, "c_abi failure in test: ", 23);
    write(2, c_abi_current_test, c_abi_string_length(c_abi_current_test));
    write(2, "\n", 1);
#endif
    *(volatile int*)0 = 1;
    for (;;)
    {
    }
}

static void assert_or_panic(bool ok)
{
    if (!ok)
    {
        zig_panic();
    }
}

void run_c_tests(void);

// --- integers ---------------------------------------------------------------

struct u128
{
    unsigned __int128 value;
};
struct i128
{
    __int128 value;
};

void c_u8(uint8_t);
void c_u16(uint16_t);
void c_u32(uint32_t);
void c_u64(uint64_t);
void c_struct_u128(struct u128);
void c_i8(int8_t);
void c_i16(int16_t);
void c_i32(int32_t);
void c_i64(int64_t);
void c_struct_i128(struct i128);
void c_five_integers(int32_t, int32_t, int32_t, int32_t, int32_t);

void zig_five_integers(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e)
{
    assert_or_panic(a == 12);
    assert_or_panic(b == 34);
    assert_or_panic(c == 56);
    assert_or_panic(d == 78);
    assert_or_panic(e == 90);
}

static void test_integers(void)
{
    c_abi_current_test = "integers";
    c_u8(0xff);
    c_u16(0xfffe);
    c_u32(0xfffffffd);
    c_u64(0xfffffffffffffffcULL);
#ifndef ZIG_NO_I128
    {
        struct u128 s = {0xfffffffffffffffcULL};
        c_struct_u128(s);
    }
#endif
    c_i8(-1);
    c_i16(-2);
    c_i32(-3);
    c_i64(-4);
#ifndef ZIG_NO_I128
    {
        struct i128 s = {-6};
        c_struct_i128(s);
    }
#endif
    c_five_integers(12, 34, 56, 78, 90);
}

void zig_u8(uint8_t x)
{
    assert_or_panic(x == 0xff);
}
void zig_u16(uint16_t x)
{
    assert_or_panic(x == 0xfffe);
}
void zig_u32(uint32_t x)
{
    assert_or_panic(x == 0xfffffffd);
}
void zig_u64(uint64_t x)
{
    assert_or_panic(x == 0xfffffffffffffffcULL);
}
void zig_i8(int8_t x)
{
    assert_or_panic(x == -1);
}
void zig_i16(int16_t x)
{
    assert_or_panic(x == -2);
}
void zig_i32(int32_t x)
{
    assert_or_panic(x == -3);
}
void zig_i64(int64_t x)
{
    assert_or_panic(x == -4);
}
void zig_struct_i128(struct i128 a)
{
    assert_or_panic(a.value == -6);
}
void zig_struct_u128(struct u128 a)
{
    assert_or_panic(a.value == 0xfffffffffffffffcULL);
}

// --- floats, pointer, bool --------------------------------------------------

void c_five_floats(float, float, float, float, float);

void zig_five_floats(float a, float b, float c, float d, float e)
{
    assert_or_panic(a == 1.0f);
    assert_or_panic(b == 2.0f);
    assert_or_panic(c == 3.0f);
    assert_or_panic(d == 4.0f);
    assert_or_panic(e == 5.0f);
}

static void test_floats(void)
{
    c_abi_current_test = "floats";
    c_five_floats(1.0f, 2.0f, 3.0f, 4.0f, 5.0f);
}

void c_ptr(void*);

static void test_pointer(void)
{
    c_abi_current_test = "pointer";
    c_ptr((void*)0xdeadbeef);
}

void zig_ptr(void* x)
{
    assert_or_panic((uintptr_t)x == 0xdeadbeef);
}

void c_bool(bool);

static void test_bool(void)
{
    c_abi_current_test = "bool";
    c_bool(true);
}

void zig_bool(bool x)
{
    assert_or_panic(x);
}

// --- complex ----------------------------------------------------------------
// Upstream models the _Complex types as two-field extern structs on the Zig
// side; the C side answers with real float/double complex, which the ABI must
// treat identically. The struct halves compile everywhere; the tests only run
// where the C side's _Complex half exists.

typedef struct
{
    float real;
    float imag;
} ComplexFloat;
typedef struct
{
    double real;
    double imag;
} ComplexDouble;

ComplexFloat zig_cmultf(ComplexFloat a, ComplexFloat b)
{
    assert_or_panic(a.real == 1.25f);
    assert_or_panic(a.imag == 2.6f);
    assert_or_panic(b.real == 11.3f);
    assert_or_panic(b.imag == -1.5f);
    return (ComplexFloat){.real = 1.5f, .imag = 13.5f};
}

ComplexDouble zig_cmultd(ComplexDouble a, ComplexDouble b)
{
    assert_or_panic(a.real == 1.25);
    assert_or_panic(a.imag == 2.6);
    assert_or_panic(b.real == 11.3);
    assert_or_panic(b.imag == -1.5);
    return (ComplexDouble){.real = 1.5, .imag = 13.5};
}

ComplexFloat zig_cmultf_comp(float a_r, float a_i, float b_r, float b_i)
{
    assert_or_panic(a_r == 1.25f);
    assert_or_panic(a_i == 2.6f);
    assert_or_panic(b_r == 11.3f);
    assert_or_panic(b_i == -1.5f);
    return (ComplexFloat){.real = 1.5f, .imag = 13.5f};
}

ComplexDouble zig_cmultd_comp(double a_r, double a_i, double b_r, double b_i)
{
    assert_or_panic(a_r == 1.25);
    assert_or_panic(a_i == 2.6);
    assert_or_panic(b_r == 11.3);
    assert_or_panic(b_i == -1.5);
    return (ComplexDouble){.real = 1.5, .imag = 13.5};
}

#ifndef ZIG_NO_COMPLEX
ComplexFloat c_cmultf_comp(float a_r, float a_i, float b_r, float b_i);
ComplexDouble c_cmultd_comp(double a_r, double a_i, double b_r, double b_i);
ComplexFloat c_cmultf(ComplexFloat a, ComplexFloat b);
ComplexDouble c_cmultd(ComplexDouble a, ComplexDouble b);

static void test_complex_float(void)
{
    c_abi_current_test = "complex float";
    ComplexFloat a = {.real = 1.25f, .imag = 2.6f};
    ComplexFloat b = {.real = 11.3f, .imag = -1.5f};
    ComplexFloat z = c_cmultf(a, b);
    assert_or_panic(z.real == 1.5f);
    assert_or_panic(z.imag == 13.5f);
}

static void test_complex_float_by_component(void)
{
    c_abi_current_test = "complex float by component";
    ComplexFloat a = {.real = 1.25f, .imag = 2.6f};
    ComplexFloat b = {.real = 11.3f, .imag = -1.5f};
    ComplexFloat z = c_cmultf_comp(a.real, a.imag, b.real, b.imag);
    assert_or_panic(z.real == 1.5f);
    assert_or_panic(z.imag == 13.5f);
}

static void test_complex_double(void)
{
    c_abi_current_test = "complex double";
    ComplexDouble a = {.real = 1.25, .imag = 2.6};
    ComplexDouble b = {.real = 11.3, .imag = -1.5};
    ComplexDouble z = c_cmultd(a, b);
    assert_or_panic(z.real == 1.5);
    assert_or_panic(z.imag == 13.5);
}

static void test_complex_double_by_component(void)
{
    c_abi_current_test = "complex double by component";
    ComplexDouble a = {.real = 1.25, .imag = 2.6};
    ComplexDouble b = {.real = 11.3, .imag = -1.5};
    ComplexDouble z = c_cmultd_comp(a.real, a.imag, b.real, b.imag);
    assert_or_panic(z.real == 1.5);
    assert_or_panic(z.imag == 13.5);
}
#endif // ZIG_NO_COMPLEX

// --- f32/f64/long double argument-position series ---------------------------

float zig_ret_f32(void)
{
    return 1;
}
void zig_f32(float f, size_t i)
{
    assert_or_panic(f == 2);
    assert_or_panic(i == 1);
}
void zig_1_f32(size_t a0, float f, size_t i)
{
    assert_or_panic(f == 3);
    assert_or_panic(i == 2);
}
void zig_2_f32(size_t a0, size_t a1, float f, size_t i)
{
    assert_or_panic(f == 4);
    assert_or_panic(i == 3);
}
void zig_3_f32(size_t a0, size_t a1, size_t a2, float f, size_t i)
{
    assert_or_panic(f == 5);
    assert_or_panic(i == 4);
}
void zig_4_f32(size_t a0, size_t a1, size_t a2, size_t a3, float f, size_t i)
{
    assert_or_panic(f == 6);
    assert_or_panic(i == 5);
}
void zig_5_f32(size_t a0, size_t a1, size_t a2, size_t a3, size_t a4, float f, size_t i)
{
    assert_or_panic(f == 7);
    assert_or_panic(i == 6);
}
void zig_6_f32(size_t a0, size_t a1, size_t a2, size_t a3, size_t a4, size_t a5, float f, size_t i)
{
    assert_or_panic(f == 8);
    assert_or_panic(i == 7);
}
void zig_7_f32(size_t a0, size_t a1, size_t a2, size_t a3, size_t a4, size_t a5, size_t a6, float f, size_t i)
{
    assert_or_panic(f == 9);
    assert_or_panic(i == 8);
}
void zig_8_f32(size_t a0, size_t a1, size_t a2, size_t a3, size_t a4, size_t a5, size_t a6, size_t a7, float f, size_t i)
{
    assert_or_panic(f == 10);
    assert_or_panic(i == 9);
}

float c_ret_f32(void);
void c_f32(float, size_t);
void c_1_f32(size_t, float, size_t);
void c_2_f32(size_t, size_t, float, size_t);
void c_3_f32(size_t, size_t, size_t, float, size_t);
void c_4_f32(size_t, size_t, size_t, size_t, float, size_t);
void c_5_f32(size_t, size_t, size_t, size_t, size_t, float, size_t);
void c_6_f32(size_t, size_t, size_t, size_t, size_t, size_t, float, size_t);
void c_7_f32(size_t, size_t, size_t, size_t, size_t, size_t, size_t, float, size_t);
void c_8_f32(size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, float, size_t);
void c_test_f32(void);

static void test_f32(void)
{
    c_abi_current_test = "f32";
    float f = c_ret_f32();
    assert_or_panic(f == 11);
    c_f32(12, 1);
    c_1_f32(0, 13, 2);
    c_2_f32(0, 1, 14, 3);
    c_3_f32(0, 1, 2, 15, 4);
    c_4_f32(0, 1, 2, 3, 16, 5);
    c_5_f32(0, 1, 2, 3, 4, 17, 6);
    c_6_f32(0, 1, 2, 3, 4, 5, 18, 7);
    c_7_f32(0, 1, 2, 3, 4, 5, 6, 19, 8);
    c_8_f32(0, 1, 2, 3, 4, 5, 6, 7, 20, 9);
    c_test_f32();
}

double zig_ret_f64(void)
{
    return 1;
}
void zig_f64(double f, size_t i)
{
    assert_or_panic(f == 2);
    assert_or_panic(i == 1);
}
void zig_1_f64(size_t a0, double f, size_t i)
{
    assert_or_panic(f == 3);
    assert_or_panic(i == 2);
}
void zig_2_f64(size_t a0, size_t a1, double f, size_t i)
{
    assert_or_panic(f == 4);
    assert_or_panic(i == 3);
}
void zig_3_f64(size_t a0, size_t a1, size_t a2, double f, size_t i)
{
    assert_or_panic(f == 5);
    assert_or_panic(i == 4);
}
void zig_4_f64(size_t a0, size_t a1, size_t a2, size_t a3, double f, size_t i)
{
    assert_or_panic(f == 6);
    assert_or_panic(i == 5);
}
void zig_5_f64(size_t a0, size_t a1, size_t a2, size_t a3, size_t a4, double f, size_t i)
{
    assert_or_panic(f == 7);
    assert_or_panic(i == 6);
}
void zig_6_f64(size_t a0, size_t a1, size_t a2, size_t a3, size_t a4, size_t a5, double f, size_t i)
{
    assert_or_panic(f == 8);
    assert_or_panic(i == 7);
}
void zig_7_f64(size_t a0, size_t a1, size_t a2, size_t a3, size_t a4, size_t a5, size_t a6, double f, size_t i)
{
    assert_or_panic(f == 9);
    assert_or_panic(i == 8);
}
void zig_8_f64(size_t a0, size_t a1, size_t a2, size_t a3, size_t a4, size_t a5, size_t a6, size_t a7, double f, size_t i)
{
    assert_or_panic(f == 10);
    assert_or_panic(i == 9);
}

double c_ret_f64(void);
void c_f64(double, size_t);
void c_1_f64(size_t, double, size_t);
void c_2_f64(size_t, size_t, double, size_t);
void c_3_f64(size_t, size_t, size_t, double, size_t);
void c_4_f64(size_t, size_t, size_t, size_t, double, size_t);
void c_5_f64(size_t, size_t, size_t, size_t, size_t, double, size_t);
void c_6_f64(size_t, size_t, size_t, size_t, size_t, size_t, double, size_t);
void c_7_f64(size_t, size_t, size_t, size_t, size_t, size_t, size_t, double, size_t);
void c_8_f64(size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, double, size_t);
void c_test_f64(void);

static void test_f64(void)
{
    c_abi_current_test = "f64";
    double f = c_ret_f64();
    assert_or_panic(f == 11);
    c_f64(12, 1);
    c_1_f64(0, 13, 2);
    c_2_f64(0, 1, 14, 3);
    c_3_f64(0, 1, 2, 15, 4);
    c_4_f64(0, 1, 2, 3, 16, 5);
    c_5_f64(0, 1, 2, 3, 4, 17, 6);
    c_6_f64(0, 1, 2, 3, 4, 5, 18, 7);
    c_7_f64(0, 1, 2, 3, 4, 5, 6, 19, 8);
    c_8_f64(0, 1, 2, 3, 4, 5, 6, 7, 20, 9);
    c_test_f64();
}

#ifndef ZIG_NO_LONG_DOUBLE
long double zig_ret_longdouble(void)
{
    return 1;
}
void zig_longdouble(long double f, size_t i)
{
    assert_or_panic(f == 2);
    assert_or_panic(i == 1);
}
void zig_1_longdouble(size_t a0, long double f, size_t i)
{
    assert_or_panic(f == 3);
    assert_or_panic(i == 2);
}
void zig_2_longdouble(size_t a0, size_t a1, long double f, size_t i)
{
    assert_or_panic(f == 4);
    assert_or_panic(i == 3);
}
void zig_3_longdouble(size_t a0, size_t a1, size_t a2, long double f, size_t i)
{
    assert_or_panic(f == 5);
    assert_or_panic(i == 4);
}
void zig_4_longdouble(size_t a0, size_t a1, size_t a2, size_t a3, long double f, size_t i)
{
    assert_or_panic(f == 6);
    assert_or_panic(i == 5);
}
void zig_5_longdouble(size_t a0, size_t a1, size_t a2, size_t a3, size_t a4, long double f, size_t i)
{
    assert_or_panic(f == 7);
    assert_or_panic(i == 6);
}
void zig_6_longdouble(size_t a0, size_t a1, size_t a2, size_t a3, size_t a4, size_t a5, long double f, size_t i)
{
    assert_or_panic(f == 8);
    assert_or_panic(i == 7);
}
void zig_7_longdouble(size_t a0, size_t a1, size_t a2, size_t a3, size_t a4, size_t a5, size_t a6, long double f, size_t i)
{
    assert_or_panic(f == 9);
    assert_or_panic(i == 8);
}
void zig_8_longdouble(size_t a0, size_t a1, size_t a2, size_t a3, size_t a4, size_t a5, size_t a6, size_t a7, long double f, size_t i)
{
    assert_or_panic(f == 10);
    assert_or_panic(i == 9);
}

long double c_ret_longdouble(void);
void c_longdouble(long double, size_t);
void c_1_longdouble(size_t, long double, size_t);
void c_2_longdouble(size_t, size_t, long double, size_t);
void c_3_longdouble(size_t, size_t, size_t, long double, size_t);
void c_4_longdouble(size_t, size_t, size_t, size_t, long double, size_t);
void c_5_longdouble(size_t, size_t, size_t, size_t, size_t, long double, size_t);
void c_6_longdouble(size_t, size_t, size_t, size_t, size_t, size_t, long double, size_t);
void c_7_longdouble(size_t, size_t, size_t, size_t, size_t, size_t, size_t, long double, size_t);
void c_8_longdouble(size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, long double, size_t);
void c_test_longdouble(void);

static void test_long_double(void)
{
    c_abi_current_test = "long double";
    long double f = c_ret_longdouble();
    assert_or_panic(f == 11);
    c_longdouble(12, 1);
    c_1_longdouble(0, 13, 2);
    c_2_longdouble(0, 1, 14, 3);
    c_3_longdouble(0, 1, 2, 15, 4);
    c_4_longdouble(0, 1, 2, 3, 16, 5);
    c_5_longdouble(0, 1, 2, 3, 4, 17, 6);
    c_6_longdouble(0, 1, 2, 3, 4, 5, 18, 7);
    c_7_longdouble(0, 1, 2, 3, 4, 5, 6, 19, 8);
    c_8_longdouble(0, 1, 2, 3, 4, 5, 6, 7, 20, 9);
    c_test_longdouble();
}
#endif // ZIG_NO_LONG_DOUBLE

// --- named struct and union shapes ------------------------------------------

typedef struct
{
    int32_t a;
    int32_t b;
} Struct_i32_i32;
Struct_i32_i32 c_mut_struct_i32_i32(Struct_i32_i32);
void c_struct_i32_i32(Struct_i32_i32);

static void test_struct_i32_i32(void)
{
    c_abi_current_test = "struct i32 i32";
    Struct_i32_i32 s = {.a = 1, .b = 2};
    Struct_i32_i32 mut_res = c_mut_struct_i32_i32(s);
    assert_or_panic(s.a == 1);
    assert_or_panic(s.b == 2);
    assert_or_panic(mut_res.a == 101);
    assert_or_panic(mut_res.b == 252);
    c_struct_i32_i32(s);
}

void zig_struct_i32_i32(Struct_i32_i32 s)
{
    assert_or_panic(s.a == 1);
    assert_or_panic(s.b == 2);
}

typedef struct
{
    uint64_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
    uint8_t e;
} BigStruct;
void c_big_struct(BigStruct);

static void test_big_struct(void)
{
    c_abi_current_test = "big struct";
    BigStruct s = {.a = 1, .b = 2, .c = 3, .d = 4, .e = 5};
    c_big_struct(s);
}

void zig_big_struct(BigStruct x)
{
    assert_or_panic(x.a == 1);
    assert_or_panic(x.b == 2);
    assert_or_panic(x.c == 3);
    assert_or_panic(x.d == 4);
    assert_or_panic(x.e == 5);
}

typedef union
{
    BigStruct a;
} BigUnion;
void c_big_union(BigUnion);

static void test_big_union(void)
{
    c_abi_current_test = "big union";
    BigUnion x = {.a = {.a = 1, .b = 2, .c = 3, .d = 4, .e = 5}};
    c_big_union(x);
}

void zig_big_union(BigUnion x)
{
    assert_or_panic(x.a.a == 1);
    assert_or_panic(x.a.b == 2);
    assert_or_panic(x.a.c == 3);
    assert_or_panic(x.a.d == 4);
    assert_or_panic(x.a.e == 5);
}

typedef struct
{
    uint32_t a;
    float b;
    float c;
    uint32_t d;
} MedStructMixed;
void c_med_struct_mixed(MedStructMixed);
MedStructMixed c_ret_med_struct_mixed(void);

static void test_med_struct_mixed(void)
{
    c_abi_current_test = "medium struct of ints and floats";
    MedStructMixed s = {.a = 1234, .b = 100.0f, .c = 1337.0f};
    c_med_struct_mixed(s);
    MedStructMixed s2 = c_ret_med_struct_mixed();
    assert_or_panic(s2.a == 1234);
    assert_or_panic(s2.b == 100.0f);
    assert_or_panic(s2.c == 1337.0f);
}

void zig_med_struct_mixed(MedStructMixed x)
{
    assert_or_panic(x.a == 1234);
    assert_or_panic(x.b == 100.0f);
    assert_or_panic(x.c == 1337.0f);
}

MedStructMixed zig_ret_med_struct_mixed(void)
{
    return (MedStructMixed){.a = 1234, .b = 100.0f, .c = 1337.0f};
}

// Zig packed structs travel as their backing integer.

void c_small_packed_struct(uint8_t);
uint8_t c_ret_small_packed_struct(void);

void zig_small_packed_struct(uint8_t x)
{
    assert_or_panic(((x >> 0) & 0x3) == 0);
    assert_or_panic(((x >> 2) & 0x3) == 1);
    assert_or_panic(((x >> 4) & 0x3) == 2);
    assert_or_panic(((x >> 6) & 0x3) == 3);
}

static void test_small_packed_struct(void)
{
    c_abi_current_test = "small packed struct";
    uint8_t s = (uint8_t)((0 << 0) | (1 << 2) | (2 << 4) | (3 << 6));
    c_small_packed_struct(s);
    uint8_t s2 = c_ret_small_packed_struct();
    assert_or_panic(((s2 >> 0) & 0x3) == 0);
    assert_or_panic(((s2 >> 2) & 0x3) == 1);
    assert_or_panic(((s2 >> 4) & 0x3) == 2);
    assert_or_panic(((s2 >> 6) & 0x3) == 3);
}

#ifndef ZIG_NO_I128
void c_big_packed_struct(__int128);
__int128 c_ret_big_packed_struct(void);

void zig_big_packed_struct(__int128 x)
{
    assert_or_panic(((x >> 0) & 0xFFFFFFFFFFFFFFFF) == 1);
    assert_or_panic(((x >> 64) & 0xFFFFFFFFFFFFFFFF) == 2);
}

static void test_big_packed_struct(void)
{
    c_abi_current_test = "big packed struct";
    __int128 s = 0;
    s |= 1 << 0;
    s |= (__int128)2 << 64;
    c_big_packed_struct(s);
    __int128 s2 = c_ret_big_packed_struct();
    assert_or_panic(((s2 >> 0) & 0xFFFFFFFFFFFFFFFF) == 1);
    assert_or_panic(((s2 >> 64) & 0xFFFFFFFFFFFFFFFF) == 2);
}
#endif // ZIG_NO_I128

typedef struct
{
    uint64_t a;
    uint8_t b;
    uint32_t c;
} SplitStructInt;
void c_split_struct_ints(SplitStructInt);

static void test_split_struct_ints(void)
{
    c_abi_current_test = "split struct of ints";
    SplitStructInt s = {.a = 1234, .b = 100, .c = 1337};
    c_split_struct_ints(s);
}

void zig_split_struct_ints(SplitStructInt x)
{
    assert_or_panic(x.a == 1234);
    assert_or_panic(x.b == 100);
    assert_or_panic(x.c == 1337);
}

typedef struct
{
    uint64_t a;
    uint8_t b;
    float c;
} SplitStructMixed;
void c_split_struct_mixed(SplitStructMixed);
SplitStructMixed c_ret_split_struct_mixed(void);

static void test_split_struct_mixed(void)
{
    c_abi_current_test = "split struct of ints and floats";
    SplitStructMixed s = {.a = 1234, .b = 100, .c = 1337.0f};
    c_split_struct_mixed(s);
    SplitStructMixed s2 = c_ret_split_struct_mixed();
    assert_or_panic(s2.a == 1234);
    assert_or_panic(s2.b == 100);
    assert_or_panic(s2.c == 1337.0f);
}

void zig_split_struct_mixed(SplitStructMixed x)
{
    assert_or_panic(x.a == 1234);
    assert_or_panic(x.b == 100);
    assert_or_panic(x.c == 1337.0f);
}

SplitStructMixed zig_ret_split_struct_mixed(void)
{
    return (SplitStructMixed){.a = 1234, .b = 100, .c = 1337.0f};
}

BigStruct c_big_struct_both(BigStruct);

static void test_sret_and_byval_together(void)
{
    c_abi_current_test = "sret and byval together";
    BigStruct s = {.a = 1, .b = 2, .c = 3, .d = 4, .e = 5};
    BigStruct y = c_big_struct_both(s);
    assert_or_panic(y.a == 10);
    assert_or_panic(y.b == 11);
    assert_or_panic(y.c == 12);
    assert_or_panic(y.d == 13);
    assert_or_panic(y.e == 14);
}

BigStruct zig_big_struct_both(BigStruct x)
{
    assert_or_panic(x.a == 30);
    assert_or_panic(x.b == 31);
    assert_or_panic(x.c == 32);
    assert_or_panic(x.d == 33);
    assert_or_panic(x.e == 34);
    return (BigStruct){.a = 20, .b = 21, .c = 22, .d = 23, .e = 24};
}

// --- integer return types ---------------------------------------------------

bool zig_ret_bool(void)
{
    return true;
}
uint8_t zig_ret_u8(void)
{
    return 0xff;
}
uint16_t zig_ret_u16(void)
{
    return 0xffff;
}
uint32_t zig_ret_u32(void)
{
    return 0xffffffff;
}
uint64_t zig_ret_u64(void)
{
    return 0xffffffffffffffffULL;
}
int8_t zig_ret_i8(void)
{
    return -1;
}
int16_t zig_ret_i16(void)
{
    return -1;
}
int32_t zig_ret_i32(void)
{
    return -1;
}
int64_t zig_ret_i64(void)
{
    return -1;
}

bool c_ret_bool(void);
uint8_t c_ret_u8(void);
uint16_t c_ret_u16(void);
uint32_t c_ret_u32(void);
uint64_t c_ret_u64(void);
int8_t c_ret_i8(void);
int16_t c_ret_i16(void);
int32_t c_ret_i32(void);
int64_t c_ret_i64(void);

static void test_integer_return_types(void)
{
    c_abi_current_test = "integer return types";
    assert_or_panic(c_ret_bool() == true);
    assert_or_panic(c_ret_u8() == 0xff);
    assert_or_panic(c_ret_u16() == 0xffff);
    assert_or_panic(c_ret_u32() == 0xffffffff);
    assert_or_panic(c_ret_u64() == 0xffffffffffffffffULL);
    assert_or_panic(c_ret_i8() == -1);
    assert_or_panic(c_ret_i16() == -1);
    assert_or_panic(c_ret_i32() == -1);
    assert_or_panic(c_ret_i64() == -1);
}

// --- array-bearing structs --------------------------------------------------

typedef struct
{
    int32_t a;
    uint8_t padding[4];
    int64_t b;
} StructWithArray;
void c_struct_with_array(StructWithArray);
StructWithArray c_ret_struct_with_array(void);

static void test_struct_with_array_as_padding(void)
{
    c_abi_current_test = "Struct with array as padding.";
    c_struct_with_array((StructWithArray){.a = 1, .b = 2});
    StructWithArray x = c_ret_struct_with_array();
    assert_or_panic(x.a == 4);
    assert_or_panic(x.b == 155);
}

typedef struct
{
    struct
    {
        double x;
        double y;
    } origin;
    struct
    {
        double width;
        double height;
    } size;
} FloatArrayStruct;
void c_float_array_struct(FloatArrayStruct);
FloatArrayStruct c_ret_float_array_struct(void);

static void test_float_array_like_struct(void)
{
    c_abi_current_test = "Float array like struct";
    c_float_array_struct((FloatArrayStruct){
        .origin = {.x = 5, .y = 6},
        .size = {.width = 7, .height = 8},
    });
    FloatArrayStruct x = c_ret_float_array_struct();
    assert_or_panic(x.origin.x == 1);
    assert_or_panic(x.origin.y == 2);
    assert_or_panic(x.size.width == 3);
    assert_or_panic(x.size.height == 4);
}

// --- DC / CFF / PD field-position structs -----------------------------------
// The c_assert/zig_assert pairs return the 1-based index of the first
// mismatching field, zero for agreement.

typedef struct
{
    double v1;
    uint8_t v2;
} DC;
int c_assert_DC(DC);
int c_assert_ret_DC(void);
int c_send_DC(void);
DC c_ret_DC(void);

int zig_assert_DC(DC lv)
{
    int err = 0;
    if (lv.v1 != -0.25)
    {
        err = 1;
    }
    if (lv.v2 != 15)
    {
        err = 2;
    }
    return err;
}
DC zig_ret_DC(void)
{
    return (DC){.v1 = -0.25, .v2 = 15};
}

static void test_dc_zig_passes_to_c(void)
{
    c_abi_current_test = "DC: Zig passes to C";
    assert_or_panic(c_assert_DC((DC){.v1 = -0.25, .v2 = 15}) == 0);
}
static void test_dc_zig_returns_to_c(void)
{
    c_abi_current_test = "DC: Zig returns to C";
    assert_or_panic(c_assert_ret_DC() == 0);
}
static void test_dc_c_passes_to_zig(void)
{
    c_abi_current_test = "DC: C passes to Zig";
    assert_or_panic(c_send_DC() == 0);
}
static void test_dc_c_returns_to_zig(void)
{
    c_abi_current_test = "DC: C returns to Zig";
    DC actual = c_ret_DC();
    assert_or_panic(actual.v1 == -0.25);
    assert_or_panic(actual.v2 == 15);
}

typedef struct
{
    uint8_t v1;
    float v2;
    float v3;
} CFF;
int c_assert_CFF(CFF);
int c_assert_ret_CFF(void);
int c_send_CFF(void);
CFF c_ret_CFF(void);

int zig_assert_CFF(CFF lv)
{
    int err = 0;
    if (lv.v1 != 39)
    {
        err = 1;
    }
    if (lv.v2 != 0.875f)
    {
        err = 2;
    }
    if (lv.v3 != 1.0f)
    {
        err = 3;
    }
    return err;
}
CFF zig_ret_CFF(void)
{
    return (CFF){.v1 = 39, .v2 = 0.875f, .v3 = 1.0f};
}

static void test_cff_zig_passes_to_c(void)
{
    c_abi_current_test = "CFF: Zig passes to C";
    assert_or_panic(c_assert_CFF((CFF){.v1 = 39, .v2 = 0.875f, .v3 = 1.0f}) == 0);
}
static void test_cff_zig_returns_to_c(void)
{
    c_abi_current_test = "CFF: Zig returns to C";
    assert_or_panic(c_assert_ret_CFF() == 0);
}
static void test_cff_c_passes_to_zig(void)
{
    c_abi_current_test = "CFF: C passes to Zig";
    assert_or_panic(c_send_CFF() == 0);
}
static void test_cff_c_returns_to_zig(void)
{
    c_abi_current_test = "CFF: C returns to Zig";
    CFF actual = c_ret_CFF();
    assert_or_panic(actual.v1 == 39);
    assert_or_panic(actual.v2 == 0.875f);
    assert_or_panic(actual.v3 == 1.0f);
}

typedef struct
{
    void* v1;
    double v2;
} PD;
int c_assert_PD(PD);
int c_assert_ret_PD(void);
int c_send_PD(void);
PD c_ret_PD(void);

int zig_assert_PD(PD lv)
{
    int err = 0;
    if (lv.v1 != 0)
    {
        err = 1;
    }
    if (lv.v2 != 0.5)
    {
        err = 2;
    }
    return err;
}
PD zig_ret_PD(void)
{
    return (PD){.v1 = 0, .v2 = 0.5};
}

static void test_pd_zig_passes_to_c(void)
{
    c_abi_current_test = "PD: Zig passes to C";
    assert_or_panic(c_assert_PD((PD){.v1 = 0, .v2 = 0.5}) == 0);
}
static void test_pd_zig_returns_to_c(void)
{
    c_abi_current_test = "PD: Zig returns to C";
    assert_or_panic(c_assert_ret_PD() == 0);
}
static void test_pd_c_passes_to_zig(void)
{
    c_abi_current_test = "PD: C passes to Zig";
    assert_or_panic(c_send_PD() == 0);
}
static void test_pd_c_returns_to_zig(void)
{
    c_abi_current_test = "PD: C returns to Zig";
    PD actual = c_ret_PD();
    assert_or_panic(actual.v1 == 0);
    assert_or_panic(actual.v2 == 0.5);
}

// --- by-ref modification, byval through a function pointer ------------------

typedef struct
{
    int val;
    int arr[15];
} ByRef;
ByRef c_modify_by_ref_param(ByRef);

static void test_modify_by_ref_param(void)
{
    c_abi_current_test = "C function modifies by ref param";
    ByRef res = c_modify_by_ref_param((ByRef){.val = 1});
    assert_or_panic(res.val == 42);
}

typedef struct
{
    struct
    {
        unsigned long x;
        unsigned long y;
        unsigned long z;
    } origin;
    struct
    {
        unsigned long width;
        unsigned long height;
        unsigned long depth;
    } size;
} ByVal;
void c_func_ptr_byval(void*, void*, ByVal, unsigned long, void*, unsigned long);

static void test_func_ptr_byval(void)
{
    c_abi_current_test = "C function that takes byval struct called via function pointer";
    // A volatile pointer keeps the call indirect, as upstream's fn_ptr does.
    void (*volatile fn_ptr)(void*, void*, ByVal, unsigned long, void*, unsigned long) = c_func_ptr_byval;
    fn_ptr((void*)1, (void*)2,
           (ByVal){
               .origin = {.x = 9, .y = 10, .z = 11},
               .size = {.width = 12, .height = 13, .depth = 14},
           },
           3, (void*)4, 5);
}

// --- f16 / f80 / f128 -------------------------------------------------------

#ifndef ZIG_NO_F16
// Upstream runs the bare-f16 exchange only where the ABI is defined for it
// (its ZIG_NO_RAW_F16); of this port's targets that is aarch64 alone — on
// x86-64 clang itself rejects __fp16 parameters and returns.
#if defined(__aarch64__)
__fp16 c_f16(__fp16);

static void test_f16_bare(void)
{
    c_abi_current_test = "f16 bare";
    __fp16 a = c_f16(12);
    assert_or_panic(a == 34);
}
#endif

typedef struct
{
    __fp16 a;
} f16_struct;
f16_struct c_f16_struct(f16_struct);

static void test_f16_struct(void)
{
    c_abi_current_test = "f16 struct";
    f16_struct a = c_f16_struct((f16_struct){12});
    assert_or_panic(a.a == 34);
}
#endif // ZIG_NO_F16

#ifndef ZIG_NO_F80
#if defined(__x86_64__) && !defined(_MSC_VER)
typedef long double f80;
f80 c_f80(f80);
typedef struct
{
    f80 a;
} f80_struct;
f80_struct c_f80_struct(f80_struct);
typedef struct
{
    f80 a;
    int b;
} f80_extra_struct;
f80_extra_struct c_f80_extra_struct(f80_extra_struct);

static void test_f80_bare(void)
{
    c_abi_current_test = "f80 bare";
    f80 a = c_f80(12.34L);
    assert_or_panic((double)a == 56.78);
}
static void test_f80_struct(void)
{
    c_abi_current_test = "f80 struct";
    f80_struct a = c_f80_struct((f80_struct){12.34L});
    assert_or_panic((double)a.a == 56.78);
}
static void test_f80_extra_struct(void)
{
    c_abi_current_test = "f80 extra struct";
    f80_extra_struct a = c_f80_extra_struct((f80_extra_struct){12.34L, 42});
    assert_or_panic((double)a.a == 56.78);
    assert_or_panic(a.b == 24);
}
#endif
#endif // ZIG_NO_F80

#ifndef ZIG_NO_F128
__float128 zig_f128(__float128 x)
{
    assert_or_panic(x == 12);
    return 34;
}
__float128 c_f128(__float128);

static void test_f128_bare(void)
{
    c_abi_current_test = "f128 bare";
    __float128 a = c_f128(12.34);
    assert_or_panic((double)a == 56.78);
}

typedef struct
{
    __float128 a;
} f128_struct;
f128_struct zig_f128_struct(f128_struct a)
{
    assert_or_panic(a.a == 12345);
    return (f128_struct){98765};
}
f128_struct c_f128_struct(f128_struct);

typedef struct
{
    __float128 a;
    __float128 b;
} f128_f128_struct;
f128_f128_struct zig_f128_f128_struct(f128_f128_struct a)
{
    assert_or_panic(a.a == 13);
    assert_or_panic(a.b == 57);
    return (f128_f128_struct){24, 68};
}
f128_f128_struct c_f128_f128_struct(f128_f128_struct);

static void test_f128_struct(void)
{
    c_abi_current_test = "f128 struct";
    f128_struct a = c_f128_struct((f128_struct){12.34});
    assert_or_panic((double)a.a == 56.78);
    f128_f128_struct b = c_f128_f128_struct((f128_f128_struct){12.34, 87.65});
    assert_or_panic((double)b.a == 56.78);
    assert_or_panic((double)b.b == 43.21);
}
static void test_f128_f128_struct(void)
{
    c_abi_current_test = "f128 f128 struct";
    f128_struct a = c_f128_struct((f128_struct){12.34});
    assert_or_panic((double)a.a == 56.78);
    f128_f128_struct b = c_f128_f128_struct((f128_f128_struct){12.34, 87.65});
    assert_or_panic((double)b.a == 56.78);
    assert_or_panic((double)b.b == 43.21);
}
#endif // ZIG_NO_F128

// --- calling-convention specials --------------------------------------------
// The stdcall attribute is a no-op on every target this port builds for, so
// these three run as plain C ABI shapes, exactly as upstream does off x86-32.

void stdcall_scalars(char, short, int, float, double);

static void test_stdcall_scalars(void)
{
    c_abi_current_test = "Stdcall ABI scalars";
    stdcall_scalars(1, 2, 3, 4.0f, 5.0);
}

typedef struct
{
    int16_t x;
    int16_t y;
} Coord2;
Coord2 stdcall_coord2(Coord2, Coord2, Coord2);

static void test_stdcall_structs(void)
{
    c_abi_current_test = "Stdcall ABI structs";
    Coord2 res = stdcall_coord2(
        (Coord2){.x = 0x1111, .y = 0x2222},
        (Coord2){.x = 0x3333, .y = 0x4444},
        (Coord2){.x = 0x5555, .y = 0x6666});
    assert_or_panic(res.x == 123);
    assert_or_panic(res.y == 456);
}

void stdcall_big_union(BigUnion);

static void test_stdcall_big_union(void)
{
    c_abi_current_test = "Stdcall ABI big union";
    BigUnion x = {.a = {.a = 1, .b = 2, .c = 3, .d = 4, .e = 5}};
    stdcall_big_union(x);
}

#if !defined(ZIG_NO_CC_ATTRIBUTES) && defined(__x86_64__)
ByRef __attribute__((ms_abi)) c_explict_win64(ByRef);
ByRef __attribute__((sysv_abi)) c_explict_sys_v(ByRef);

static void test_explicit_win64(void)
{
    c_abi_current_test = "explicit Win64 calling convention";
    ByRef res = c_explict_win64((ByRef){.val = 1});
    assert_or_panic(res.val == 42);
}
static void test_explicit_sys_v(void)
{
    c_abi_current_test = "explicit SysV calling convention";
    ByRef res = c_explict_sys_v((ByRef){.val = 1});
    assert_or_panic(res.val == 42);
}
#endif

typedef struct
{
    double x;
    double y;
} byval_tail_callsite_attr_Point;
typedef struct
{
    double width;
    double height;
} byval_tail_callsite_attr_Size;
typedef struct
{
    byval_tail_callsite_attr_Point origin;
    byval_tail_callsite_attr_Size size;
} byval_tail_callsite_attr_Rect;
double c_byval_tail_callsite_attr(byval_tail_callsite_attr_Rect);

static void test_byval_tail_callsite_attr(void)
{
    c_abi_current_test = "byval tail callsite attribute";
    byval_tail_callsite_attr_Rect v = {
        .origin = {.x = 1, .y = 2},
        .size = {.width = 3, .height = 4},
    };
    assert_or_panic(c_byval_tail_callsite_attr(v) == 3.0);
}

#if defined(__x86_64__) && !defined(_WIN32)
// Deliberately narrower than the C side's (unsigned, int, unsigned, int)
// definition: the exchange only works when the caller extends narrow
// arguments the way the de-facto SysV convention expects.
void c_x86_64_sysv_uint_int_uint_int(uint8_t, int8_t, uint16_t, int16_t);

static void test_x86_64_sysv_args(void)
{
    c_abi_current_test = "x86_64 sysv args";
    c_x86_64_sysv_uint_int_uint_int(1, -2, 3, -4);
}
#endif

#if defined(__x86_64__) && defined(_WIN32)
// Upstream calls these as untyped Zig varargs with zero-size structs threaded
// between the values; C has neither, so the port declares one named double
// followed by promoted varargs — the same Win64 slot layout. The u64
// parameters on the C side then read the raw f64 bit patterns.
void c_win64_varargs_u64_f64_u64_f64(double, ...);
void c_win64_varargs_f64_u64_f64_u64(double, ...);

static void test_win64_varargs(void)
{
    c_abi_current_test = "win64 varargs";
    c_win64_varargs_u64_f64_u64_f64(1.0f, 2.0f, 3.0, 4.0);
    c_win64_varargs_f64_u64_f64_u64(5.0f, 6.0f, 7.0, 8.0);
}
#endif

#if !defined(ZIG_NO_CC_ATTRIBUTES) && (defined(__x86_64__) || defined(__aarch64__))
int __attribute__((preserve_none)) zig_preserve_none(int x)
{
    return x + 1;
}
int __attribute__((preserve_none)) c_preserve_none(int);
void c_preserve_none_check(void);

static void test_preserve_none(void)
{
    c_abi_current_test = "preserve_none calling convention";
    assert_or_panic(c_preserve_none(41) == 42);
    c_preserve_none_check();
}
#endif

// --- driver -----------------------------------------------------------------

int main(void)
{
    c_abi_current_test = "run_c_tests";
    run_c_tests();
    test_integers();
    test_floats();
    test_pointer();
    test_bool();
#ifndef ZIG_NO_COMPLEX
    test_complex_float();
    test_complex_float_by_component();
    test_complex_double();
    test_complex_double_by_component();
#endif
    test_f32();
    test_f64();
#ifndef ZIG_NO_LONG_DOUBLE
    test_long_double();
#endif
    c_abi_run_generated_tests();
    test_struct_i32_i32();
    test_big_struct();
    test_big_union();
    test_med_struct_mixed();
    test_small_packed_struct();
#ifndef ZIG_NO_I128
    test_big_packed_struct();
#endif
    test_split_struct_ints();
    test_split_struct_mixed();
    test_sret_and_byval_together();
    test_integer_return_types();
    test_struct_with_array_as_padding();
    test_float_array_like_struct();
    test_dc_zig_passes_to_c();
    test_dc_zig_returns_to_c();
    test_dc_c_passes_to_zig();
    test_dc_c_returns_to_zig();
    test_cff_zig_passes_to_c();
    test_cff_zig_returns_to_c();
    test_cff_c_passes_to_zig();
    test_cff_c_returns_to_zig();
    test_pd_zig_passes_to_c();
    test_pd_zig_returns_to_c();
    test_pd_c_passes_to_zig();
    test_pd_c_returns_to_zig();
    test_modify_by_ref_param();
    test_func_ptr_byval();
#ifndef ZIG_NO_F16
#if defined(__aarch64__)
    test_f16_bare();
#endif
    test_f16_struct();
#endif
#ifndef ZIG_NO_F80
#if defined(__x86_64__) && !defined(_MSC_VER)
    test_f80_bare();
    test_f80_struct();
    test_f80_extra_struct();
#endif
#endif
#ifndef ZIG_NO_F128
    test_f128_bare();
    test_f128_struct();
    test_f128_f128_struct();
#endif
    test_stdcall_scalars();
    test_stdcall_structs();
    test_stdcall_big_union();
#if !defined(ZIG_NO_CC_ATTRIBUTES) && defined(__x86_64__)
    test_explicit_win64();
    test_explicit_sys_v();
#endif
    test_byval_tail_callsite_attr();
#if defined(__x86_64__) && !defined(_WIN32)
    test_x86_64_sysv_args();
#endif
#if defined(__x86_64__) && defined(_WIN32)
    test_win64_varargs();
#endif
#if !defined(ZIG_NO_CC_ATTRIBUTES) && (defined(__x86_64__) || defined(__aarch64__))
    test_preserve_none();
#endif
    return 0;
}
