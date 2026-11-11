// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's -Wimplicit-enum-enum-cast (#1447, the end of CheckImplicitConversion): an implicit
// conversion from one named enumeration to another, an enumerator taken as its enumeration, on by default under
// -Wenum-conversion. The shared warning harness compares the pinned Clang's complete diagnostics through both kernels and
// requested lanes.

static const char check_driver_enum_cast_contexts_source[] =
    "enum E { A, B }; enum F { C, D }; typedef enum E TE; enum { U1 }; typedef enum { T1 } Tn;\n"
                  "struct S { enum E e; enum F f : 2; };\n"
                  "enum E g(enum E);\n"
                  "enum E h(enum F f) {\n"
                  "    enum E e = C;\n"
                  "    enum E e2 = (enum F)0;\n"
                  "    enum F f2 = f;\n"
                  "    e = f;\n"
                  "    e = (f);\n"
                  "    e = 0 ? f : f;\n"
                  "    e = (e, f);\n"
                  "    TE t = f;\n"
                  "    struct S s = { f, e };\n"
                  "    s.e = f;\n"
                  "    g(f);\n"
                  "    e = U1;\n"
                  "    enum E e3 = (enum { V1 })0;\n"
                  "    Tn tn = f;\n"
                  "    e += f;\n"
                  "    e = (enum E)f;\n"
                  "    e = (int)f;\n"
                  "    _Bool bb = f;\n"
                  "    int i = f;\n"
                  "    (void)e2; (void)f2; (void)t; (void)e3; (void)tn; (void)bb; (void)i;\n"
                  "    if (0) e = f;\n"
                  "    return f;\n"
                  "}\n";

static const char check_driver_enum_cast_forms_source[] =
    "enum E { A, B }; enum F { C, D }; typedef enum E TE; typedef enum F TF; typedef enum { T1 } Tn;\n"
                  "enum E ge = C;\n"
                  "TE gt = D;\n"
                  "enum E garr[2] = { C, A };\n"
                  "struct S { enum E e; enum F f; } gs = { C, A };\n"
                  "void take(enum E, enum F);\n"
                  "TE back(enum F f) { return f; }\n"
                  "Tn anon(enum F f) { return f; }\n"
                  "int f(enum E e, TF g, int c) {\n"
                  "    e = c ? (c ? g : C) : D;\n"
                  "    e = (0, 1, g);\n"
                  "    take(g, e);\n"
                  "    take(C, A);\n"
                  "    struct S s = { g, e };\n"
                  "    struct S *p = &(struct S){ g, e };\n"
                  "    enum E arr[] = { g, C, e };\n"
                  "    TE t = g;\n"
                  "    t = C;\n"
                  "    e = (enum F)c;\n"
                  "    e = (enum E)g;\n"
                  "    e += g;\n"
                  "    c = g;\n"
                  "    (void)s; (void)p; (void)arr; (void)t;\n"
                  "#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic ignored \"-Wimplicit-enum-enum-cast\"\n"
                  "    e = g;\n"
                  "#pragma clang diagnostic pop\n"
                  "    if (0) e = g;\n"
                  "    return e = g;\n"
                  "}\n";

// The review's reproducers on #1827 (#1447). A unit whose enumerations are named only by typedefs (clang's name for
// linkage), and its a.k.a. spellings; a qualified typedef names none, an aligned one does; a typedef that spells a record's
// definition prints its keyword in an a.k.a.; the C23 underlying types' widths, where a constant that doesn't fit warns
// of its constant conversion alone and a wider variable of nothing; where each leaf reports (a _Generic's selected
// association, an inner comma's operator, a member's name, a postfix operator); and a bit-field's width.
static const char check_driver_enum_cast_typedef_source[] =
    "typedef enum { TA, TB } TAnon;\n"
    "typedef enum { UA, UB } UAnon;\n"
    "void f(void){ TAnon a; UAnon u; a = u; u = a; (void)a;(void)u; }\n"
    "void g(void){ TAnon a = UA; UAnon u = TB; (void)a; (void)u; }\n"
    "typedef enum { VA } VAnon, VAlias;\n"
    "VAlias h(UAnon x) { return x; }\n";

