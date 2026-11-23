// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's floating precision warnings (#1875): -Wimplicit-float-conversion's "implicit conversion
// loses floating-point precision" (CheckImplicitConversion) and its compound assignment's "when assigning computation
// result" (AnalyzeCompoundAssignment), off by default, and -Wliteral-range's "floating-point comparison is always
// true/false" (CheckFloatComparison), on by default, with controls that must not warn. The shared warning harness compares
// the pinned clang's complete diagnostics through both readers and requested lanes, at -fsyntax-only and -c. (-Wconversion
// itself is refused until its other families are implemented, #1365; -Wimplicit-float-conversion's child
// -Wimplicit-int-float-conversion is not given, so no source converts an integer wider than a float's significand.)

// Conversions to a lower floating rank in every place clang checks one: initializers (scalar, brace, designated, nested,
// file scope), assignments, arguments (a prototype's, not a variadic or an unprototyped one's), returns, a conditional's
// arms and a comma's result, macros' arguments and bodies, typedefs and qualifiers, complex types, _Float16 and __bf16,
// compound assignments, constants that convert exactly or don't, and unreachable and short-circuited code.
static const char check_driver_float_conversions_source[] =
    "typedef float F;\n"
    "typedef double D;\n"
    "typedef const double CD;\n"
    "struct P { float x; F y; double z; };\n"
    "union U { float f; double d; };\n"
    "float take(float, ...);\n"
    "float old();\n"
    "void vf(F);\n"
    "static float ret(D d) { return d; }\n"
    "static long double ret_quad(__float128 q) { return q; }\n"
    "#define SET(a, b) ((a) = (b))\n"
    "#define WRAP(x) (x)\n"
    "static float gf = (float)1.0 + 0.1;\n"
    "static struct P gp = { 0.1, 0.25, 0.1L };\n"
    "static float garr[] = { [1] = 0.1, [0] = 1.0 };\n"
    "static struct P gnest[2] = { { 0.5, 0.1 }, { .z = 0.1L } };\n"
    "static double gq = 1.1Q;\n"
    "static _Complex float gz = 0.1 + 0.25i;\n"
    "float use(double d, long double l, F f, D dd, CD cd, volatile double vd, struct P* p, union U u, int i, _Float16 h, __bf16 b, _Complex double z)\n"
    "{\n"
    "    float a = d, c = l;\n"
    "    F e = dd;\n"
    "    float g = cd;\n"
    "    float k = vd;\n"
    "    _Complex float w = z;\n"
    "    p->x = d;\n"
    "    p->y = p->z;\n"
    "    u.f = u.d;\n"
    "    f = take(d, d);\n"
    "    f = old(d);\n"
    "    vf(d);\n"
    "    vf(WRAP(d));\n"
    "    SET(f, d);\n"
    "    f = WRAP(d);\n"
    "    f = ((d));\n"
    "    f = i ? d : l;\n"
    "    f = i ? (d, f) : 0.5;\n"
    "    f = (struct P){ d, 0.1, 0.1 }.x;\n"
    "    f = ({ d; });\n"
    "    f = __builtin_choose_expr(1, d, f);\n"
    "    f = _Generic(i, int: d);\n"
    "    f = p->z;\n"
    "    f = (&d)[0];\n"
    "    f = __builtin_fabs(d);\n"
    "    f = ++d;\n"
    "    f = d++;\n"
    "    f = (d = 0.5);\n"
    "    p->x += d;\n"
    "    p->y -= 1.0;\n"
    "    f *= cd;\n"
    "    f /= 0.5;\n"
    "    h += 1.0;\n"
    "    h *= b;\n"
    "    b /= h;\n"
    "    d -= l;\n"
    "    dd += l;\n"
    "    vd += l;\n"
    "    h = f;\n"
    "    b = h;\n"
    "    f = d > 0 ? 1.0 : 0.1f;\n"
    "    f = 1e39;\n"
    "    f = 1e-50;\n"
    "    f = 0x1p-150;\n"
    "    f = __builtin_nan(\"1\");\n"
    "    f = __builtin_nans(\"\");\n"
    "    f = 1.0 / 3.0;\n"
    "    h = 65520.0;\n"
    "    h = 0.1f16;\n"
    "    b = 1.0078125;\n"
    "    if (0)\n"
    "        f = d;\n"
    "    i = i || (f = d);\n"
    "    return a + c + e + g + k + ret(d) + (float)ret_quad(2.0Q) + __real__ w + f + gp.x + gnest[0].y + garr[0] + gf + (float)gq + __real__ gz;\n"
    "}\n";