static const char check_driver_enum_cast_typedef_aliases_source[] =
    "typedef enum { VA } VAnon, VAlias;\n"
    "typedef VAnon VAlias2;\n"
    "typedef const enum { WA } WAnon, WAlias;\n"
    "typedef struct { int x; } SAnon, SAlias;\n"
    "enum G { GA };\n"
    "void f(enum G g) { VAlias a = g; VAlias2 b = g; VAnon c = g; WAlias w = g; (void)a; (void)b; (void)c; (void)w; }\n"
    "void h(SAlias s) { int *p = s; (void)p; }\n"
    "void k(void) { VAlias2 v = GA; enum G g = v; enum G g2 = VA; (void)g; (void)g2; }\n";

static const char check_driver_enum_cast_qualified_source[] =
    "typedef const struct { int x; } CS, CS2;\n"
    "typedef volatile enum { WA } WAnon;\n"
    "typedef enum { XA } __attribute__((aligned(8))) XAnon;\n"
    "enum G { GA };\n"
    "void h(CS s, CS2 t){ int *p = s; int *q = t; (void)p; (void)q; }\n"
    "void k(WAnon w, XAnon x){ enum G g = w; enum G g2 = x; enum G g3 = WA; enum G g4 = XA; int *p = w; (void)g; (void)g2; (void)g3; (void)g4; (void)p; }\n";

static const char check_driver_enum_cast_aka_source[] =
    "typedef struct { int x; } S, *SP;\n"
    "typedef S *SP2;\n"
    "typedef struct { int y; } *TP, T;\n"
    "void h(SP a, SP2 b, TP c){ int *p = a; int *q = b; int *r = c; (void)p; (void)q; (void)r; }\n";

static const char check_driver_enum_cast_widths_source[] =
    "enum Small : signed char { s0 }; enum Big : short { b0 = 30000, b1 = 5, b2 = -200 }; enum Bigger : int { i0 };\n"
    "enum USmall : unsigned char { u0 };\n"
    "void f1(enum Big x){ enum Small r = x; (void)r; }\n"
    "void f2(void){ enum Small r = b0; (void)r; }\n"
    "void f3(void){ enum Small r = b1; (void)r; }\n"
    "void f4(void){ enum Small r = b2; (void)r; }\n"
    "void f5(void){ enum USmall r = b2; (void)r; }\n"
    "void f6(void){ enum USmall r = b1; (void)r; }\n"
    "void f7(enum Small s){ enum USmall r = s; (void)r; }\n"
    "void f8(enum USmall s){ enum Small r = s; (void)r; }\n"
    "void f9(enum Small s){ enum Big r = s; (void)r; }\n"
    "enum E { A }; enum F { B = -1 }; enum G { C = 0x7fffffff, D = -1 };\n"
    "void g1(void){ enum E e = B; (void)e; }  void g2(enum F f){ enum E e = f; (void)e; }\n"
    "void g3(enum E x){ enum F e = x; (void)e; }  void g4(void){ enum E e = C; (void)e; }\n"
    "enum Big2 : short { b3 = 128, b4 = 255 }; enum UBig : unsigned short { ub = 128 };\n"
    "void h1(void){ enum Small r = b3; (void)r; }\n"
    "void h2(void){ enum Small r = ub; (void)r; }\n"
    "void h3(void){ enum USmall r = b4; (void)r; }\n"
    "struct BF { enum Big x : 4; enum Big y : 9; };\n"
    "void h4(struct BF *p){ enum Small r = p->x; enum Small q = p->y; (void)r; (void)q; }\n"
    "void h5(enum Big x, int c){ enum Small r = c ? b1 : x; (void)r; }\n";

static const char check_driver_enum_cast_locations_source[] =
    "enum E { A }; enum F { B };\n"
    "void f(enum E e, enum F g, int c) {\n"
    "    e = _Generic(c, int: g, default: B);\n"
    "    e = (_Generic(c, int: (g), default: B));\n"
    "    e = (e, g);\n"
    "    e = ((e, g));\n"
    "    e = (((e, g)));\n"
    "    e = ((0, (g)));\n"
    "    e = (c, (e, g));\n"
    "    e = (g);\n"
    "    e = ((g));\n"
    "}\n";

static const char check_driver_enum_cast_leaves_source[] =
    "enum E { A }; enum F { B };\n"
    "enum F hf(void);\n"
    "struct S { enum F f; enum F b : 3; } s, *ps, arr[2];\n"
    "void f(enum E e, enum F g, int c) {\n"
    "    enum F g2 = B;\n"
    "    e = (g2 = g);\n"
    "    e = (g2 += 1);\n"
    "    e = g++;\n"
    "    e = ++g;\n"
    "    e = (enum F)c;\n"
    "    e = hf();\n"
    "    e = ps->f;\n"
    "    e = (ps)->b;\n"
    "    e = arr[c].f;\n"
    "    e = (c, (g2 = g));\n"
    "    e = __builtin_choose_expr(1, g, e);\n"
    "    e = _Generic(c, int: (c, g), default: B);\n"
    "}\n";

// Enumeration definitions nested in an enumerator's value: an outer enumerator after an inner definition closes, and
// inner ones after both (check_common_enum_coerced's search, #1923).
static const char check_driver_enum_cast_nested_source[] =
    "enum O { OA = sizeof(enum I { IX, IY }), OB, OC = IY };\n"
    "enum P { PA };\n"
    "void f(enum P p) { p = OB; p = IX; p = OA; p = IY; p = OC; (void)p; }\n"
    "enum Q { QA = sizeof(enum R { RX }) + sizeof(enum S { SX }), QB };\n"
    "void g(enum P p) { p = QB; p = RX; p = SX; p = QA; (void)p; }\n";

// The order of conversions in a conditional's arms (#1895, clang's CheckConditionalOperand): each arm's own conversions
// before the arm's, the condition's first and the true arm's before the false one's, through nested conditionals, comma
// arms, a selection, a known condition's pruned arm, a _Bool target, unreachable code and a return.
static const char check_driver_enum_cast_arm_order_source[] =
    "enum E { A }; enum F { B }; enum G { C };\n"
    "void f(enum E e, enum F g, enum G h, int c, signed char sc, signed char t, _Bool b) {\n"
    "    e = c ? (g = h) : g;\n"
    "    e = c ? g : (g = h);\n"
    "    e = c ? (c ? (g = h) : g) : (g = h);\n"
    "    enum E e2 = c ? (g = h) : (enum F)(h = B);\n"
    "    sc = c ? (t = 300) : 400;\n"
    "    sc = c ? (c ? (t = 301) : 302) : (t = 303, 304);\n"
    "    sc = 1 ? 305 : (t = 306);\n"
    "    sc = 0 ? (t = 307) : 308;\n"
    "    e = c ? (g = h, (sc = 309, g)) : _Generic(c, int: (g = h));\n"
    "    b = c ? (t = 310) : (t = c ? 2 : 3);\n"
    "    if (0) { e = c ? (g = h) : g; sc = c ? (t = 311) : 312; }\n"
    "    (void)e2;\n"
    "}\n"
    "enum E r(int c, enum F g, enum G h, signed char t) { return c ? (g = h) : (t = 313, g); }\n";