// Floating comparisons with a literal: in an array bound, an enumerator, a static assertion, a bit-field width, sizeof,
// typeof, a case label, unreachable and unevaluated code, each operand order, casts implicit and explicit, every floating
// type, macros, and the forms that are no CastExpr or no FloatingLiteral (a sign, an unconverted literal, an ordering).
static const char check_driver_float_comparisons_source[] =
    "typedef float F;\n"
    "static int a1[(0.1 == 0.1f) + 1];\n"
    "enum E { X = 0.1 != 0.1f, Y = (int)(0.1L == (double)0.1), Z = 0.1 == (double)0.1f };\n"
    "_Static_assert(0.1 != 0.1f, \"\");\n"
    "struct S { unsigned b : (0.1 == 0.1f) + 1; };\n"
    "static int g = sizeof(0.1 == 0.1f);\n"
    "static int h = 0.1f == 0.1 ? 1 : 2;\n"
    "static __typeof__(0.1 == 0.1f) t;\n"
    "int e = 0.1L != 0.1;\n"
    "int k = 0.1q == 0.1;\n"
    "#define EQ(a, b) ((a) == (b))\n"
    "#define TENTH 0.1\n"
    "double g2(void);\n"
    "float g1(void);\n"
    "int f(int i, float x, F y, double d, long double l, __float128 q, _Float16 h16, __bf16 b16, float* p, struct S s)\n"
    "{\n"
    "    int r = a1[0] + g + h + t + e + k + (int)s.b;\n"
    "    switch (i) { case (0.1 == 0.1f): return 1; }\n"
    "    if (0) return x == 0.1;\n"
    "    r += (int)sizeof(char[(0.1 == 0.1f) + 1]) + (int)sizeof(x == 0.1) + (1 || x == 0.1) + _Generic(x == 0.1, int: 0);\n"
    "    r += x == 0.1;\n"
    "    r += 0.1 != x;\n"
    "    r += y == 0.1;\n"
    "    r += *p == 0.1;\n"
    "    r += p[1] != 1e-50;\n"
    "    r += (x) == (0.1);\n"
    "    r += (double)x == 0.1;\n"
    "    r += (double)(float)d == 0.1;\n"
    "    r += (float)d == 0.1;\n"
    "    r += g1() == 0.1;\n"
    "    r += d == 0.1L;\n"
    "    r += l == 0.1q;\n"
    "    r += q == 0.1;\n"
    "    r += 0.1 == h16;\n"
    "    r += 0.1 == b16;\n"
    "    r += h16 == 0.5;\n"
    "    r += x == 1e300;\n"
    "    r += x != 0x1p-150;\n"
    "    r += EQ(x, 0.1);\n"
    "    r += x == TENTH;\n"
    "    r += _Generic(0, int: 0.1) == x;\n"
    "    r += x == 0.5;\n"
    "    r += x == -0.1;\n"
    "    r += x == 0.1f;\n"
    "    r += x < 0.1;\n"
    "    r += i == 0.1;\n"
    "    r += 0.1 == 0.2;\n"
    "    r += d == 0.1;\n"
    "    r += g2() == 0.1;\n"
    "    r += (0.1, x) == 0.1;\n"
    "    return r;\n"
    "}\n";

// Nothing to warn of: widening and same-rank conversions, explicit casts, an _Atomic target, exact constants (const
// objects' too), and sizeof's operand.
static const char check_driver_float_controls_source[] =
    "typedef float F;\n"
    "struct P { float x; double y; };\n"
    "static const double half = 0.5;\n"
    "static const double tenth = 0.1;\n"
    "static const struct P exact = { 0.5f, 0.25 };\n"
    "float controls(double d, long double l, float f, _Float16 h, __bf16 b, int i, _Complex double z, struct P* p)\n"
    "{\n"
    "    const double local = 0.25;\n"
    "    _Atomic float af = d;\n"
    "    double w = f;\n"
    "    long double v = d;\n"
    "    float r = (float)d;\n"
    "    F e = (F)l;\n"
    "    float g = 0.5;\n"
    "    float k = -0.0;\n"
    "    float m = 0x1p-149;\n"
    "    float n = 3.4028234663852886e38;\n"
    "    float s = __builtin_inff();\n"
    "    float t = __builtin_inf();\n"
    "    float u = __builtin_nan(\"\");\n"
    "    float q = 0.5 + 0.25;\n"
    "    float c = half;\n"
    "    float c2 = local;\n"
    "    float c3 = exact.y;\n"
    "    _Float16 hh = 0.5f;\n"
    "    __bf16 bb = 1.0;\n"
    "    __fp16 fp = h;\n"
    "    _Complex float cz = 0.5 + 0.25i;\n"
    "    f = (float)d;\n"
    "    f = (short)i;\n"
    "    f += f;\n"
    "    f *= 2;\n"
    "    d += f;\n"
    "    h = (_Float16)f;\n"
    "    b = (__bf16)h;\n"
    "    f = sizeof(f = d);\n"
    "    f = (float)(d + l);\n"
    "    p->x = (float)p->y;\n"
    "    return f + (float)w + (float)v + r + e + g + k + m + n + s + t + u + q + c + c2 + c3 + hh + bb + fp + __real__ cz + af + (float)tenth + (float)__real__ z + b;\n"
    "}\n";

// Diagnostic pragmas map both warnings where they are reported, macros' expansions too.
static const char check_driver_float_pragmas_source[] =
    "#define SET(a, b) ((a) = (b))\n"
    "#define NARROW(x) ((float)0 + (x))\n"
    "#define LOSE(x) float lost = x\n"
    "float pragmas(double d, long double l, float f)\n"
    "{\n"
    "    f = d;\n"
    "#pragma clang diagnostic push\n"
    "#pragma clang diagnostic warning \"-Wimplicit-float-conversion\"\n"
    "    f = d;\n"
    "    SET(f, l);\n"
    "    f = NARROW(d);\n"
    "    LOSE(d);\n"
    "    f += lost;\n"
    "    f += d;\n"
    "#pragma clang diagnostic error \"-Wimplicit-float-conversion\"\n"
    "    d = l;\n"
    "#pragma clang diagnostic ignored \"-Wimplicit-float-conversion\"\n"
    "    d = l;\n"
    "#pragma clang diagnostic pop\n"
    "    d = l;\n"
    "#pragma clang diagnostic ignored \"-Wliteral-range\"\n"
    "    f = f == 0.1;\n"
    "#pragma clang diagnostic warning \"-Wliteral-range\"\n"
    "    f = f == 0.1;\n"
    "    return f + d;\n"
    "}\n";

// What -Weverything gives of these sources' kind where every other warning it enables is avoided (-Wdouble-promotion's
// implicit widening, -Wfloat-equal's comparisons, -Wmissing-prototypes' declarations).
static const char check_driver_float_everything_source[] =
    "typedef float F;\n"
    "static double take(F x);\n"
    "static double take(F x)\n"
    "{\n"
    "    return (double)x;\n"
    "}\n"
    "static int compare(double d, long double l, float f)\n"
    "{\n"
    "    int r = (double)f == 0.1;\n"
    "    r += 0.1L != (long double)d;\n"
    "    r += (double)f != 0.5;\n"
    "    r += (long double)(float)l == 0.1L;\n"
    "    return r;\n"
    "}\n"
    "static float narrow(double d, long double l)\n"
    "{\n"
    "    float f = (float)d;\n"
    "    double e = l;\n"
    "    f = d;\n"
    "    f -= (float)e;\n"
    "    e = (double)l;\n"
    "    e += l;\n"
    "    return f + (float)take(d);\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    float n = narrow(1.0, 2.0L);\n"
    "    return compare(1.0, 2.0L, 3.0f) + (n > 0.0f);\n"
    "}\n";