// The same order for a conditional passed as an argument (review on #1967): a prototype's enumeration and constant
// conversions, two arguments, nested arms, a known condition's pruned arm, a _Bool parameter and unreachable code; and a
// _Generic's conditional arm, whose arms carry the conversion as a conditional's do.
static const char check_driver_enum_cast_argument_order_source[] =
    "enum E { A }; enum F { B }; enum G { C };\n"
    "void take(enum E);\n"
    "void take_char(signed char);\n"
    "void take2(enum E, signed char);\n"
    "void takeb(_Bool);\n"
    "void f(int c, enum F g, enum G h, enum E e, signed char t) {\n"
    "    take(c ? (g = h) : g);\n"
    "    take_char(c ? (t = 300) : 400);\n"
    "    take2(c ? g : (g = h), c ? (t = 301) : 302);\n"
    "    take(c ? (c ? (g = h) : g) : (g = h, g));\n"
    "    take_char(1 ? 303 : (t = 304));\n"
    "    takeb(c ? (t = 307) : (t = 308));\n"
    "    take2(c ? g : (c ? (g = h) : g), c ? (t = 309) : 310);\n"
    "    e = _Generic(c, int: c ? (g = h) : g);\n"
    "    take(_Generic(c, int: c ? (g = h) : g));\n"
    "    take(_Generic(c, int: c ? g : g));\n"
    "    if (0) take(c ? (g = h) : g);\n"
    "}\n";

// A leaf a macro's argument writes shows that argument's macro, unless the conversion's context (an assignment's operator,
// a declarator, a call, a return) is written in the same argument too (#1447, clang's rangesInsideSameMacroArgExpansion):
// nested arguments, a two-parameter macro's, a conditional macro's arms, a brace initializer's.
static const char check_driver_enum_cast_macro_arguments_source[] =
    "enum A { A1 }; enum B { B1 };\n"
                  "#define ID(v) v\n"
                  "#define TWO(x, y) x; y\n"
                  "void take(enum A);\n"
                  "enum A r(void) { return ID(B1); }\n"
                  "void f(enum A a) {\n"
                  "    ID(a = B1);\n"
                  "    ID(take(B1));\n"
                  "    take(ID(B1));\n"
                  "    TWO(a = B1, a = ID(B1));\n"
                  "    a = ID(ID(B1));\n"
                  "    a = ID((B1));\n"
                  "    a = (ID(B1));\n"
                  "    enum A arr[1] = { ID(B1) };\n"
                  "    ID(enum A c = B1);\n"
                  "    (void)c;\n"
                  "}\n";

static const char check_driver_enum_cast_macro_forms_source[] =
    "enum A { A1 }; enum B { B1, B2 };\n"
                  "#define ID(v) v\n"
                  "#define B_VAL B1\n"
                  "#define PICK(c, x, y) ((c) ? (x) : (y))\n"
                  "#define INIT(v) { v }\n"
                  "#define ASSIGN(l, r) l = r\n"
                  "void take(enum A);\n"
                  "enum A g(int c) {\n"
                  "    enum A a = ID(B_VAL);\n"
                  "    a = PICK(c, B1, A1);\n"
                  "    a = c ? ID(B1) : ID(B2);\n"
                  "    enum A arr[2] = INIT(B1);\n"
                  "    ASSIGN(a, B1);\n"
                  "    ASSIGN(a, ID(B1));\n"
                  "    ID(enum A arr2[1] = { B1 });\n"
                  "    take(PICK(c, B1, B2));\n"
                  "    (void)arr; (void)arr2;\n"
                  "    return ID(c) ? B1 : A1;\n"
                  "}\n";

// A selection that folds to an enumerator converts that enumerator's enumeration (#1447): an assignment's, a return's,
// an argument's, an initializer's and a conditional arm's, a nested selection's and one in parentheses; another
// enumeration's variable, an anonymous enumeration's enumerator and an integer constant give none.
static const char check_driver_enum_cast_selection_source[] =
    "enum A { A1, A2 };\n"
                  "enum B { B1, B2 };\n"
                  "enum { C1 };\n"
                  "enum B vb;\n"
                  "void take(enum A);\n"
                  "enum A ret(void) { return _Generic(0, int: B2); }\n"
                  "void f(enum A a, int c)\n"
                  "{\n"
                  "    a = _Generic(1, int: A1);\n"
                  "    a = _Generic(1, int: C1);\n"
                  "    a = _Generic(1, int: 5);\n"
                  "    a = _Generic(1, long: A1, default: B2);\n"
                  "    a = _Generic(1, int: _Generic(2, int: B1));\n"
                  "    a = _Generic(1, int: vb);\n"
                  "    a = c ? _Generic(1, int: B1) : A1;\n"
                  "    take(_Generic(1, int: B1));\n"
                  "    enum A x = _Generic(1, int: (B1));\n"
                  "    enum A y[2] = { _Generic(1, int: B1), A2 };\n"
                  "    a = (_Generic(1, int: B1));\n"
                  "    a = _Generic(1, int: B1, char: A1);\n"
                  "}\n";