// Review cases (#3361): -Wliteral-range's _Atomic operands, read through their value type, in the graph and in the
// constant reader; compound assignments' computation types, with the right operand after its own promotion and the
// left one unpromoted (__fp16's against an integer as float), printed as converted (an __fp16 one with an __bf16 one is
// left out: clang 23.1.4's backend can't lower it at -c); and commas whose right operand is an
// assignment or a value computed from one, which clang's evaluator doesn't fold, beside ones it does; and an _Atomic
// typedef's right operand, spelled by its value type's sugar.
static const char check_driver_float_review_source[] =
    "_Atomic float af;\n"
    "_Atomic double ad;\n"
    "__fp16 gh;\n"
    "int atomic_compare(void)\n"
    "{\n"
    "    _Atomic float *p = &af;\n"
    "    return (af == 0.1) + ((double)af == 0.1) + (0.1 != af) + (af != 0.1f) + (*p == 0.1);\n"
    "}\n"
    "enum { ATOMIC_SIZE = sizeof(af == 0.1) };\n"
    "float compound(float f, _Float16 g, __bf16 b)\n"
    "{\n"
    "    gh += 1;\n"
    "    f += ad;\n"
    "    gh *= g;\n"
    "    gh /= gh;\n"
    "    g += gh;\n"
    "    b *= gh;\n"
    "    return f + g + b;\n"
    "}\n"
    "float commas(double d, int i, float f)\n"
    "{\n"
    "    f = (i, (i, d = 0.5));\n"
    "    f = i ? (0, d = 0.5) : 0.5;\n"
    "    f = (i, 0.5);\n"
    "    f = (0, (i, 0.25));\n"
    "    f = -(d = 0.5);\n"
    "    f = (i, -(d = 0.5));\n"
    "    f = (double)(d = 0.5);\n"
    "    f = (d = 0.5) * 1.0;\n"
    "    f = -(i, 0.5);\n"
    "    f = (d = 1, 0.5);\n"
    "    return f;\n"
    "}\n"
    "typedef double D;\n"
    "_Atomic D atomic_d;\n"
    "float compound_sugar(float f)\n"
    "{\n"
    "    f += atomic_d;\n"
    "    return f;\n"
    "}\n";

// `#pragma clang fp eval_method(extended)` converts a double or float literal to long double, so only a long double one
// is still a literal to CheckFloatComparison: in the constant reader and in a body.
static const char check_driver_float_eval_extended_source[] =
    "#pragma clang fp eval_method(extended)\n"
    "float x;\n"
    "enum E { X = 0.1 != 0.1f, Y = 0.5 == 0.1f, Z = sizeof(x == 0.1L) };\n"
    "int f(void) { return (x == 0.1) + (x != 0.1L); }\n";

// `#pragma clang fp eval_method(double)` widens a compound assignment's right operand, not its left one.
static const char check_driver_float_eval_double_source[] =
    "#pragma clang fp eval_method(double)\n"
    "float f(float a, float b, __fp16 *h, double d)\n"
    "{\n"
    "    a += b;\n"
    "    a *= 2.0f;\n"
    "    *h += a;\n"
    "    d += a;\n"
    "    return a + d;\n"
    "}\n";

// One of each, for promotion to errors below the error limit.
static const char check_driver_float_small_source[] =
    "float small(double d, float f)\n"
    "{\n"
    "    f = d;\n"
    "    f += d;\n"
    "    return f == 0.1;\n"
    "}\n";

// Narrowing at builtin parameter edges, including nested calls and a group of controls.
// Suppress fabsf's independent -Wabsolute-value diagnostic so these cases isolate the argument conversions.
static const char check_driver_builtin_float_arguments_source[] =
    "typedef double D;\n"
    "float copy(D x, long double y) { return __builtin_copysignf(x, y); }\n"
    "float absolute(double x) { return __builtin_fabsf(x); }\n"
    "float fused(double x, long double y) { return __builtin_fmaf(x, 1.0f, y); }\n"
    "float nested(long double x) { return __builtin_copysignf(__builtin_ceil(x), __builtin_floor(x)); }\n"
    "float choose(int c, double x, double y) { return __builtin_sqrtf(c ? x : y); }\n"
    "float dead(double x) { if (0) return __builtin_sqrtf(x); return __builtin_ceilf(0.1); }\n"
    "float exchange(_Atomic float *p, double x) { return __c11_atomic_exchange(p, x, 5); }\n"
    "float exact(double x) { return __builtin_copysignf((float)x, 1.0); }\n"
    "int query(long double x) { return __builtin_isfinite(x) + (int)sizeof(__builtin_floorf(x)); }\n"
    "float component(_Complex double x) { return __builtin_crealf(x); }\n";

static const CheckDriverDefaultWarningFixture check_driver_float_precision_fixtures[] =
{
    {
        .name = "builtin floating parameter conversions",
        .source = check_driver_builtin_float_arguments_source,
        .options = { "-Wno-absolute-value", "-Wimplicit-float-conversion" },
        .warnings = 15,
        .compare_node_window = true,
    },
    {
        .name = "builtin floating parameter conversions with the warning disabled",
        .source = check_driver_builtin_float_arguments_source,
        .options = { "-Wno-absolute-value", "-Wimplicit-float-conversion", "-Wno-implicit-float-conversion" },
        .compare_node_window = true,
    },
    {
        .name = "floating conversions to a lower rank, by default",
        .source = check_driver_float_conversions_source,
        .warnings = 2,
    },
    {
        .name = "floating conversions to a lower rank under -Wimplicit-float-conversion",
        .source = check_driver_float_conversions_source,
        .options = { "-Wimplicit-float-conversion" },
        .warnings = 60,
        .notes = 3,
    },
    {
        .name = "floating conversions to a lower rank under -Wall -Wimplicit-float-conversion",
        .source = check_driver_float_conversions_source,
        .options = { "-Wall", "-Wimplicit-float-conversion" },
        .warnings = 60,
        .notes = 3,
    },
    {
        .name = "floating conversions to a lower rank, -Wimplicit-float-conversion then -Wno-implicit-float-conversion",
        .source = check_driver_float_conversions_source,
        .options = { "-Wimplicit-float-conversion", "-Wno-implicit-float-conversion" },
        .warnings = 2,
    },
    {
        .name = "floating conversions to a lower rank under -Wconversion -Wno-conversion",
        .source = check_driver_float_small_source,
        .options = { "-Wconversion", "-Wno-conversion" },
        .warnings = 1,
    },
    {
        .name = "-Werror=implicit-float-conversion",
        .source = check_driver_float_small_source,
        .options = { "-Werror=implicit-float-conversion" },
        .warnings = 1,
        .errors = 2,
    },
    {
        .name = "-Werror=literal-range with -Wimplicit-float-conversion",
        .source = check_driver_float_small_source,
        .options = { "-Werror=literal-range", "-Wimplicit-float-conversion" },
        .warnings = 2,
        .errors = 1,
    },
    {
        .name = "floating comparisons with a literal, by default",
        .source = check_driver_float_comparisons_source,
        .warnings = 39,
        .notes = 1,
    },
    {
        .name = "floating comparisons with a literal under -Wall",
        .source = check_driver_float_comparisons_source,
        .options = { "-Wall" },
        .warnings = 39,
        .notes = 1,
    },
    {
        .name = "floating comparisons with a literal in C23",
        .source = check_driver_float_comparisons_source,
        .warnings = 39,
        .notes = 1,
        .c23 = true,
    },
    {
        .name = "floating comparisons with a literal under -Wno-literal-range",
        .source = check_driver_float_comparisons_source,
        .options = { "-Wno-literal-range" },
        .warnings = 2,
    },
    {
        .name = "floating controls, by default",
        .source = check_driver_float_controls_source,
        .warnings = 1,
    },
    {
        .name = "floating controls under -Wall -Wimplicit-float-conversion",
        .source = check_driver_float_controls_source,
        .options = { "-Wall", "-Wimplicit-float-conversion" },
        .warnings = 1,
    },
    {
        .name = "floating precision pragmas, by default",
        .source = check_driver_float_pragmas_source,
        .warnings = 6,
        .errors = 1,
        .notes = 3,
    },
    {
        .name = "floating precision pragmas under -Wimplicit-float-conversion",
        .source = check_driver_float_pragmas_source,
        .options = { "-Wimplicit-float-conversion" },
        .warnings = 9,
        .errors = 1,
        .notes = 3,
    },
    {
        .name = "floating precision and comparisons under -Weverything",
        .source = check_driver_float_everything_source,
        .options = { "-Weverything" },
        .warnings = 7,
    },
    {
        .name = "floating review cases, by default",
        .source = check_driver_float_review_source,
        .warnings = 13,
    },
    {
        .name = "floating review cases under -Wimplicit-float-conversion",
        .source = check_driver_float_review_source,
        .options = { "-Wimplicit-float-conversion" },
        .warnings = 25,
    },
    {
        .name = "floating comparisons under eval_method(extended)",
        .source = check_driver_float_eval_extended_source,
        .warnings = 2,
    },
    {
        .name = "floating compound assignments under eval_method(double) and -Wimplicit-float-conversion",
        .source = check_driver_float_eval_double_source,
        .options = { "-Wimplicit-float-conversion" },
        .warnings = 4,
    },
};

static void check_test_driver_float_precision(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_float_precision_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_float_precision_fixtures));
}