// A unit that names neither an enumerator nor an enumeration's variable (#1447): its enumerations' values reach the
// conversions through members, calls' results and casts, which the gate (CheckInput.enum_names) opens for too.
static const char check_driver_enum_cast_unnamed_source[] =
    "enum A { A1 };\n"
                  "enum B { B1 };\n"
                  "struct S { enum A a; enum B b; };\n"
                  "enum B gb(void);\n"
                  "void sa(struct S *p, int i) { p->a = p->b; p->a = gb(); p->a = (enum B)i; }\n"
                  "struct S mk(struct S *p) { struct S s = { gb() }; return s; }\n"
                  "void take(enum A);\n"
                  "void call(struct S *p) { take(p->b); }\n";

static const CheckDriverDefaultWarningFixture check_driver_enum_cast_fixtures[] =
{
    {
        .name = "initializers, assignments, arms, commas, arguments and returns convert between enumerations",
        .source = check_driver_enum_cast_contexts_source,
        .warnings = 16,
    },
    {
        .name = "file-scope, brace, compound literal and unreachable conversions between enumerations, and pragmas",
        .source = check_driver_enum_cast_forms_source,
        .warnings = 28,
    },
    {
        .name = "-Wno-implicit-enum-enum-cast",
        .source = check_driver_enum_cast_forms_source,
        .options = { "-Wno-implicit-enum-enum-cast" },
        .warnings = 2,
    },
    {
        .name = "-Wenum-conversion holds -Wimplicit-enum-enum-cast",
        .source = check_driver_enum_cast_forms_source,
        .options = { "-Wenum-conversion" },
        .warnings = 28,
    },
    {
        .name = "-Werror=implicit-enum-enum-cast",
        .source = check_driver_enum_cast_forms_source,
        .options = { "-Werror=implicit-enum-enum-cast" },
        .errors = 20,
    },
    {
        .name = "enumerations named only by typedefs",
        .source = check_driver_enum_cast_typedef_source,
        .warnings = 5,
    },
    {
        .name = "typedef aliases of anonymous enumerations and their a.k.a.",
        .source = check_driver_enum_cast_typedef_aliases_source,
        .warnings = 6,
        .errors = 1,
    },
    {
        .name = "a qualified typedef names no anonymous tag, an aligned one does",
        .source = check_driver_enum_cast_qualified_source,
        .warnings = 2,
        .errors = 3,
    },
    {
        .name = "a typedef that spells an anonymous record's definition prints its keyword in an a.k.a.",
        .source = check_driver_enum_cast_aka_source,
        .errors = 3,
    },
    {
        .name = "C23 underlying types: constant and width ranges",
        .source = check_driver_enum_cast_widths_source,
        .warnings = 17,
        .c23 = true,
    },
    {
        .name = "where a selection's, a comma's and a parenthesized leaf report",
        .source = check_driver_enum_cast_locations_source,
        .warnings = 15,
    },
    {
        .name = "where assignments, increments, casts, calls, members and bit-fields report",
        .source = check_driver_enum_cast_leaves_source,
        .warnings = 14,
    },
    {
        .name = "enumerators of definitions nested in an enumerator's value",
        .source = check_driver_enum_cast_nested_source,
        .warnings = 9,
    },
    {
        .name = "conversions in a conditional's arms, in clang's order",
        .source = check_driver_enum_cast_arm_order_source,
        .warnings = 36,
    },
    {
        .name = "conversions in a conditional argument's arms, in clang's order",
        .source = check_driver_enum_cast_argument_order_source,
        .warnings = 35,
    },
    {
        .name = "macro arguments' notes",
        .source = check_driver_enum_cast_macro_arguments_source,
        .warnings = 11,
        .notes = 9,
    },
    {
        .name = "macro arguments' notes in conditionals, initializers and calls",
        .source = check_driver_enum_cast_macro_forms_source,
        .warnings = 11,
        .notes = 11,
    },
    {
        .name = "selections that fold to enumerators",
        .source = check_driver_enum_cast_selection_source,
        .warnings = 10,
    },
    {
        .name = "selections that fold to enumerators in C23",
        .source = check_driver_enum_cast_selection_source,
        .warnings = 10,
        .c23 = true,
    },
    {
        .name = "a unit's enumerations reached only through members, calls and casts",
        .source = check_driver_enum_cast_unnamed_source,
        .warnings = 5,
    },
};

// The underlying-type matrix (#1447, review on #1827): each ordered pair of C23 fixed underlying types converts a source
// enumeration's variable and its enumerators (1, its type's maximum and, for a signed type, its minimum) to a target
// enumeration. The warnings it expects follow clang's CheckImplicitConversion apart from the checker: a value whose likely
// range (a constant's significant or active bits, a variable's type's width) is no wider than the target's integer type
// warns of the enumerations, but a constant wider than it, or one whose non-negative value fills a signed target exactly
// from a signed type, warns of its constant conversion instead, and a wider variable of neither.
typedef struct CheckDriverEnumCastType CheckDriverEnumCastType;
struct CheckDriverEnumCastType
{
    const char* name;
    const char* maximum;
    const char* minimum;
    uint32_t width;
    bool is_signed;
};

static const CheckDriverEnumCastType check_driver_enum_cast_types[] =
{
    { "char", "127", "-128", 8, true },
    { "signed char", "127", "-128", 8, true },
    { "unsigned char", "255", NULL, 8, false },
    { "short", "32767", "-32768", 16, true },
    { "unsigned short", "65535", NULL, 16, false },
    { "int", "2147483647", "-2147483647 - 1", 32, true },
    { "unsigned int", "4294967295U", NULL, 32, false },
    { "long", "9223372036854775807L", "-9223372036854775807L - 1", 64, true },
    { "unsigned long", "18446744073709551615UL", NULL, 64, false },
};

// The warnings clang gives the matrix, by kind.
typedef struct CheckDriverEnumCastCounts CheckDriverEnumCastCounts;
struct CheckDriverEnumCastCounts
{
    uint32_t enums;
    uint32_t constants;
};

// One conversion to `target` of a likely range `width` bits wide, `negative` or not, a constant's or else a value's of type
// `source`, counted by the rule above.
static void check_driver_enum_cast_count(CheckDriverEnumCastCounts* counts, const CheckDriverEnumCastType* source, const CheckDriverEnumCastType* target,
                                         uint32_t width, bool negative, bool constant)
{
    const bool wider = width > target->width;
    const bool same_width = constant && width == target->width && target->is_signed && !negative && source->is_signed;
    counts->constants += constant && (wider || same_width) ? 1u : 0u;
    counts->enums += !wider && !same_width ? 1u : 0u;
}

// Writes the matrix's source to `buffer` and returns whether it fits; `counts` gets the warnings clang gives it.
static bool check_driver_enum_cast_matrix(char* buffer, uint64_t capacity, CheckDriverEnumCastCounts* counts)
{
    static const char* const forms[] = { "d = s; ", "d = S%u_one; ", "d = S%u_max; ", "d = S%u_min; " };
    uint64_t used = 0;
    bool fits = true;
    *counts = (CheckDriverEnumCastCounts){ 0 };
    for (uint32_t k = 0; fits && k < BUSTER_ARRAY_LENGTH(check_driver_enum_cast_types); k += 1)
    {
        const CheckDriverEnumCastType* type = check_driver_enum_cast_types + k;
        const int written = type->minimum != NULL ?
            snprintf(buffer + used, capacity - used, "enum S%u : %s { S%u_one = 1, S%u_max = %s, S%u_min = %s };\nenum D%u : %s { D%u_zero };\n", k,
                     type->name, k, k, type->maximum, k, type->minimum, k, type->name, k) :
            snprintf(buffer + used, capacity - used, "enum S%u : %s { S%u_one = 1, S%u_max = %s };\nenum D%u : %s { D%u_zero };\n", k, type->name, k, k,
                     type->maximum, k, type->name, k);
        fits = written > 0 && (uint64_t)written < capacity - used;
        used += fits ? (uint64_t)written : 0;
    }
    for (uint32_t pair = 0; fits && pair < BUSTER_ARRAY_LENGTH(check_driver_enum_cast_types) * BUSTER_ARRAY_LENGTH(check_driver_enum_cast_types); pair += 1)
    {
        const uint32_t i = pair / BUSTER_ARRAY_LENGTH(check_driver_enum_cast_types);
        const uint32_t j = pair % BUSTER_ARRAY_LENGTH(check_driver_enum_cast_types);
        const CheckDriverEnumCastType* source = check_driver_enum_cast_types + i;
        const CheckDriverEnumCastType* target = check_driver_enum_cast_types + j;
        const uint32_t form_count = source->minimum != NULL ? 4u : 3u;
        int written = snprintf(buffer + used, capacity - used, "void f%u_%u(enum S%u s) { enum D%u d; ", i, j, i, j);
        fits = written > 0 && (uint64_t)written < capacity - used;
        used += fits ? (uint64_t)written : 0;
        for (uint32_t form = 0; fits && form < form_count; form += 1)
        {
            written = snprintf(buffer + used, capacity - used, forms[form], i);
            fits = written > 0 && (uint64_t)written < capacity - used;
            used += fits ? (uint64_t)written : 0;
        }
        written = fits ? snprintf(buffer + used, capacity - used, "(void)d; }\n") : 0;
        fits = fits && written > 0 && (uint64_t)written < capacity - used;
        used += fits ? (uint64_t)written : 0;
        // The variable's range is its type's; 1 has one active bit; the maximum all of an unsigned type's bits and all but
        // a signed one's sign; the minimum all of them, as significant bits.
        check_driver_enum_cast_count(counts, source, target, source->width, false, false);
        check_driver_enum_cast_count(counts, source, target, 1, false, true);
        check_driver_enum_cast_count(counts, source, target, source->is_signed ? source->width - 1 : source->width, false, true);
        if (source->minimum != NULL)
        {
            check_driver_enum_cast_count(counts, source, target, source->width, true, true);
        }
    }
    return fits;
}

static void check_test_driver_enum_casts(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_enum_cast_fixtures,
                                      BUSTER_ARRAY_LENGTH(check_driver_enum_cast_fixtures));
    static char matrix[16384];
    CheckDriverEnumCastCounts counts = { 0 };
    const bool fits = check_driver_enum_cast_matrix(matrix, sizeof(matrix), &counts);
    check_test_expect(test, fits, "the enumerations' underlying-type matrix fits its buffer");
    if (fits)
    {
        const CheckDriverDefaultWarningFixture fixtures[] =
        {
            {
                .name = "the underlying-type matrix: enumeration and constant conversions",
                .source = matrix,
                .warnings = counts.enums + counts.constants,
                .c23 = true,
            },
            {
                .name = "the underlying-type matrix: -Wno-constant-conversion leaves the enumerations'",
                .source = matrix,
                .options = { "-Wno-constant-conversion" },
                .warnings = counts.enums,
                .c23 = true,
            },
            {
                .name = "the underlying-type matrix: -Wno-implicit-enum-enum-cast leaves the constants'",
                .source = matrix,
                .options = { "-Wno-implicit-enum-enum-cast" },
                .warnings = counts.constants,
                .c23 = true,
            },
        };
        check_test_driver_warning_fixtures(test, clang, executable, directory, fixtures, BUSTER_ARRAY_LENGTH(fixtures));
    }
}
