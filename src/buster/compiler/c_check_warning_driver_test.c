// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original default-warning sources. Clang supplies the external diagnostic oracle, including source policy.
// Include after the checker driver's spawn, count and file-comparison helpers. Initializer-only families have
// separate fixtures. These cases compare complete no-caret diagnostics through both kernels and lane requests.

typedef struct CheckDriverDefaultWarningFixture CheckDriverDefaultWarningFixture;
struct CheckDriverDefaultWarningFixture
{
    const char* name;
    const char* source;
    const char* options[4];
    uint32_t warnings;
    uint32_t errors;
    uint32_t notes;
    bool c23;
    bool compare_warning_text;
    bool syntax_only;
};

static const char check_driver_format_plain_source[] =
    "typedef struct _IO_FILE FILE;\n"
    "int printf(const char*,...);\nint fprintf(FILE*,const char*,...);\n"
    "int sprintf(char*,const char*,...);\nint snprintf(char*,unsigned long,const char*,...);\n"
    "void f(FILE*p,char*b){printf(\"%d\",\"str\");fprintf(p,\"%d\",\"str\");"
    "sprintf(b,\"%d\",\"str\");snprintf(b,16,\"%d\",\"str\");snprintf(b,16,\"%s.mo\\0\",\"x\");}\n";

// The first attribute survives a bare redeclaration; the second is attached after a bare declaration.
// A non-library name with a different format index keeps its source contract under the same options.
static const char check_driver_format_attributed_source[] =
    "int printf(const char*,...)__attribute__((format(printf,1,2)));\nint printf(const char*,...);\n"
    "int snprintf(char*,unsigned long,const char*,...);\n"
    "int snprintf(char*,unsigned long,const char*,...)__attribute__((format(printf,3,4)));\n"
    "int trace(int,const char*,...)__attribute__((format(printf,2,3)));\n"
    "void f(char*b){printf(\"%d\",\"str\");snprintf(b,16,\"%d\",\"str\");trace(0,\"%d\",\"str\");}\n";

static const char check_driver_gnu_designator_source[] =
    "struct P { int a, b; };\n"
    "struct P p = { b: 1, a: 2 };\n"
    "int g[4] = { [1 ... 2] = 3 };\n"
    "int h[4] = { [1 ... 2] 3, [3] 4 };\n"
    "struct P q[2] = { [1] { 1, 2 }, [0].a = 3, [0] .b = 4 };\n"
    "int n[] = { [4] 1 };\n"
    "int f(void)\n"
    "{\n"
    "    int e[3] = { [0] 2, 1, [2] 5.5 };\n"
    "    struct P r = { a: 1.5 };\n"
    "#pragma clang diagnostic push\n"
    "#pragma clang diagnostic ignored \"-Wgnu-designator\"\n"
    "    int s[2] = { [1] 1 };\n"
    "#pragma clang diagnostic pop\n"
    "    return e[0] + r.a + s[1];\n"
    "}\n";

static const CheckDriverDefaultWarningFixture check_driver_default_warning_fixtures[] =
{
    {
        // A failed static assertion's requirement and its operands' values (#1447, check_semantic_assert): the first
        // false term of a conjunction, literals printed as clang's StmtPrinter prints them (`2147483648U`, `'\x00'`,
        // `2.`, `1.0E+10`), `sizeof (x)`, `__alignof`, and the "expression evaluates to" note where an operand isn't
        // obvious, a plain char's value as `'<0xC8>' (0xC8, -56)`, floats at APFloat's default precision.
        .name = "static assertion requirements and their values",
        .source = "enum { A = 3 };\n"
                  "_Static_assert(A == 4, \"a\");\n"
                  "_Static_assert(1 && A > 2 && (A < 3 || A > 5), \"b\");\n"
                  "_Static_assert(-A == 4 && 1, \"c\");\n"
                  "_Static_assert('a' == A, \"d\");\n"
                  "_Static_assert(1.5 + A < 2.0, \"e\");\n"
                  "_Static_assert((_Bool)A == 0, \"f\");\n"
                  "_Static_assert(A + 0u > 5u, \"g\");\n"
                  "_Static_assert((char)A == 'b', \"h\");\n"
                  "_Static_assert((char)200 == (char)'\\n', \"i\");\n"
                  "_Static_assert(A < 1 == 1, \"j\");\n"
                  "_Static_assert(A - 3, \"k\");\n"
                  "_Static_assert(0x80000000 == A + 3000000000, \"l\");\n"
                  "_Static_assert(1e300 * A == 0.1f, \"m\");\n"
                  "_Static_assert(1e-5 * A == 1e5, \"n\");\n"
                  "_Static_assert(sizeof A == sizeof (A) + sizeof(int), \"o\");\n"
                  "_Static_assert(__alignof__(A) == 2 * _Alignof(int), \"p\");\n",
        .errors = 16,
        .notes = 14,
    },
    {
        // (Apart, as clang stops at 20 errors.)
        .name = "static assertion requirements and their values, literals and conjunctions",
        .source = "enum { A = 3 };\n"
                  "_Static_assert('\\0' == 1 && 0, \"q\");\n"
                  "_Static_assert((0), \"r\");\n"
                  "_Static_assert(1 && (0), \"s\");\n"
                  "_Static_assert(0 && 2 == 3, \"t\");\n"
                  "_Static_assert(L'\\x263a' + u'x' == 1, \"u\");\n"
                  "_Static_assert(-0.0 == 1.0L, \"v\");\n"
                  "_Static_assert((A & 1) == 0 && (A | 4) == 3, \"w\");\n"
                  "_Static_assert((A << 2) == (A >> 1), \"x\");\n"
                  "_Static_assert(A == 3 ? 0 : 1, \"y\");\n"
                  "_Static_assert(!A, \"z\");\n"
                  "void f(void) { _Static_assert(A * 1000000 == 4, \"block\"); }\n",
        .errors = 11,
        .notes = 4,
    },
    {
        .name = "static assertion requirements and their values in C23",
        .source = "enum { A = 3 };\n"
                  "_Static_assert((bool)A == false, \"a\");\n"
                  "_Static_assert((_Bool)A == 0, \"b\");\n"
                  "_Static_assert(1wb + A == 2uwb, \"c\");\n"
                  "_Static_assert(1'000 * A == 18446744073709551615u, \"d\");\n"
                  "_Static_assert(alignof(int) == sizeof 1 + 1, \"e\");\n"
                  "_Static_assert(true && A == 4, \"f\");\n"
                  "_Static_assert(u8'a' == 1.0e10, \"g\");\n"
                  "_Static_assert((float)A / 3 == 2, \"h\");\n"
                  "_Static_assert(false, \"i\");\n",
        .errors = 9,
        .notes = 6,
        .c23 = true,
    },
    {
        // A statement expression's statements are evaluated as it is (#1447, check_common_statement_roots): a
        // short-circuited operand's, a constant conditional's other arm's, a constant if's and unreachable code's give
        // none of their runtime or unused-value warnings, as clang's CFG leaves their blocks unreachable.
        .name = "statements of unevaluated statement expressions",
        .source = "int g(int);\n"
                  "int f(int x)\n"
                  "{\n"
                  "    int r = 0;\n"
                  "    r += 0 && ({ x << 40; 1; });\n"
                  "    r += 1 || ({ 1 / 0; 1; });\n"
                  "    r += 0 ? ({ x << 33; 1; }) : 2;\n"
                  "    if (0) { r += ({ x << 34; 1; }); }\n"
                  "    return r;\n"
                  "    r += ({ x << 35; 1; });\n"
                  "}\n",
        .options = { "-Wall", "-Wextra" },
    },
    {
        // A body with statement expressions keeps its unreachable-code proof (#1447, check_common_unreachable).
        .name = "unreachable code in a body with statement expressions",
        .source = "int f(int x)\n"
                  "{\n"
                  "    int r = 0;\n"
                  "    return r;\n"
                  "    r += x << 35;\n"
                  "    x << 36;\n"
                  "    r += ({ x << 37; 1; });\n"
                  "    ({ x << 38; 1; });\n"
                  "}\n",
        .options = { "-Wall", "-Wextra" },
    },
    {
        .name = "reachable statement expressions, jumps out of them and returns inside them",
        .source = "int g(int);\n"
                  "int f(int x, int y)\n"
                  "{\n"
                  "    int r = ({ x << 40; 1; });\n"
                  "    r += y ? ({ x << 41; 2; }) : ({ x << 42; 3; });\n"
                  "    r += 1 && ({ x / 0; 1; });\n"
                  "    r += ({ if (x) goto out; 0; });\n"
                  "    r += ({ int k = ({ x << 43; 4; }); k; });\n"
                  "    for (;;) { r += ({ x << 44; 1; }); if (r) break; }\n"
                  "    while (1) { }\n"
                  "    r += ({ x << 45; 1; });\n"
                  "out:\n"
                  "    r += ({ x << 46; 1; });\n"
                  "    r += 0 && ({ int u; u + ({ x << 47; 1; }); });\n"
                  "    return r;\n"
                  "}\n"
                  "int h(int x)\n"
                  "{\n"
                  "    int r = ({ if (x) return 1; x << 48; 2; });\n"
                  "    r += ({ return 2; x << 49; 3; });\n"
                  "    return r;\n"
                  "}\n",
        .options = { "-Wall", "-Wextra" },
        .warnings = 16,
    },
    {
        .name = "-Wuninitialized in unevaluated statement expressions",
        .source = "int g(int);\n"
                  "int f(int x)\n"
                  "{\n"
                  "    int u;\n"
                  "    int r = 0 && ({ u + 1; });\n"
                  "    r += 0 ? ({ g(u); }) : 1;\n"
                  "    int v;\n"
                  "    r += ({ v; });\n"
                  "    return r;\n"
                  "    ({ int w; w; });\n"
                  "}\n",
        .options = { "-Wall", "-Wextra" },
        .warnings = 2,
        .notes = 1,
    },
    {
        // An enclosing construct's warning comes after the diagnostics its operands gave as clang built them (#1941): a
        // condition's -Wparentheses at its `)` or `;`, a call's -Wformat from its `)`, a bitwise operator's precedence and a
        // logical not's at their right operand's end; a statement expression's and a nested call's warnings first. A call
        // argument's -Wformat is at its callee.
        .name = "enclosing warnings follow their operands' diagnostics",
        .source = "int printf(const char *, ...);\n"
                  "int f(int x)\n"
                  "{\n"
                  "    if (x = ({ printf(\"%s\", 1); x; })) return 1;\n"
                  "    if (x = printf(\"%s\", 2)) return 1;\n"
                  "    while (x = ({ printf(\"%s\", 3); x; })) {}\n"
                  "    for (; x = ({ printf(\"%d\", \"v\"); 1; }); ) {}\n"
                  "    do {} while (x = ({ printf(\"%d\", \"w\"); 1; }));\n"
                  "    printf(\"%s\", ({ printf(\"%d\", \"s\"); 1; }));\n"
                  "    printf(\"%s\", printf(\"%d\", \"t\"));\n"
                  "    int a = x & ({ printf(\"%d\", \"u\"); 1; }) == 2;\n"
                  "    int b = !x == ({ printf(\"%d\", \"y\"); 1; });\n"
                  "    (void)a; (void)b;\n"
                  "    return (x = 1) ? printf(\"%s\", 4) : 0;\n"
                  "}\n",
        .options = { "-Wall" },
        .warnings = 19,
        .notes = 14,
    },
    {
        .name = "enclosing warnings follow their operands' diagnostics in C23 under -Wextra",
        .source = "int printf(const char *, ...);\n"
                  "int f(int x)\n"
                  "{\n"
                  "    if (x = ({ printf(\"%s\", 1); x; })) return 1;\n"
                  "    if (x = printf(\"%s\", 2)) return 1;\n"
                  "    while (x = ({ printf(\"%s\", 3); x; })) {}\n"
                  "    for (; x = ({ printf(\"%d\", \"v\"); 1; }); ) {}\n"
                  "    do {} while (x = ({ printf(\"%d\", \"w\"); 1; }));\n"
                  "    printf(\"%s\", ({ printf(\"%d\", \"s\"); 1; }));\n"
                  "    printf(\"%s\", printf(\"%d\", \"t\"));\n"
                  "    int a = x & ({ printf(\"%d\", \"u\"); 1; }) == 2;\n"
                  "    int b = !x == ({ printf(\"%d\", \"y\"); 1; });\n"
                  "    (void)a; (void)b;\n"
                  "    return (x = 1) ? printf(\"%s\", 4) : 0;\n"
                  "}\n",
        .options = { "-Wall", "-Wextra" },
        .warnings = 19,
        .notes = 14,
        .c23 = true,
    },
    {
        // Clang's DiagRuntimeBehavior drops a runtime warning in an array bound (ConstantEvaluated), a variable array's
        // too, and in an unevaluated operand's statement expression: shift counts, divisions, sign comparisons, constant
        // conversions and array indexes; a comma's unused value stays (#1447, check_common_bound).
        .name = "runtime warnings in array bounds and unevaluated statement expressions",
        .source = "int g(void) __attribute__((warn_unused_result));\n"
                  "void f(int n, unsigned u) {\n"
                  "    int b[n + (1 << 40)];\n"
                  "    int c[n + 1 / 0];\n"
                  "    int d[n + (u < -1)];\n"
                  "    char cc = 0;\n"
                  "    int e[n + (cc = 300)];\n"
                  "    int h[n + (n, 1)];\n"
                  "    int a2[3]; int k[n + a2[5]];\n"
                  "    int s[n + sizeof(1 << 41)];\n"
                  "    int t = sizeof(int[n + (1 << 45)]);\n"
                  "    int v = (int)sizeof(({ int w = 1 << 46; w; })) + (int)sizeof(({ 1 << 47; }));\n"
                  "    int x = (1 << 48) + 1 / 0;\n"
                  "    (void)b; (void)c; (void)d; (void)e; (void)h; (void)k; (void)s; (void)t; (void)v; (void)x;\n"
                  "}\n",
        .options = { "-Wall", "-Wextra" },
        .warnings = 3,
    },
    {
        .name = "runtime warnings in array bounds and unevaluated statement expressions in C23",
        .source = "int g(void) __attribute__((warn_unused_result));\n"
                  "void f(int n, unsigned u) {\n"
                  "    int b[n + (1 << 40)];\n"
                  "    int c[n + 1 / 0];\n"
                  "    int d[n + (u < -1)];\n"
                  "    char cc = 0;\n"
                  "    int e[n + (cc = 300)];\n"
                  "    int h[n + (n, 1)];\n"
                  "    int a2[3]; int k[n + a2[5]];\n"
                  "    int s[n + sizeof(1 << 41)];\n"
                  "    int t = sizeof(int[n + (1 << 45)]);\n"
                  "    int v = (int)sizeof(({ int w = 1 << 46; w; })) + (int)sizeof(({ 1 << 47; }));\n"
                  "    int x = (1 << 48) + 1 / 0;\n"
                  "    (void)b; (void)c; (void)d; (void)e; (void)h; (void)k; (void)s; (void)t; (void)v; (void)x;\n"
                  "}\n",
        .warnings = 3,
        .c23 = true,
    },
    {
        .name = "return flow conservative no-fallthrough and reachable fallthrough",
        .source = "int a(int c){if(c)return 1;return 2;}int b(int c){if(c)return 1;}int d(void){while(1){}}_Noreturn void r(void){while(1){}}\n",
        .warnings = 1,
    },
    {
        .name = "floating literal file and local initializers",
        .source = "int a=1.5;\nint f(void){int b=2.5;return b;}\n",
        .warnings = 2,
    },
    {
        // An enumeration or an _Atomic target takes neither -Wliteral-conversion nor -Wconstant-conversion in clang (#1447,
        // review on #1453): only a builtin type, qualified or not, does.
        .name = "conversion warnings skip enumeration and atomic targets",
        .source = "enum E { A, B };\nvoid arg(enum E);\nvoid argi(int);\nenum E fret(void) { return 3.5; }\n_Atomic int gy = 7.5;\n"
                  "_Atomic char gc = 300;\nvoid f(void) {\n  int x = 1.5; enum E e = 2.5; _Atomic int y = 3.5; _Atomic char z = 4.5;\n"
                  "  arg(4.5); argi(4.5); e = 5.5; y = 6.5; const int c = 8.5; volatile int v = 9.5;\n"
                  "  _Bool b = 0.5; _Atomic _Bool ab = 0.5; enum E e2 = 300; _Atomic char k = 300; char d = 300; z = 500;\n"
                  "  (void)x; (void)e; (void)y; (void)z; (void)c; (void)v; (void)b; (void)ab; (void)e2; (void)k; (void)d;\n}\n",
        .warnings = 6,
    },
    {
        // A brace element's destination is _Atomic by its declared type, which its plan part records stripped (#1447).
        .name = "conversion warnings skip atomic initializer elements",
        .source = "_Atomic char ga[1] = { 1.5 };\nstruct S { _Atomic char c; int i; };\nstruct S gs = { 1.5, 2.5 };\nvoid f(void) {\n"
                  "  _Atomic char a[2] = { 1.5, 2 };\n  struct S s = { 3.5, 4.5 };\n  struct { _Atomic int x; double d; } t = { .x = 5.5, .d = 1 };\n"
                  "  (void)a; (void)s; (void)t;\n}\n",
        .warnings = 2,
    },
    {
        // A return's or a prototype argument's _Atomic destination too, for either warning, beside the int, char and
        // plain parameter ones that still warn (review on #1453).
        .name = "conversion warnings skip atomic returns and arguments",
        .source = "_Atomic int f(void) { return 3.5; }\n_Atomic int gpar(_Atomic int);\nvoid h(void) { gpar(3.5); }\n"
                  "_Atomic char g(void) { return 300; }\nvoid gc(_Atomic char);\nvoid kc(void) { gc(300); }\n"
                  "struct S { int i; _Atomic int m; };\nvoid k(void) { struct S t; t.m = 3.5; t.m = 5000000000LL; (void)t; }\n"
                  "int fi(void) { return 2.5; }\nvoid pi(int);\nvoid hp(void) { pi(2.5); }\nchar fc(void) { return 300; }\n",
        .warnings = 3,
    },
    {
        // An enumeration takes -Wconstant-conversion by its integer type's range, though not -Wliteral-conversion, in
        // every position (review on #1453). (A brace element takes neither yet: #1759.)
        .name = "constant conversions to enumerations",
        .source = "enum Small : signed char { X };\nenum Small e = 300;\nenum Small rs(void) { return 300; }\n"
                  "void ca(enum Small);\nvoid cc(void) { ca(300); }\n"
                  "void as(void) { enum Small s; s = 300; (void)s; }\nenum FB : _Bool { FA };\nenum FB fb = 2;\n"
                  "enum E { A, B };\nenum E ee = 5000000000LL;\nenum Small lit = 2.5;\n",
        .warnings = 6,
        .c23 = true,
    },
    {
        // An assignment's or an increment's value is no constant to clang's evaluator (#1447), in a list's element too
        // (`{ y = 300 }`), but a comma's right operand is.
        .name = "no constant conversion of an assignment's value",
        .source = "void g(void) { int y; char c = y = 1000; char d[1] = { y = 300 }; unsigned char u = (y += 1, 300); char e = y++; "
                  "char f = (y = 1, 400); _Bool b = (y = 2); float fl = (y = 16777217); "
                  "(void)c; (void)d; (void)u; (void)e; (void)f; (void)b; (void)fl; }\n",
        .warnings = 2,
    },
    {
        // -Wunreachable-code-generic-assoc (#1447): an association of array type or a qualified one (an address space's,
        // restrict, a typedef's const spelled as its typedef), at its type's first token past the qualifiers, at file
        // scope and in a body, in sizeof's operand too; not _Atomic.
        .name = "generic associations never selected",
        .source = "typedef const int CI;\n"
                  "void g(void) { int x; int b[2];\n"
                  "int c = _Generic(b, int[2]: 1, default: 2);\nint d = _Generic(x, _Atomic int: 1, default: 2);\n"
                  "int e = _Generic(x, int const: 1, volatile long: 3, default: 2);\nint f = _Generic(x, CI: 1, default: 2);\n"
                  "int h = _Generic(&x, int * __attribute__((address_space(1))): 1, int * restrict: 4, default: 2);\n"
                  "int i = sizeof(_Generic(x, const char: 1, default: 2));\n"
                  "(void)c; (void)d; (void)e; (void)f; (void)h; (void)i; }\n"
                  "int k = _Generic(1, const int: 1, int[3]: 2, default: 0);\n",
        .warnings = 9,
    },
    {
        .name = "generic associations never selected, without -Wunreachable-code",
        .source = "void g(void) { int x; int b[2]; int c = _Generic(b, int[2]: 1, const int: 3, default: 2); (void)c; (void)x; }\n",
        .options = { "-Wno-unreachable-code" },
    },
    {
        // An association's type name is read as a cast's (#1447): objc_gc's qualifier, which the warning names, and
        // objc_ownership's warning on a type it doesn't apply to.
        .name = "attributes in generic association types",
        .source = "void g(void) { int x;\nint a = _Generic(x, const int: 1, default: 2);\n"
                  "int c = _Generic(&x, int __attribute__((objc_gc(weak))) *: 1, default: 2);\n"
                  "int e = _Generic(&x, int __attribute__((objc_gc(strong))) *: 1, default: 2);\n"
                  "int f = _Generic(&x, int __attribute__((objc_ownership(weak))) *: 1, default: 2);\n"
                  "(void)a; (void)c; (void)e; (void)f; }\n",
        .warnings = 4,
    },
    {
        .name = "floating literal assignment return and call conversions",
        .source = "int f(void){int a;a=1.5;return -2.5;}\nvoid g(int);void h(void){g(3.5);}\n",
        .warnings = 3,
    },
    {
        // A truth context converts a floating literal to _Bool (#147): conditions, `!`, `&&`, `||` and a conditional's first
        // operand, in unreachable code and short-circuited operands too; never a conditional's arms, a comma's result or
        // sizeof's operand.
        .name = "floating literal truth contexts",
        .source = "int f(int x){if(0.25)x=1;while(0.5)break;for(;0.75;)break;do{}while(1.5);return (2.5&&x)+(x||-3.5)+!4.5+(5.5?x:1)+(1.0&&x)+(0.0||x);}\n"
                  "int g(int x){return (x?1.5:2.5)&&x;}int h(int x){return (x,1.5)&&x;}unsigned long k(int x){return sizeof(1.5&&x);}\n"
                  "int u(int x){if(0)return 6.5&&x;return 0&&7.5;}\n_Bool b=1.5;_Bool c(void){return 2.5;}\n",
        .warnings = 13,
    },
    {
        // A static assertion's condition is a truth context too, warned where clang's parser reads it: between a declaration's
        // initializer and a later statement's, at file scope, and in unreachable code.
        .name = "floating literal static assertions",
        .source = "int o(int x){int y=2.5;_Static_assert(1.5,\"\");_Static_assert(0.0==0,\"\");return y+(x&&3.5);}\n_Static_assert(-0.5,\"\");\n"
                  "_Static_assert(1.5&&2.5,\"\");int z(int x){if(0){_Static_assert(0.5,\"\");}return x;}\n",
        .warnings = 7,
    },
    {
        .name = "floating literal source formats",
        .source = "int a=1.1f;int b=1.1;int c=1.1L;\n",
        .warnings = 3,
    },
    {
        .name = "floating literal array and record initializer elements",
        .source = "int values[2]={1.5,2.5};\nstruct S{int n;}object={1.5};\n",
        .warnings = 3,
    },
    {
        .name = "floating literal local brace initializer order",
        .source = "int f(void){int a[2]={1.5,2.5};int b={3.5};return a[0]+b;}\n",
        .warnings = 3,
    },
    {
        .name = "floating literal scalar and aggregate declaration order",
        .source = "int f(void){int x=4.5;int a[2]={1.5,2.5};int y=3.5;return x+a[0]+y;}\n",
        .warnings = 4,
    },
    {
        .name = "floating literal range initializer conversions",
        .source = "int values[4]={[0 ... 3]=1.5};\n",
        .warnings = 4,
    },
    {
        .name = "floating literal decimal and hexadecimal spelling",
        .source = "int a=0.9999999999999999;int b=1e-30;int c=0x1.8p0;\n",
        .warnings = 3,
    },
    {
        .name = "floating literal adjacent integer precision",
        .source = "int a=1.9999999999999998;int b=1.0000000000000002;\n",
        .warnings = 2,
    },
    {
        .name = "floating literal call argument subtrees follow right to left conversion order",
        .source = "void f(int,int);int g(int);void h(void){f(g(1.5),2.5);f(3.5,g(4.5));f(5.5,6.5);}\n",
        .warnings = 6,
    },
    {
        .name = "floating literal nested call and comma conversions are emitted once",
        .source = "void f(int,int);int g(int);void h(void){f(g(1.5),g(2.5));f(3.5,4.5),f(5.5,6.5);}\n",
        .warnings = 6,
    },
    {
        .name = "binary operand calls visit literal conversions right to left",
        .source = "int f(int);int h(void){return f(1.5)+f(2.5);}\n",
        .warnings = 2,
    },
    {
        .name = "comma assignment conversions visit the right operand first",
        .source = "int h(void){int a,b;a=1.5,b=2.5;return a+b;}\n",
        .warnings = 2,
    },
    {
        .name = "reverse conversions precede source ordered bounds and unused results",
        .source = "int f(int);void h(void){int a[1];(f(1.5)+a[2]),(f(2.5)+a[3]);}\n",
        .warnings = 6,
        .notes = 2,
    },
    {
        .name = "reverse conversions preserve each inner comma marker before its bounds warning",
        .source = "void f(int);void h(void){int a[1];(a[2],f(1.5)),(a[3],f(2.5));}\n",
        .warnings = 6,
        .notes = 2,
    },
    {
        .name = "comma integer call conversions follow the same reverse walk",
        .source = "short f(short);void h(void){f(65536),f(65537);}\n",
        .warnings = 2,
    },
    {
        .name = "brace initializer conversions precede nested call conversions",
        .source = "int f(int);int h(void){int a[2]={(f(1.5),2.5),(f(3.5),4.5)};return a[0];}\n",
        .warnings = 4,
    },
    {
        .name = "unreachable return and local initializer retain literal conversion warnings",
        .source = "int f(void){if(0)return 1.5;if(0){int a=2.5;(void)a;}return 0;}\n",
        .warnings = 2,
    },
    {
        .name = "unreachable call conversion precedes the later reachable conversion",
        .source = "void f(int);void h(void){if(0)f(1.5);f(2.5);}\n",
        .warnings = 2,
    },
    {
        .name = "unreachable literal conversions do not enable runtime warning families",
        .source = "int f(void){int a[1];if(0){int x=1.5;(void)x;1;a[2];return 1/0;}return 0;}\n",
        .warnings = 1,
    },
    {
        .name = "short-circuited and conditional call literals remain source checks",
        .source = "int g(int);int f(void){return 0&&g(1.5);}int h(void){return 1?0:g(2.5);}\n",
        .warnings = 2,
    },
    {
        .name = "conditional call arms retain literal conversions",
        .source = "int f(int); void h(int c){c ? f(1.5) : f(2.5);}\n",
        .warnings = 2,
    },
    {
        .name = "comma conditional arms do not acquire outer conversion",
        .source = "int side(void);void take(int);int f(int c){int x=c?(side(),2.5):3.0;x=c?3.0:(side(),2.5);take(c?(side(),2.5):3.0);return c?(side(),2.5):3.0;}\n",
        .options = { "-Werror=literal-conversion" },
        .c23 = true,
    },
    {
        .name = "nested comma and conditional comma results do not inherit literal conversion",
        .source = "int side(void);int f(int c){return (side(),(side(),2.5));}int g(int c){return (side(),(c?2.5:2.5));}\n",
        .options = { "-Werror=literal-conversion" },
        .c23 = true,
    },
    {
        .name = "integer conversion of comma results uses the whole result expression",
        .source = "int side(void);short f(int c){return (side(),(c?65536:65537));}short g(void){return (side(),(1?65536:65537));}short h(void){return (side(),(side(),65536));}short i(int c){return c?(side(),65536):65537;}\n",
        .warnings = 4,
    },
    {
        .name = "top level comma still receives outer conversion",
        .source = "int side(void);int f(void){return (side(),2.5);}\n",
        .warnings = 1,
    },
    {
        .name = "unevaluated sizeof and generic arms suppress literal conversions",
        .source = "int f(void){return sizeof((int){1.5})+_Generic(0,int:0,default:(int){2.5});}\n",
    },
    {
        .name = "conditional literal arms inherit initializer and return targets",
        .source = "int a=0?1.5:2.5;int b=1?3.5:4.5;int f(int c){return c?5.5:6.5;}\n",
        .warnings = 6,
    },
    {
        .name = "conditional brace initializer and unreachable argument targets",
        .source = "int a[1]={0?1.5:2.5};void f(int);void h(int c){if(0)f(c?3.5:4.5);}\n",
        .warnings = 4,
    },
    {
        .name = "conditional integer conversions retain selected-arm pruning",
        .source = "short a=0?65536:65537;short b=1?65538:65539;\n",
        .warnings = 2,
    },
    {
        .name = "nested integer argument conversions follow right to left order",
        .source = "void f(short,short);short g(short);void h(void){f(g(65536),65537);}\n",
        .warnings = 2,
    },
    {
        .name = "unreachable integer conversion families remain suppressed",
        .source = "void f(short);short g(void){if(0)return 65536;if(0){short a=65536;(void)a;}return 0;}"
                  "void h(void){if(0)f(65536);f(65537);}int i(void){return 0&&(f(65536),1);}int j(void){return 1?0:(f(65536),1);}\n",
        .warnings = 1,
    },
    {
        // An unreachable initializer's barriers outlive its body's scratch (#1447): the next body's marks and queue
        // must not reuse them, which made a later body's initialized variables "uninitialized when used here".
        .name = "unreachable initializer barriers outlive their body",
        .source = "int f(int n) {\n  return n;\n  int dead = n;\n  return dead;\n}\n"
                  "int g(int n) {\n  int a = n + 1;\n  if (n > 1) n = a;\n  int b = n + 2;\n  if (n > 2) n = b;\n"
                  "  int c = n + 3;\n  if (n > 3) n = c;\n  int d = n + 4;\n  if (n > 4) n = d;\n  return n;\n}\n",
        .options = { "-Wall" },
    },
    {
        .name = "initializers after return retain only literal source conversions",
        .source = "int h(void){return 0;short a=65536;int b=1.5;(void)a;(void)b;}\n",
        .warnings = 1,
    },
    // Clang reports a literal conversion at once, as its full expression completes (Sema::Diag), while constant
    // conversions, shift counts and division by zero wait for their function's end (DiagRuntimeBehavior); at file scope a
    // declarator's constant conversion follows its literal ones. The whole output is clang's, in clang's order.
    {
        .name = "a truth context's literal conversion precedes the body's shift counts",
        .source = "int f(int x){return (x<<40)+(x>>-1)+(1.5?x<<33:0);}\n",
        .warnings = 4,
    },
    {
        .name = "literal conversions report at once before the body's deferred conversions and shift counts",
        .source = "char c=1<<9;\n"
                  "void g(int x){char d=1<<9;char e=x<<40;int k=2.5;if(0.5){x<<=64;}x=!1.5?x>>99:x;(void)d;(void)e;(void)k;}\n",
        .warnings = 7,
    },
    {
        // A divisor clang's EvaluateAsInt finds zero through a const object's initializer (#1447,
        // check_common_zero_divisor): a scalar's, an element's, a member's, through parentheses and casts, at file and
        // block scope; not a volatile's, a non-const's or one initialized at run time, and none in unreachable code.
        .name = "division by a const object's zero",
        .source = "const int k = 0;\n"
                  "static const int ks = 0;\n"
                  "const volatile int kv = 0;\n"
                  "int nk = 0;\n"
                  "const long kl = 0;\n"
                  "const int ka[2] = { 0, 1 };\n"
                  "struct P { int m; };\n"
                  "const struct P kp = { 0 };\n"
                  "const int one = 1;\n"
                  "const char s[] = \"a\";\n"
                  "int f(int x) {\n"
                  "    const int lk = 0;\n"
                  "    const int lk2 = x;\n"
                  "    int r = 1 / k + 1 % ks + 1 / kv + 1 / nk + x / kl + 1 / lk + 1 / lk2 + 1 / ka[0] + 1 / kp.m + 1 / (k) + 1 / (int)k;\n"
                  "    if (0) { r = 1 / k; }\n"
                  "    r += (int)sizeof(1 / k) + 1 / one + 1 / s[1];\n"
                  "    r += x ? 1 / lk : 0;\n"
                  "    return r;\n"
                  "    r = 1 / k;\n"
                  "}\n",
        .warnings = 10,
    },
    {
        .name = "division by a const object's zero in C23 under -Wall",
        .source = "const int k = 0;\n"
                  "static const int ks = 0;\n"
                  "const volatile int kv = 0;\n"
                  "int nk = 0;\n"
                  "const long kl = 0;\n"
                  "const int ka[2] = { 0, 1 };\n"
                  "struct P { int m; };\n"
                  "const struct P kp = { 0 };\n"
                  "const int one = 1;\n"
                  "const char s[] = \"a\";\n"
                  "int f(int x) {\n"
                  "    const int lk = 0;\n"
                  "    const int lk2 = x;\n"
                  "    int r = 1 / k + 1 % ks + 1 / kv + 1 / nk + x / kl + 1 / lk + 1 / lk2 + 1 / ka[0] + 1 / kp.m + 1 / (k) + 1 / (int)k;\n"
                  "    if (0) { r = 1 / k; }\n"
                  "    r += (int)sizeof(1 / k) + 1 / one + 1 / s[1];\n"
                  "    r += x ? 1 / lk : 0;\n"
                  "    return r;\n"
                  "    r = 1 / k;\n"
                  "}\n",
        .options = { "-Wall" },
        .warnings = 10,
        .c23 = true,
    },
    {
        .name = "literal conversions precede selection arms' shift counts and division by zero",
        .source = "int h(int x){int a=_Generic(x,int:1,long:x<<70);int b=1.5&&(x+(x<<35));int c=__builtin_choose_expr(1,x,x>>40);short s=3.7;"
                  "return a+b+c+s+(x<<-3)/0;}\n",
        .warnings = 7,
    },
    {
        .name = "argument and conditional-arm literal conversions precede deferred constant conversions",
        .source = "void take(char);int m(int x){take(1<<9);take(2.5);unsigned char u=(x?300:1.5);(void)u;return x?x<<32:(0?x<<50:1);}\n",
        .warnings = 5,
    },
    {
        .name = "literal conversions in unreachable and short-circuited code precede deferred shift counts",
        .source = "int g1(int x){if(0){int a=2.5;x<<=40;(void)a;}int b=sizeof(x<<40)+sizeof((char)3.5);int c=0&&(x<<40)&&1.5;"
                  "int d=_Generic(1.5,double:x<<1,default:x<<99);int e=(char)(x>>70)+4;int f[2]={1.5,x<<33};return b+c+d+e+f[0];}\n"
                  "_Static_assert(1.5,\"x\");struct S{int m;}s={2.5};\n",
        .warnings = 8,
    },
    {
        .name = "file-scope literal conversions keep each declarator's order with constant conversions",
        .source = "char c=1<<9;int a=2.5;_Bool b=0.5;char d=1.5?300:2;char w=300,x=5.5;int v[3]={300,4.5,7};_Static_assert(1.5,\"\");"
                  "struct T{int i;double e;}t={6.5,1.5};\n",
        .warnings = 10,
    },
    {
        .name = "unreachable code keeps literal conversions in each full expression's conversion order",
        .source = "int c0(int);int u(int x){return x;x=x<<40;int a=2.5;char c=1.5?300:0;x/=0;return a+c+c0(3.5)+(x&&4.5);}\n"
                  "int w(int x){if(0){x=x<<40;x=5.5;}while(0){x=x/0+6.5;}return x;}\n",
        .warnings = 5,
    },
    {
        .name = "floating literal boolean conversions",
        .source = "_Bool a=1.5;_Bool b=2.0;_Bool c=1.0;\n",
        .warnings = 2,
    },
    {
        .name = "floating literal out of range conversions",
        .source = "unsigned char a=1000.5;unsigned b=-1.5;int c=1e30;\n",
        .warnings = 3,
    },
    {
        .name = "floating literal macro expansion note",
        .source = "#define VALUE 1.5\nint a=VALUE;\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "floating literal parentheses retain the literal warning",
        .source = "int a=((1.5));int b=+2.5;int c=-3.5;\n",
        .warnings = 3,
    },
    {
        .name = "explicit and nonliteral floating conversions stay quiet by default",
        .source = "int a=(double)1.5f;int b=(int)1.5;int c=2.5-1.0;int d=2.5+0.0;int e=1.0;\n"
                  "int f(double a){return a;}int g(void){double a=1.5;return a;}\nint h=__builtin_nan(\"\");int i=__builtin_inf();\n",
    },
    {
        .name = "floating literal conversion group disabled",
        .source = "int a=1.5;\n",
        .options = { "-Wno-literal-conversion" },
    },
    {
        // A block's declaration with linkage (an extern object, a function) belongs to the translation unit in clang, so
        // it is never an unused variable or function; the block's own static, register and automatic objects are (#1447).
        .name = "a block's extern declarations are never unused",
        .source = "void f(void) { extern int x; }\nvoid g(void) { extern int y; (void)y; }\nvoid h(void) { extern int z(void); }\n"
                  "void k(void) { extern int w __attribute__((unused)); }\nvoid m(void) { extern int v; { extern int v; v = 1; } }\n"
                  "int q;\nvoid n(void) { extern int q; }\nstatic int s5;\nvoid o(void) { extern int s5; (void)s5; }\n"
                  "void p(void) { extern int arr[]; extern int a, b; (void)b; }\nvoid s(void) { static int st; register int r; int loc; }\n"
                  "void t(void) { int y4; { extern int y4; } }\n",
        .options = { "-Wall" },
        .warnings = 4,
    },
    {
        // A variable with an applied cleanup is used, as clang's ShouldDiagnoseUnusedDecl has it (#1881): one declarator's,
        // a specifier's on several declarators, and one never read but assigned; another declarator of the declaration is
        // still unused. (A file-scope variable whose cleanup is ignored is unused too, but its -Wignored-attributes would
        // be an error before it under the sweep's -Werror, after which clang gives no file-scope unused warning.)
        .name = "a cleanup variable is never unused",
        .source = "void rel(int *p);\nvoid relv(void *p);\nint f(int a) { int c __attribute__((cleanup(rel))) = a; if (a) return 1; return 2; }\n"
                  "void g(void) { __attribute__((cleanup(rel))) int c = 0, d = 1; }\nvoid h(int a) { int c __attribute__((cleanup(relv))); c = a; }\n"
                  "void k(void) { int c __attribute__((cleanup(rel))), e; }\n",
        .options = { "-Wall" },
        .warnings = 1,
    },
    {
        // A use through a block's declaration with linkage uses the outer entity it redeclares, through nested blocks too;
        // a block's extern that nothing uses leaves a file-scope static unused (#1447).
        .name = "a use through a block's extern declaration uses the outer entity",
        .source = "static void sf(void);\nvoid o(void) { extern void sf(void); sf(); }\nstatic void sf(void) {}\nstatic int t;\n"
                  "void p(void) { { extern int t; } }\nstatic int u;\nvoid q(void) { { extern int u; { extern int u; (void)u; } } }\n",
        .options = { "-Wall" },
        .warnings = 1,
    },
    {
        // Clang's error at a block's extern declarator with an initializer, __private_extern__ too, not a generic one.
        .name = "a block's extern declaration with an initializer",
        .source = "int y;\nvoid f(void) { extern int y = 2, z; (void)z; }\n",
        .errors = 1,
    },
    {
        .name = "a block's private extern declaration with an initializer",
        .source = "void f(void) { __private_extern__ int x = 1; }\n",
        .errors = 1,
    },
    {
        .name = "floating literal conversion parent group disabled",
        .source = "int a=1.5;\n",
        .options = { "-Wno-conversion" },
    },
    {
        .name = "floating literal conversion source policy",
        .source = "#pragma clang diagnostic error \"-Wliteral-conversion\"\nint a=1.5;\n",
        .errors = 1,
    },
    {
        .name = "float conversion error does not promote literal conversion",
        .source = "int a=1.5;\n",
        .options = { "-Werror=float-conversion" },
        .warnings = 1,
    },
    {
        .name = "float conversion disabled does not suppress literal conversion",
        .source = "int a=1.5;\n",
        .options = { "-Wno-float-conversion" },
        .warnings = 1,
    },
    {
        .name = "literal disabled before float conversion error",
        .source = "int a=1.5;\n",
        .options = { "-Wno-literal-conversion", "-Werror=float-conversion" },
    },
    {
        .name = "literal error after float conversion disabled",
        .source = "int a=1.5;\n",
        .options = { "-Wno-float-conversion", "-Werror=literal-conversion" },
        .errors = 1,
    },
    {
        .name = "float conversion source policy does not promote literal conversion",
        .source = "#pragma clang diagnostic error \"-Wfloat-conversion\"\nint a=1.5;\n",
        .warnings = 1,
    },
    {
        .name = "literal source policy remains disabled across float conversion error",
        .source = "#pragma clang diagnostic ignored \"-Wliteral-conversion\"\n#pragma clang diagnostic error \"-Wfloat-conversion\"\nint a=1.5;\n",
    },
    {
        .name = "struct brace initializer members warn of constant conversions at file and block scope",
        .source = "struct S{int i;char c;}s={2,300};\nvoid f(void){struct S t={2,300};(void)t;}\n",
        .warnings = 2,
    },
    {
        .name = "array, nested and designated brace initializers warn in clang's reverse order",
        .source = "char a[3]={300,301,302};struct T{struct S{char x,y;}s;char d[2];}t={{303,304},{305,306}};struct S u={.y=307};\n",
        .warnings = 8,
    },
    {
        .name = "brace constant conversions follow their values' runtime warnings, arms, commas and ranges",
        .source = "void f(int x){char b[3]={301,x<<40,302};char c[2]={x?303:1,(x,304)};char e[3]={[0 ... 2]=305};(void)b;(void)c;(void)e;}\n",
        .warnings = 9,
    },
    {
        .name = "brace constant conversions skip unreachable code and keep clang's unsigned and bit-pattern exemptions",
        .source = "void f(void){if(0){char g[1]={310};(void)g;}}unsigned char t1[]={255,256,-1,-129};signed char t2[]={127,128,-129,0xff,0x1ff};\n",
        .warnings = 4,
    },
    {
        .name = "same-width brace constants warn at once, except into a character element",
        .source = "short g[]={32768,70000};struct{signed char c;short s;}h={128,32768};void k(int x){short t[2]={32768,70000};x<<40;(void)t;}\n",
        .warnings = 7,
    },
    {
        .name = "same-width scalar constants warn at once, before a body's queued warnings",
        .source = "void f(int x){x<<40;short a=32768;short b=70000;(void)a;(void)b;}\n",
        .warnings = 4,
    },
    {
        .name = "a file-scope shift count follows its declarator's literal conversions",
        .source = "int h[2]={15.5,1<<40};\n",
        .warnings = 2,
    },
    {
        .name = "file-scope declarators queue shift counts and constant conversions to their own ends",
        .source = "int a=1<<40,b=2.5;int e[2]={1<<41,4.5},f=5.5,g=1<<42;char h[2]={301,1<<43},i[1]={6.5};struct{char c;int i;}k={302,1<<44};\n",
        .warnings = 12,
    },
    {
        // A comma's left operand's -Wunused-value in a file-scope initializer is queued to the declarator's end with its
        // operands' shift counts, in the order clang builds them (#1943); a brace initializer's values walk in
        // initializer order, before its conversions, in a function too.
        .name = "comma and shift warnings in initializers in clang's order",
        .source = "struct T { char a; char b; };\n"
                  "int z = (1 << 40, 3), y = (2, 1 << 41);\n"
                  "int w = (2 << 42, 1 << 43, 3);\n"
                  "char c = (1 << 42, 300);\n"
                  "struct T s = { (1 << 43, 1), (4, 1 << 44) };\n"
                  "int arr[2] = { (5, 6), 7 };\n"
                  "struct T g = { (5, 302), (6, 303) };\n"
                  "void f(int x) { struct T t = { (1, 300), (2, 301) }; struct T u = { (3, x << 40), 1.5 }; struct T v = { x / 0, (4, x) }; (void)t; (void)u; (void)v;\n"
                  "    static int k = (1 << 45, 3); (void)k; int q = (1 << 46, 2); (void)q; }\n",
        .options = { "-Wall" },
        .warnings = 34,
        // (-c can't compile a comma's static initializer yet.)
        .syntax_only = true,
    },
    {
        .name = "comma warnings in initializers in clang's order without shift counts",
        .source = "struct T { char a; char b; };\n"
                  "int z = (1 << 40, 3), y = (2, 1 << 41);\n"
                  "int w = (2 << 42, 1 << 43, 3);\n"
                  "char c = (1 << 42, 300);\n"
                  "struct T s = { (1 << 43, 1), (4, 1 << 44) };\n"
                  "int arr[2] = { (5, 6), 7 };\n"
                  "struct T g = { (5, 302), (6, 303) };\n"
                  "void f(int x) { struct T t = { (1, 300), (2, 301) }; struct T u = { (3, x << 40), 1.5 }; struct T v = { x / 0, (4, x) }; (void)t; (void)u; (void)v;\n"
                  "    static int k = (1 << 45, 3); (void)k; int q = (1 << 46, 2); (void)q; }\n",
        .options = { "-Wno-shift-count-overflow" },
        .warnings = 24,
        .c23 = true,
        .syntax_only = true,
    },
    {
        .name = "literal conversions precede constant conversions in one brace initializer",
        .source = "char a[4]={300,1.5,301,2.5};void f(void){char b[4]={303,5.5,304,6.5};int y=7.5;(void)b;(void)y;}\n",
        .warnings = 9,
    },
    {
        .name = "brace members of an enumeration warn by its integer type's range",
        .source = "enum Small:signed char{S0};struct T{enum Small m;int i;}st={300,1};enum Small sa[2]={1,301};\n",
        .warnings = 2,
        .c23 = true,
    },
    {
        .name = "brace parts of a constant shift warn of its changed value, and into _Bool of its truth",
        .source = "struct S{short s;char c;};short a[1]={1<<20};int c[1]={3LL<<40};short d={1<<20};struct S e={1<<18,2<<8};"
                  "void f(void){short x[1]={4<<18};struct S y={0,1<<9};_Bool b[1]={1<<3};short z[1]={1<<3};(void)x;(void)y;(void)b;(void)z;}\n",
        .warnings = 8,
    },
    {
        .name = "a compound literal's constant conversions follow its full expression's counts, in conditions and increments too",
        .source = "struct S{int i;char c;};int f(int x){x=(struct S){x,300}.c+(x<<33);if((struct S){x,301}.c+(x<<34))return x<<32;"
                  "for(int i=0;i<2;i+=(struct S){1,302}.c+(x<<35)){struct S s={1,303};x+=s.c+(x<<36);}return x;}\n",
        .warnings = 9,
    },
    {
        .name = "_Atomic brace elements and members take no constant conversion",
        .source = "_Atomic short a0[2]={32768,1};struct{_Atomic char c;int i;}s={300,1};void f(void){_Atomic short a3[2]={32768,70000};(void)a3;}\n",
    },
    {
        .name = "brace constant conversion suppression keeps literal conversions",
        .source = "char a[4]={300,1.5,301,2.5};short g[]={32768};\n",
        .options = { "-Wno-constant-conversion" },
        .warnings = 2,
    },
    {
        .name = "brace constant conversion promotion rejects object publication",
        .source = "struct S{int i;char c;}s={2,300};\n",
        .options = { "-Werror=constant-conversion" },
        .errors = 1,
    },
    {
        .name = "file initializer discards pointer qualifiers at its declaration",
        .source = "const int b=1;\nint *c=&b;\n",
        .warnings = 1,
    },
    {
        .name = "local initializer discards pointer qualifiers",
        .source = "int f(void){const int b=1;int *c=&b;return *c;}\n",
        .warnings = 1,
    },
    {
        .name = "assignment discards pointer qualifiers",
        .source = "void f(const int *a,int *b){b=a;}\n",
        .warnings = 1,
    },
    {
        .name = "return discards pointer qualifiers at its expression",
        .source = "const int b=1;\nint *f(void){return &b;}\n",
        .warnings = 1,
    },
    {
        .name = "discarded pointer qualifiers follow named policy",
        .source = "const int b=1;\nint *c=&b;\n",
        .options = { "-Wno-incompatible-pointer-types-discards-qualifiers" },
    },
    {
        .name = "discarded pointer qualifiers honor source severity",
        .source = "#pragma clang diagnostic error \"-Wincompatible-pointer-types-discards-qualifiers\"\nconst int b=1;\nint *c=&b;\n",
        .errors = 1,
    },
    {
        .name = "discarded pointer qualifiers retain typedef spellings",
        .source = "typedef const int CI;CI b=1;typedef int *P;P c=&b;\n",
        .warnings = 1,
    },
    {
        .name = "discarded array qualifiers retain the source array type",
        .source = "const int b[2]={1,2};int *c=b;\n",
        .warnings = 1,
    },
    {
        .name = "discarded pointer qualifiers retain outer const spelling",
        .source = "const int b=1;int *const c=&b;\n"
                  "void f(const int *const a){int *const b=a;(void)b;}\n"
                  "int *g(const int *const a){return a;}\n",
        .warnings = 3,
    },
    {
        .name = "plain char pointer initializer and return conversion",
        .source = "unsigned char a[2];char *b=a;\nchar *f(void){return a;}\n",
        .warnings = 2,
    },
    {
        .name = "direct call qualifier warnings retain named and unnamed parameter notes",
        .source = "void f(int *,int *b);void g(const int *p){f(p,p);}\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "direct call parameter note uses the visible redeclaration",
        .source = "void f(int *first);void f(int *second);void g(const int *p){f(p);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "empty-list redeclaration retains conversion checks without a parameter note",
        .source = "void f(int *first);void f();void g(const int *p){f(p);}\n",
        .options = { "-Wno-deprecated-non-prototype" },
        .warnings = 1,
    },
    {
        .name = "closed-block prototype supplies no parameter note to an unprototyped call",
        .source = "void earlier(void){void f(int *hidden);}void f();void g(const int *p){f(p);}\n",
        .options = { "-Wno-deprecated-non-prototype" },
    },
    {
        .name = "function pointer variable call has no parameter declaration note",
        .source = "void (*f)(int *parameter);void g(const int *p){f(p);}\n",
        .warnings = 1,
    },
    {
        .name = "function pointer typedef and parameter calls have no declaration notes",
        .source = "typedef void (*F)(int *parameter);F f;void g(const int *p){f(p);}\n"
                  "void h(void (*callback)(int *parameter),const int *p){callback(p);}\n",
        .warnings = 2,
    },
    {
        .name = "dereferenced and cast callees have no parameter declaration notes",
        .source = "void f(int *parameter);void g(const int *p){(*f)(p);((void(*)(int *))f)(p);}\n",
        .warnings = 2,
    },
    {
        .name = "pointer argument warning is located at the expression beginning",
        .source = "void f(int *parameter);void g(const int *p){f(p+1);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "nested pointer qualifier wording includes the parameter note",
        .source = "void f(int **parameter);void g(const int **p){f(p);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "pointer call macro expansion precedes its parameter note",
        .source = "#define F f\n#define P p\nvoid f(int *parameter);void g(const int *p){F(P);}\n",
        .warnings = 1,
        .notes = 2,
    },
    {
        .name = "mixed pointer conversion groups follow argument order",
        .source = "void f(int *a,int *b);void g(const int *p,unsigned int *q){f(q,p);f(p,q);}\n",
        .warnings = 4,
        .notes = 4,
    },
    {
        .name = "plain char signedness overrides an array qualifier discard",
        .source = "void f(char *p);void g(void){const unsigned char a[2]={1,2};f(a);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "pointer parameter notes skip intervening scalar and callback parameters",
        .source = "void f(int *,int count,void callback(int *),int *last);\n"
                  "void g(const int *p){f(p,0,0,p);}\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "pointer parameter type spelling belongs to its own specifiers",
        .source = "typedef int *P;void f(P first,int *second);void g(const int *p){f(p,p);}\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "pointer call qualifiers and signedness obey independent group policy",
        .source = "void f(int *a,int *b);void g(const int *p,unsigned int *q){f(p,q);}\n",
        .options = { "-Wno-incompatible-pointer-types-discards-qualifiers" },
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "pointer call warning promotion retains its parameter note",
        .source = "#pragma clang diagnostic error \"-Wincompatible-pointer-types-discards-qualifiers\"\n"
                  "void f(int *parameter);void g(const int *p){f(p);}\n",
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "nested pointer initialization assignment and return wording",
        .source = "void f(const int **p,int **q){int **r=p;q=p;(void)r;}\nint **g(const int **p){return p;}\n",
        .warnings = 3,
    },
    {
        .name = "nonconstant file initializer",
        .source = "int a;\nint b=a;\n",
        .errors = 1,
    },
    {
        .name = "nonconstant aggregate initializer element",
        .source = "int a;\nint values[2]={1,a};\n",
        .errors = 1,
    },
    {
        .name = "nonconstant static local initializer",
        .source = "int read(void);\nint f(void){static int value=read();return value;}\n",
        .errors = 1,
    },
    {
        .name = "duplicate qualifiers and declaration specifiers",
        .source = "const const int a;\nvolatile volatile int b;\n_Atomic _Atomic int c;\nextern extern int d;\n"
                  "static static int e;\ntypedef typedef int T;\nunsigned unsigned int f;\n",
        .warnings = 7,
    },
    {
        .name = "duplicate function specifiers",
        .source = "inline inline void f(void){}\n_Noreturn _Noreturn void g(void){for(;;){}}\n",
        .warnings = 2,
    },
    {
        .name = "duplicate separated noreturn specifiers",
        .source = "_Noreturn void _Noreturn f(void){for(;;){}}\n_Noreturn extern _Noreturn void g(void);\n",
        .warnings = 2,
    },
    {
        .name = "duplicate parameter and pointer qualifiers",
        .source = "int f(const const int x,int *volatile volatile p){return x+*p;}\n",
        .warnings = 2,
    },
    {
        .name = "duplicate specifiers in operand type names",
        .source = "typeof(unsigned unsigned int) q;\nint r = sizeof(typeof(const const int));\nenum E : unsigned unsigned short { A };\n"
                  "_Atomic(signed signed char) s;\ntypeof(_Atomic _Atomic int) t;\n"
                  "int f(void) { return sizeof(short short) + (int)(typeof(volatile volatile long))1; }\n",
        .warnings = 7,
        .compare_warning_text = true,
    },
    {
        // An enum base's const and volatile (#1952): clang's -Wunderlying-cv-qualifier-ignored once per base, at its first
        // token that isn't a qualifier, a typedef's qualifiers too, and no stand-alone declaration's warning for them.
        .name = "enum underlying type qualifiers",
        .source = "enum H : const int { D };\nenum I : const volatile const int { E };\nenum J : int const { F };\n"
                  "enum K : volatile unsigned long { G };\ntypedef const int CI;\nenum L : CI { H1 };\nenum M : const CI { M1 };\n"
                  "enum N : const int;\nconst enum O : const int { O1 };\nenum Q : const int { Q1 } q;\n"
                  "void f(void) { enum R : const short { R1 }; enum R r = R1; (void)r; }\nstruct T { enum U : const int { U1 } u; };\n",
        .warnings = 13,
        .compare_warning_text = true,
    },
    {
        .name = "enum underlying type qualifiers under -Wall",
        .source = "enum H : const int { D };\nenum I : const volatile const int { E };\nenum J : int const { F };\n"
                  "enum K : volatile unsigned long { G };\ntypedef const int CI;\nenum L : CI { H1 };\nenum M : const CI { M1 };\n"
                  "enum N : const int;\nconst enum O : const int { O1 };\nenum Q : const int { Q1 } q;\n"
                  "void f(void) { enum R : const short { R1 }; enum R r = R1; (void)r; }\nstruct T { enum U : const int { U1 } u; };\n",
        .options = { "-Wall" },
        .warnings = 13,
        .compare_warning_text = true,
    },
    {
        .name = "enum underlying type qualifiers under -w",
        .source = "enum H : const int { D };\nenum I : const volatile const int { E };\nenum J : int const { F };\n"
                  "enum K : volatile unsigned long { G };\ntypedef const int CI;\nenum L : CI { H1 };\nenum M : const CI { M1 };\n"
                  "enum N : const int;\nconst enum O : const int { O1 };\nenum Q : const int { Q1 } q;\n"
                  "void f(void) { enum R : const short { R1 }; enum R r = R1; (void)r; }\nstruct T { enum U : const int { U1 } u; };\n",
        .options = { "-w" },
    },
    {
        .name = "enum underlying type qualifiers as errors",
        .source = "enum H : const int { D };\n",
        .options = { "-Werror=underlying-cv-qualifier-ignored" },
        .errors = 1,
    },
    {
        .name = "enum underlying type qualifiers disabled",
        .source = "enum H : const int { D };\n",
        .options = { "-Wno-underlying-cv-qualifier-ignored" },
    },
    {
        // An enum's fixed base in a declaration with a declarator (#2213): clang's -Welaborated-enum-base, an error by default,
        // at the `:`, after which the declaration is an elaborated reference: a new name declared with the base (a later
        // body's "previously declared with fixed underlying type"), a visible enum's base ignored; the base's own warnings.
        .name = "an enum base with a declarator",
        .source = "enum P : unsigned const long volatile x;\nvoid f(void) { enum S : volatile char s; s = 1; (void)s; }\nenum Q : short q1, q2;\n"
                  "enum Q { QA = 1 };\n_Static_assert(sizeof(q1) == 2, \"\");\nenum E : int;\nvoid g(void) { enum E : short y; (void)y; }\n"
                  "enum F { FA };\nenum F : int z;\ntypedef enum T : char TT;\n_Static_assert(sizeof(TT) == 1, \"\");\n",
        .warnings = 2,
        .errors = 7,
        .notes = 1,
    },
    {
        .name = "an enum base with a declarator demoted",
        .source = "enum P : unsigned const long volatile x;\nvoid f(void) { enum S : volatile char s; s = 1; (void)s; }\nenum Q : short q1, q2;\n"
                  "enum Q { QA = 1 };\n_Static_assert(sizeof(q1) == 2, \"\");\nenum E : int;\nvoid g(void) { enum E : short y; (void)y; }\n"
                  "enum F { FA };\nenum F : int z;\ntypedef enum T : char TT;\n_Static_assert(sizeof(TT) == 1, \"\");\n",
        .options = { "-Wno-error=elaborated-enum-base", "-Werror=underlying-cv-qualifier-ignored" },
        .warnings = 6,
        .errors = 3,
        .notes = 1,
    },
    {
        .name = "an enum base with a declarator ignored",
        .source = "enum P : unsigned const long volatile x;\nvoid f(void) { enum S : volatile char s; s = 1; (void)s; }\nenum Q : short q1, q2;\n"
                  "enum Q { QA = 1 };\n_Static_assert(sizeof(q1) == 2, \"\");\nenum E : int;\nvoid g(void) { enum E : short y; (void)y; }\n"
                  "enum F { FA };\nenum F : int z;\ntypedef enum T : char TT;\n_Static_assert(sizeof(TT) == 1, \"\");\n",
        .options = { "-Wno-elaborated-enum-base", "-w" },
        .errors = 1,
        .notes = 1,
    },
    {
        .name = "an enum base with a declarator under -w",
        .source = "enum P : unsigned const long volatile x;\nvoid f(void) { enum S : volatile char s; s = 1; (void)s; }\nenum Q : short q1, q2;\n"
                  "enum Q { QA = 1 };\n_Static_assert(sizeof(q1) == 2, \"\");\nenum E : int;\nvoid g(void) { enum E : short y; (void)y; }\n"
                  "enum F { FA };\nenum F : int z;\ntypedef enum T : char TT;\n_Static_assert(sizeof(TT) == 1, \"\");\n",
        .options = { "-w" },
        .errors = 7,
        .notes = 1,
    },
    {
        // An object left an array of unknown bound by its tentative definitions (#2238): clang's
        // -Wtentative-definition-array once per object, at its last tentative definition, in the order of its first one,
        // before the unit's unused warnings; none for a defined or completed one, an extern one, or under the pragma.
        .name = "tentative array definitions",
        .source = "int a[];\nint a[];\nint b[]; int b[3];\nstatic int c[];\nextern int d[];\nint e[], f[];\nstatic void u(void) {}\n"
                  "typedef int A[];\nA x;\nint q[3]; int q[];\nint r[]; int r[2] = { 1 };\nint s[] = { 1, 2 }; int s[];\n"
                  "int t[]; extern int t[];\nint *p[];\nconst int k[];\nint m[][2];\n"
                  "void g(void) { extern int z[]; static int y[2]; (void)z; (void)y; }\n"
                  "#pragma clang diagnostic ignored \"-Wtentative-definition-array\"\nint i[];\n",
        .options = { "-Wall" },
        .warnings = 11,
        .compare_warning_text = true,
    },
    {
        .name = "tentative array definitions in C23",
        .source = "int a[];\nint a[];\nint b[]; int b[3];\nstatic int c[];\nextern int d[];\nint e[], f[];\nstatic void u(void) {}\n"
                  "typedef int A[];\nA x;\nint q[3]; int q[];\nint r[]; int r[2] = { 1 };\nint s[] = { 1, 2 }; int s[];\n"
                  "int t[]; extern int t[];\nint *p[];\nconst int k[];\nint m[][2];\n"
                  "void g(void) { extern int z[]; static int y[2]; (void)z; (void)y; }\n"
                  "#pragma clang diagnostic ignored \"-Wtentative-definition-array\"\nint i[];\n",
        .options = { "-Wall" },
        .warnings = 11,
        .c23 = true,
        .compare_warning_text = true,
    },
    {
        .name = "tentative array definitions under -w",
        .source = "int a[];\nint a[];\nint b[]; int b[3];\nstatic int c[];\nextern int d[];\nint e[], f[];\nstatic void u(void) {}\n"
                  "typedef int A[];\nA x;\nint q[3]; int q[];\nint r[]; int r[2] = { 1 };\nint s[] = { 1, 2 }; int s[];\n"
                  "int t[]; extern int t[];\nint *p[];\nconst int k[];\nint m[][2];\n"
                  "void g(void) { extern int z[]; static int y[2]; (void)z; (void)y; }\n"
                  "#pragma clang diagnostic ignored \"-Wtentative-definition-array\"\nint i[];\n",
        .options = { "-w" },
    },
    {
        .name = "tentative array definitions as errors",
        .source = "int a[];\nint b[];\n",
        .options = { "-Werror=tentative-definition-array" },
        .errors = 2,
    },
    {
        .name = "tentative array definitions disabled",
        .source = "int a[];\n",
        .options = { "-Wno-tentative-definition-array" },
    },
    {
        // Clang's loop over the tentative definitions has no error test: a rejected unit warns too, a definition after
        // the error included.
        .name = "tentative array definitions in a rejected unit",
        .source = "int a[];\nint z[];\nint a[];\nint f(void) { return undeclared; }\nint w[];\nint g(void) { return 1 / 0; }\n",
        .options = { "-Wall" },
        .warnings = 4,
        .errors = 1,
    },
    {
        // The unit's end in clang's order (#2238): -Wundefined-internal, the tentative arrays, the unused declarations;
        // under -Werror the first promoted one leaves out the unused ones.
        .name = "unit end order",
        .source = "static int c;\nstatic void h(void);\nvoid k(void) { h(); }\nint a[];\nstatic void u(void) {}\n",
        .options = { "-Wall" },
        .warnings = 4,
        .notes = 1,
        .compare_warning_text = true,
    },
    {
        // A promoted graph warning, published after the reader's, is an error before -Wundefined-internal (#2238).
        .name = "unit end after a promoted graph warning",
        .source = "static void h(void);\nvoid k(void) { h(); (void)(1 / 0); }\nstatic int c;\n",
        .options = { "-Wall" },
        .warnings = 3,
        .notes = 1,
        .compare_warning_text = true,
    },
    {
        // A block's promoted unused variable is an error before the unit's end (#2238).
        .name = "unit end after a promoted scope warning",
        .source = "void f(void) { int x; }\nstatic int c;\nstatic void g(void) {}\n",
        .options = { "-Wall" },
        .warnings = 3,
        .compare_warning_text = true,
    },
    {
        .name = "duplicate declaration specifier group disabled",
        .source = "const const int value;\n",
        .options = { "-Wno-duplicate-decl-specifier" },
    },
    {
        .name = "duplicate declaration specifier source policy",
        .source = "#pragma clang diagnostic ignored \"-Wduplicate-decl-specifier\"\nconst const int value;\n",
    },
    {
        .name = "noreturn fall off",
        .source = "_Noreturn void f(void){}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn explicit return",
        .source = "_Noreturn void f(void){return;}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn valued return",
        .source = "__attribute__((noreturn)) int f(void){return 1;}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn dead explicit return",
        .source = "_Noreturn void f(void){if(0)return;for(;;){}}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn ordinary call can return",
        .source = "void g(void);_Noreturn void f(void){g();}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn call ends the path",
        .source = "_Noreturn void g(void);_Noreturn void f(void){g();}\n",
    },
    {
        .name = "noreturn partial terminal path",
        .source = "_Noreturn void g(void);_Noreturn void f(int x){if(x)g();}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn trap ends the path",
        .source = "_Noreturn void f(void){__builtin_trap();}\n",
    },
    {
        .name = "noreturn cast terminal path",
        .source = "_Noreturn int g(void);_Noreturn void f(void){(void)g();}\n",
    },
    {
        .name = "noreturn partial cast terminal path",
        .source = "_Noreturn int g(void);_Noreturn void f(int x){if(x)(void)g();}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn partial trap path",
        .source = "_Noreturn void f(int x){if(x)__builtin_trap();}\n",
        .warnings = 1,
    },
    {
        .name = "noreturn independent of return type warning policy",
        .source = "_Noreturn void f(void){}\n",
        .options = { "-Wno-return-type" },
        .warnings = 1,
    },
    {
        .name = "noreturn group disabled",
        .source = "_Noreturn void f(void){return;}\n",
        .options = { "-Wno-invalid-noreturn" },
    },
    {
        .name = "noreturn source policy",
        .source = "#pragma clang diagnostic ignored \"-Winvalid-noreturn\"\n_Noreturn void f(void){}\n",
    },
    {
        .name = "scalar return fall off after ordinary call",
        .source = "void g(void);int f(void){g();}\n",
        .warnings = 1,
    },
    {
        .name = "scalar return partial terminal path",
        .source = "_Noreturn void g(void);int f(int x){if(x)g();}\n",
        .warnings = 1,
    },
    {
        .name = "scalar return terminal call",
        .source = "_Noreturn int g(void);int f(void){(void)g();}\n",
    },
    {
        .name = "scalar return partial trap path",
        .source = "int f(int x){if(x)__builtin_trap();}\n",
        .warnings = 1,
    },
    {
        .name = "private extern declarations",
        .source = "__private_extern__ int x;\n__private_extern__ int f(void);\nvoid g(void){__private_extern__ int y;}\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "private extern definitions",
        .source = "__private_extern__ int x=7;\n__private_extern__ int f(void){return x;}\n",
    },
    {
        .name = "private extern direct initializer after attribute",
        .source = "__private_extern__ int x __attribute__((unused)) (3);\n",
    },
    {
        .name = "private extern thread local declaration",
        .source = "__private_extern__ __thread int x;\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "private extern redeclarations",
        .source = "__private_extern__ int x;int x;\nint y;__private_extern__ int y;\n__private_extern__ int z;int z=3;\n",
        .warnings = 3,
        .notes = 3,
    },
    {
        .name = "private extern multiple declarators",
        .source = "__private_extern__ int x,y=2,z;\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "private extern duplicate specifier",
        .source = "__private_extern__ __private_extern__ int x;\n",
        .warnings = 2,
        .notes = 1,
    },
    {
        .name = "private extern group disabled",
        .source = "__private_extern__ int x;\n",
        .options = { "-Wno-private-extern" },
    },
    {
        .name = "private extern source policy",
        .source = "#pragma clang diagnostic ignored \"-Wprivate-extern\"\n__private_extern__ int x;\n",
    },
    {
        .name = "first block annotation survives its own later bare declaration",
        .source = "void declare(void){extern int printf(const char*,...)__attribute__((format(printf,1,2)));"
                  "extern int printf(const char*,...);}\nint printf(const char*,...);\n"
                  "void call(void){printf(\"%d\",\"str\");}\n",
        .options = { "-fno-builtin" },
        .warnings = 1,
    },
    {
        .name = "first bare block extern does not export a later block annotation",
        .source = "void declare(void){extern int printf(const char*,...);"
                  "extern int printf(const char*,...)__attribute__((format(printf,1,2)));"
                  "extern int printf(const char*,...);}\nint printf(const char*,...);\n"
                  "void call(void){printf(\"%d\",\"str\");}\n",
        .options = { "-fno-builtin" },
    },
    {
        .name = "all supported explicit format builtins keep their source argument indexes",
        .source = "typedef struct _IO_FILE FILE;\nvoid f(FILE*p,char*b){"
                  "__builtin_printf(\"%d\",\"str\");__builtin_fprintf(p,\"%d\",\"str\");"
                  "__builtin_sprintf(b,\"%d\",\"str\");__builtin_snprintf(b,16,\"%d\",\"str\");"
                  "__builtin_snprintf(b,16,\"%s.mo\\0\",\"x\");}\n",
        .options = { "-ffreestanding", "-fno-builtin" },
        .warnings = 5,
    },
    {
        .name = "explicit format receipts follow interleaved unrelated intrinsic calls",
        .source = "void f(const char*s){(void)__builtin_strlen(s);__builtin_printf(\"%d\",\"str\");(void)__builtin_strlen(s);}\n",
        .options = { "-fno-builtin" },
        .warnings = 1,
    },
    {
        .name = "first block extern format contract reaches the first file declaration",
        .source = "void declare(void){extern int printf(const char*,...)__attribute__((format(printf,1,2)));}\n"
                  "int printf(const char*,...);\nvoid call(void){printf(\"%d\",\"str\");}\n",
        .options = { "-fno-builtin" },
        .warnings = 1,
    },
    {
        .name = "later file declaration does not import a nested outer redeclaration annotation",
        .source = "int printf(const char*,...);\n"
                  "void declare(void){extern int printf(const char*,...)__attribute__((format(printf,1,2)));}\n"
                  "int printf(const char*,...);\nvoid call(void){printf(\"%d\",\"str\");}\n",
        .options = { "-fno-builtin" },
    },
    {
        .name = "middle format annotation survives a final bare declaration",
        .source = "int printf(const char*,...);\nint printf(const char*,...)__attribute__((format(printf,1,2)));\n"
                  "int printf(const char*,...);\nvoid f(void){printf(\"%d\",\"str\");}\n",
        .options = { "-fno-builtin" },
        .warnings = 1,
    },
    {
        .name = "later format annotation does not change an earlier call",
        .source = "int report(const char*,...);\nvoid before(void){report(\"%d\",\"str\");}\n"
                  "int report(const char*,...)__attribute__((format(printf,1,2)));\nint report(const char*,...);\n"
                  "void after(void){report(\"%d\",\"str\");}\n",
        .warnings = 1,
    },
    {
        .name = "block format inheritance follows the visible declaration",
        .source = "int report(const char*,...);\nvoid f(void){{"
                  "int report(const char*,...)__attribute__((format(printf,1,2)));"
                  "int report(const char*,...);report(\"%d\",\"str\");}"
                  "report(\"%d\",\"str\");}\n",
        .warnings = 1,
    },
    {
        .name = "bare format inheritance preserves unrelated declaration attributes",
        .source = "int report(const char*,...)__attribute__((format(printf,1,2)));\n"
                  "int report(const char*,...)__attribute__((noinline));\nvoid f(void){report(\"%d\",\"str\");}\n",
        .warnings = 1,
    },
    {
        .name = "hosted plain format names retain inferred checks",
        .source = check_driver_format_plain_source,
        .warnings = 5,
    },
    {
        .name = "freestanding plain format names have no inferred contract",
        .source = check_driver_format_plain_source,
        .options = { "-ffreestanding" },
    },
    {
        .name = "global no builtin disables plain format inference",
        .source = check_driver_format_plain_source,
        .options = { "-fno-builtin" },
    },
    {
        .name = "per name exclusions disable all four supported format names",
        .source = check_driver_format_plain_source,
        .options = { "-fno-builtin-printf", "-fno-builtin-fprintf", "-fno-builtin-sprintf", "-fno-builtin-snprintf" },
    },
    {
        .name = "printf exclusion preserves the other names",
        .source = check_driver_format_plain_source,
        .options = { "-fno-builtin-printf" },
        .warnings = 4,
    },
    {
        .name = "snprintf exclusion disables its embedded NUL check too",
        .source = check_driver_format_plain_source,
        .options = { "-fno-builtin-snprintf" },
        .warnings = 3,
    },
    {
        .name = "builtin exclusions match the whole name",
        .source = check_driver_format_plain_source,
        .options = { "-fno-builtin-print" },
        .warnings = 5,
    },
    {
        .name = "later builtin option restores plain format inference",
        .source = check_driver_format_plain_source,
        .options = { "-fno-builtin", "-fbuiltin" },
        .warnings = 5,
    },
    {
        .name = "later no builtin option disables plain format inference",
        .source = check_driver_format_plain_source,
        .options = { "-fbuiltin", "-fno-builtin" },
    },
    {
        .name = "freestanding preserves explicit and redeclared format contracts",
        .source = check_driver_format_attributed_source,
        .options = { "-ffreestanding" },
        .warnings = 3,
    },
    {
        .name = "global no builtin preserves explicit and redeclared format contracts",
        .source = check_driver_format_attributed_source,
        .options = { "-fno-builtin" },
        .warnings = 3,
    },
    {
        .name = "per name exclusions preserve explicit and redeclared format contracts",
        .source = check_driver_format_attributed_source,
        .options = { "-fno-builtin-printf", "-fno-builtin-snprintf" },
        .warnings = 3,
    },
    {
        .name = "plain dprintf needs an explicit format contract",
        .source = "int dprintf(int,const char*,...);\nvoid f(void){dprintf(1,\"%d\",\"str\");}\n",
    },
    {
        .name = "attributed dprintf remains checked when builtins are disabled",
        .source = "int dprintf(int,const char*,...)__attribute__((format(printf,2,3)));\nvoid f(void){dprintf(1,\"%d\",\"str\");}\n",
        .options = { "-ffreestanding", "-fno-builtin", "-fno-builtin-dprintf" },
        .warnings = 1,
    },
    {
        .name = "explicit builtin printf remains checked when builtins are disabled",
        .source = "void f(void){__builtin_printf(\"%d\",\"str\");}\n",
        .options = { "-ffreestanding", "-fno-builtin", "-fno-builtin-printf" },
        .warnings = 1,
    },
    {
        .name = "local printf function pointer has no inferred format contract",
        .source = "void f(void(*printf)(const char*,...)){printf(\"%d\",\"str\");}\n",
    },
    {
        .name = "valid plain format calls retain a quiet twin",
        .source = "int printf(const char*,...);\nint snprintf(char*,unsigned long,const char*,...);\nvoid f(char*b){printf(\"%d\",7);snprintf(b,16,\"%s.mo\",\"x\");}\n",
    },
    {
        .name = "one parenthesized comparison does not hide the other warning",
        .source = "int f(int a,int b,int c,int d){return ((a<b) & c<d)+(a==b ^ (c!=d));}\n",
        .warnings = 2,
        .notes = 4,
    },
    {
        .name = "parenthesized eager chain leaves outer precedence visible",
        .source = "int f(int a,int b,int c,int d,int e,int g){return (a<b & c<d) ^ e<g;}\n",
        .warnings = 1,
        .notes = 2,
    },
    {
        .name = "constant initializer comparison pairs",
        .source = "int a=1<2 & 3<4; static int b=1==2 ^ 3!=4; int f(void){return a+b;}\n",
    },
    {
        .name = "constant initializer single comparisons",
        .source = "int a=1<2 & 3; static int b=1 ^ 2!=3; int f(void){return a+b;}\n",
        .warnings = 2,
        .notes = 4,
    },
    {
        .name = "enum comparison pairs",
        .source = "enum E{A=1<2 & 3<4,B=1==2 ^ 3!=4};int f(void){return A+B;}\n",
    },
    {
        .name = "enum single comparisons",
        .source = "enum E{A=1<2 & 3,B=1 ^ 2!=3};int f(void){return A+B;}\n",
        .warnings = 2,
        .notes = 4,
    },
    {
        .name = "unevaluated comparison pairs",
        .source = "unsigned long f(int a,int b,int c,int d){return sizeof(a<b & c<d);}\n",
    },
    {
        .name = "unevaluated single comparison",
        .source = "unsigned long f(int a,int b,int c){return sizeof(a<b & c);}\n",
        .warnings = 1,
        .notes = 2,
    },
    {
        .name = "unselected generic comparison pair",
        .source = "int f(int a,int b,int c,int d){return _Generic(0,int:1,default:a<b ^ c<d);}\n",
    },
    {
        .name = "unselected generic single comparison",
        .source = "int f(int a,int b,int c){return _Generic(0,int:1,default:a<b ^ c);}\n",
        .warnings = 1,
        .notes = 2,
    },
    {
        .name = "two relational operands of each bitwise operator",
        .source = "int f(int a,int b,int c,int d){return (a<b & c<d)+(a>b | c>d)+(a<=b ^ c>=d);}\n",
    },
    {
        .name = "two equality operands of each bitwise operator",
        .source = "int f(int a,int b,int c,int d){return (a==b & c!=d)+(a!=b | c==d)+(a==b ^ c!=d);}\n",
    },
    {
        .name = "mixed equality and relational operands",
        .source = "int f(int a,int b,int c,int d){return (a==b & c<d)+(a>=b | c!=d)+(a!=b ^ c<=d);}\n",
    },
    {
        .name = "eager chains of three comparisons",
        .source = "int f(int a,int b,int c,int d,int e,int g){return (a<b ^ c<d ^ e<g)+(a==b & c==d & e==g)+(a!=b | c!=d | e!=g);}\n",
    },
    {
        .name = "mixed eager bitwise chains",
        .source = "int f(int a,int b,int c,int d,int e,int g){return (a<b & c<d ^ e<g)+(a==b ^ c==d | e==g);}\n",
    },
    {
        .name = "bare bitwise operand suppresses comparison precedence",
        .source = "int f(int a,int b,int c,int d){return (a&b ^ c<d)+(a<b | c^d);}\n",
    },
    {
        .name = "parenthesized comparisons are explicit",
        .source = "int f(int a,int b,int c,int d){return ((a<b) & (c<d))+((a==b)^c)+(a|(b!=c));}\n",
    },
    {
        .name = "relational left operand still warns once per operator",
        .source = "int f(int a,int b,int c){return (a<b & c)+(a<=b | c)+(a>b ^ c);}\n",
        .warnings = 3,
        .notes = 6,
    },
    {
        .name = "relational right operand still warns once per operator",
        .source = "int f(int a,int b,int c){return (a & b>=c)+(a | b<c)+(a ^ b<=c);}\n",
        .warnings = 3,
        .notes = 6,
    },
    {
        .name = "equality left operand still warns once per operator",
        .source = "int f(int a,int b,int c){return (a==b & c)+(a!=b | c)+(a==b ^ c);}\n",
        .warnings = 3,
        .notes = 6,
    },
    {
        .name = "equality right operand still warns once per operator",
        .source = "int f(int a,int b,int c){return (a & b!=c)+(a | b==c)+(a ^ b!=c);}\n",
        .warnings = 3,
        .notes = 6,
    },
    {
        .name = "parenthesized bitwise operand does not suppress warning",
        .source = "int f(int a,int b,int c,int d){return ((a&b) ^ c<d)+(a<b | (c^d));}\n",
        .warnings = 2,
        .notes = 4,
    },
    {
        .name = "nondecimal literal conversion bit patterns and lossy controls",
        .source = "#define X 0xe2\n"
                  "signed char take(signed char x) { return x; }\n"
                  "signed char small(void) { return (0xff); }\n"
                  "signed char large(void) { return 0x100; }\n"
                  "signed char single=0xff, overflow=0x1ff;\n"
                  "signed char values[8]; short s;\n"
                  "void f(void) { values[0]=0xe2; values[1]=(0342); values[2]=0b11100010; values[3]=X; s=0xffff; take(0x80); }\n"
                  "void hex_loss(void) { values[4]=0x100; }\n"
                  "void dec_loss(void) { values[5]=226; }\n"
                  "void calc_loss(void) { values[6]=0xe2+0; }\n"
                  "void unary_loss(void) { values[7]=+0xe2; }\n"
                  "void short_loss(void) { s=65535; }\n"
                  "void call_loss(void) { take(0x180); }\n",
        .warnings = 8,
        .c23 = true,
    },
    {
        .name = "nondecimal conversion with unsigned plain char",
        .source = "char c; signed char s; short h; void f(void) { c=0xe2; s=(0Xff); h=0177777; s=0x100; }\n",
        .options = { "-funsigned-char" },
        .warnings = 1,
    },
    {
        // Clang points at the cast, while the existing checker points at its literal. Compare the complete
        // warning text and severity, apart from that independent location discrepancy.
        .name = "explicit cast does not receive literal exemption",
        .source = "signed char s; void f(void) { s=(int)0xff; }\n",
        .options = { "-funsigned-char" },
        .warnings = 1,
        .compare_warning_text = true,
    },
    {
        .name = "nonliteral integer zero pointer conversion",
        .source = "int*p=1-1;\n",
        .warnings = 1,
    },
    {
        .name = "overflowing double literal",
        .source = "double value=1e400;\n",
        .warnings = 1,
    },
    {
        .name = "underflowing float literal",
        .source = "float value=1e-99f;\n",
        .warnings = 1,
    },
    {
        .name = "ordered pointer integer comparison",
        .source = "int f(int*p){return p<1;}\n",
        .warnings = 1,
    },
    {
        .name = "ordinary declaration declares nothing",
        .source = "int;\n",
        .warnings = 1,
    },
    {
        .name = "untagged record declares nothing",
        .source = "struct{int x;};\n",
        .warnings = 1,
    },
    {
        .name = "plain complex has an inferred element type",
        .source = "_Complex value;\n",
        .warnings = 1,
    },
    {
        .name = "member declaration declares nothing",
        .source = "struct S{int;int x;};\n",
        .warnings = 1,
    },
    {
        .name = "untagged enum member declares its enumerators",
        .source = "struct S{enum{E};};union U{enum{A};};struct T{struct{enum{B};};};int main(void){return E+A+B;}\n",
    },
    {
        .name = "anonymous union member declares a member",
        .source = "struct S{union{int x;};int y;};int main(void){struct S s;s.x=1;return s.x;}\n",
    },
    {
        .name = "named tag member declares nothing",
        .source = "struct S{struct Named{int x;};};\n",
        .warnings = 1,
    },
    {
        .name = "typedef without a declarator requires a name",
        .source = "typedef int;\n",
        .warnings = 1,
    },
    {
        .name = "typedef of a named record without a declarator requires a name",
        .source = "typedef struct S{int x;};\n",
        .warnings = 1,
    },
    {
        .name = "typedef of a named enum without a declarator requires a name",
        .source = "typedef enum E{A};\n",
        .warnings = 1,
    },
    {
        .name = "extern on a tag declaration is ignored",
        .source = "extern struct S;\n",
        .warnings = 1,
    },
    {
        .name = "static on a tag declaration is ignored",
        .source = "static union U;\n",
        .warnings = 1,
    },
    {
        .name = "const on a tag declaration is ignored",
        .source = "const struct S;\n",
        .warnings = 1,
    },
    {
        .name = "qualified ordinary declaration declares nothing",
        .source = "const int;\n",
        .warnings = 1,
    },
    {
        .name = "flexible record member is not last",
        .source = "struct S{int n;int a[];};struct P{struct S s;int after;};\n",
        .warnings = 1,
    },
    {
        .name = "external inline contains modifiable static local",
        .source = "inline int f(void){static int value;return value;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "leading attribute keeps the inline note at the declaration",
        .source = "__attribute__((unused)) inline int g(void){static int value;return value;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "leading extension keeps the inline note after it",
        .source = "__extension__ inline int g(void){static int value;return value;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "trailing attribute keeps the inline note at its first declaration",
        .source = "inline int g(void) __attribute__((unused));\ninline int g(void){static int value;return value;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "used internal function has no definition",
        .source = "static int missing(void);int f(void){return missing();}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "evaluated sizeof VLA bound use precedes a later call",
        .source = "static int m(void);void f(void){int a[sizeof(int[m()])];a[0]=m();(void)a;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "evaluated sizeof VLA bound use is a use alone",
        .source = "static int m(void);void f(void){int a[sizeof(int[m()])];(void)a;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "evaluated sizeof VLA bound use in an addition",
        .source = "static int m(void);int f(void){return sizeof(int[m()])+1;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "evaluated alignof VLA bound use",
        .source = "static int m(void);int f(void){return _Alignof(int[m()]);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "evaluated conditional VLA bound use",
        .source = "static int m(void);int f(void){return sizeof(int[1?m():2]);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "evaluated comma VLA bound use",
        .source = "static int m(void);int f(int n){return sizeof(int[(m(),n)]);}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "undefined batch follows first use and survives its own promotion",
        .source = "static int first(void);static int second(void);int f(void){return second()+first();}\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "earlier promoted literal error suppresses final undefined batch",
        .source = "static int missing(void);int f(void){return missing();}double value=1e400;\n",
        .warnings = 2,
        .notes = 1,
    },
    {
        .name = "mapped sparse error retains same body return analysis",
        .source = "struct S{long a,b;};struct S f(int n){int*p=1-1;(void)p;if(n)return(struct S){1,2};}\n",
        .options = { "-Werror=non-literal-null-conversion" },
        .warnings = 1,
        .errors = 1,
    },
    {
        .name = "pre C23 definition omits parameter name",
        .source = "int f(int){return 0;}\n",
        .warnings = 1,
    },
    {
        .name = "literal zero pointer conversion is quiet",
        .source = "int*p=0;\n",
    },
    {
        .name = "nonzero subnormal literal is quiet",
        .source = "float value=1e-45f;\n",
    },
    {
        .name = "exact literal zero with a small exponent is quiet",
        .source = "float value=0e-9999f;\n",
    },
    {
        .name = "explicit complex element type is quiet",
        .source = "_Complex double value;\n",
    },
    {
        .name = "terminal flexible record member is quiet",
        .source = "struct S{int n;int a[];};struct P{int before;struct S s;};\n",
    },
    {
        .name = "external inline constant static local is quiet",
        .source = "inline int f(void){static const int value=1;return value;}\n",
    },
    {
        .name = "later internal definition discharges the warning",
        .source = "static int missing(void);int f(void){return missing();}static int missing(void){return 1;}\n",
    },
    {
        .name = "unevaluated internal function reference is quiet",
        .source = "static int missing(void);unsigned long value=sizeof missing;\n",
    },
    {
        .name = "C23 definition may omit parameter name",
        .source = "int f(int){return 0;}\n",
        .c23 = true,
    },
    {
        // clang's extra `;` of attribute lists alone (#1447): only the C23 lists warn, whatever the GNU lists' order.
        .name = "attribute-only declaration warns only for its C23 lists",
        .source = "[[gnu::unused]] __attribute__((unused));\n"
                  "[[gnu::unused]] __attribute__((unused)) [[gnu::unused]] __attribute__((unused));\n"
                  "__attribute__((unused)) [[gnu::unused]] __attribute__((aligned(8)));\n"
                  "[[gnu::unused]] __attribute__((deprecated, packed)) [[gnu::unused]];\n"
                  "[[gnu::unused]] __attribute__((unknown_gnu));\n"
                  "[[unknown_c23]] __attribute__((unknown_gnu));\n",
        .warnings = 8,
        .c23 = true,
    },
    {
        .name = "attribute-only declaration's GNU lists stay quiet without ignored-attribute warnings",
        .source = "[[gnu::unused]] __attribute__((unused));\n[[unknown_c23]] __attribute__((unknown_gnu));\n",
        .options = { "-Wno-ignored-attributes" },
        .warnings = 1,
        .c23 = true,
    },
    {
        .name = "null conversion source push pop retains named policy",
        .source = "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wnon-literal-null-conversion\"\n"
                  "int*quiet=1-1;\n#pragma clang diagnostic pop\nint*loud=2-2;\n",
        .warnings = 1,
    },
    {
        .name = "empty declaration source error is promoted",
        .source = "#pragma clang diagnostic error \"-Wmissing-declarations\"\nint;\n",
        .errors = 1,
    },
    {
        .name = "unnamed parameter source suppression is scoped",
        .source = "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wc23-extensions\"\n"
                  "int quiet(int){return 0;}\n#pragma clang diagnostic pop\nint loud(int){return 0;}\n",
        .warnings = 1,
    },
    {
        .name = "named no error overrides global warning promotion",
        .source = "int*p=1-1;\n",
        .options = { "-Werror", "-Wno-error=non-literal-null-conversion" },
        .warnings = 1,
    },
    {
        .name = "everything does not restore an explicitly disabled diagnostic",
        .source = "int*p=1-1;\n",
        .options = { "-Wno-everything", "-Weverything", "-Wno-missing-variable-declarations" },
    },
    {
        .name = "later broad suppression disables a named diagnostic",
        .source = "int*p=1-1;\n",
        .options = { "-Wnon-literal-null-conversion", "-Wno-everything" },
    },
    {
        .name = "later named diagnostic overrides broad suppression",
        .source = "int*p=1-1;\n",
        .options = { "-Wno-everything", "-Wnon-literal-null-conversion" },
        .warnings = 1,
    },
    {
        .name = "everything retains a named suppression",
        .source = "int*p=1-1;\n",
        .options = { "-Wno-non-literal-null-conversion", "-Weverything", "-Wno-missing-variable-declarations" },
    },
    {
        .name = "system warning families are suppressed by default",
        .source = "# 1 \"system-warning-705.h\" 3\nint*p=1-1;\ndouble value=1e400;\nint;\nint f(int){return 0;}\n"
                  "# 10 \"user-warning-705.c\"\nint ordinary;\n",
    },
    {
        .name = "system warning opt in enables sparse families",
        .source = "# 1 \"system-warning-705.h\" 3\nint*p=1-1;\ndouble value=1e400;\nint;\nint f(int){return 0;}\n"
                  "# 10 \"user-warning-705.c\"\nint ordinary;\n",
        .options = { "-Wsystem-headers" },
        .warnings = 4,
    },
    {
        .name = "user expansion of a system macro retains diagnostic notes",
        .source = "# 1 \"system-warning-705.h\" 3\n#define ZERO (1-1)\n# 10 \"user-warning-705.c\"\nint*p=ZERO;\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        // An indirection through a pointer to void (#147, -Wvoid-ptr-dereference): its operand's type as written (a typedef
        // kept, top-level qualifiers dropped), in dead code too; not right after a unary `&` nor in sizeof's or alignof's
        // operand.
        .name = "void pointer dereferences",
        .source = "typedef void *VP;\nvoid f(void *p, VP v, const void *c, void *const k) {\n"
                  "  (void)*p; (void)*v; (void)*c; (void)*k; void *q = &*p; q = &(*p); q = & *p;\n"
                  "  (void)sizeof(*p); (void)_Alignof(*p); (void)__alignof__ *p; (void)(1 ? *p : *p);\n"
                  "  if (0) (void)*p; (void)**(void **)p; (void)q;\n}\n",
        .warnings = 10,
    },
    {
        // A dereference of a null pointer constant (#1447, -Wnull-dereference): read or assigned (a compound assignment's
        // too, a cast to void's operand too), through casts (a void pointer's, a constant expression's), with clang's note;
        // not its address, a member of it, its increment, a volatile one, a pointer variable's, a subscript, sizeof's
        // operand, nor unreachable code.
        .name = "null pointer dereferences",
        .source = "struct S { int a; };\nint g(int);\nint f(int n) {\n"
                  "  *(int *)0 = 1; *(int *)0 += 2; int x = *(int *)0; g(*(int *)0); int k = n ? *(int *)0 : 1;\n"
                  "  int c = *(const int *)(0); int l = *(int *)(1 - 1); int t = *(char *)(void *)0L;\n"
                  "  int m = (*(struct S *)0).a; int *q = &*(int *)0; (*(int *)0)++; *(volatile int *)0 = 3;\n"
                  "  int *p = 0; *p = 4; int y = ((int *)0)[0]; int s = sizeof(*(int *)0); ((struct S *)0)->a = 2;\n"
                  "  if (0) { int z = *(int *)0; (void)z; }\n"
                  "  (void)*(int *)0;\n"
                  "  return x + k + c + l + t + m + y + s + (q != 0);\n  *(int *)0 = 9;\n}\n",
        .warnings = 9,
        .notes = 9,
    },
    {
        // A nullptr_t operand is a null pointer constant too (#1447, C23): `nullptr` and a nullptr_t variable through casts,
        // not a volatile target.
        .name = "null pointer dereferences of nullptr",
        .source = "typedef typeof(nullptr) np;\nint g(np n) {\n"
                  "  int a = *(int *)n; int b = *(int *)(nullptr); int c = *(volatile int *)nullptr; int d = *(int *)(void *)nullptr;\n"
                  "  *(int *)nullptr = 1;\n"
                  "  return a + b + c + d;\n}\n",
        .c23 = true,
        .warnings = 4,
        .notes = 4,
    },
    {
        // Unreachable initializers (#1447): after a return, in a constant false branch and in a `while (0)`'s body,
        // clang's runtime behavior's warnings (array bounds, division by zero, null dereference) stay silent.
        .name = "runtime warnings skip unreachable initializers",
        .source = "int a[2];\nint f(int n) {\n"
                  "  if (0) { int d = a[5]; int e = n / 0; int z = *(int *)0; (void)d; (void)e; (void)z; }\n"
                  "  while (0) { int d = a[5]; int e = n / 0; int z = *(int *)0; (void)d; (void)e; (void)z; }\n"
                  "  return n;\n"
                  "  { int d = a[5]; int e = n / 0; int z = *(int *)0; (void)d; (void)e; (void)z; }\n}\n",
    },
    {
        // An unreachable initializer's barriers outlive its body's scratch (#1447): the next body's marks and queue
        // must not reuse them, which made a later body's initialized variables "uninitialized when used here".
        .name = "unreachable initializer barriers outlive their body",
        .source = "int f(int n) {\n  return n;\n  int dead = n;\n  return dead;\n}\n"
                  "int g(int n) {\n  int a = n + 1;\n  if (n > 1) n = a;\n  int b = n + 2;\n  if (n > 2) n = b;\n"
                  "  int c = n + 3;\n  if (n > 3) n = c;\n  int d = n + 4;\n  if (n > 4) n = d;\n  return n;\n}\n",
        .options = { "-Wall" },
    },
    {
        // A compound literal's unnamed object is no variable for -Wuninitialized (#1447): its own value, at its closing
        // `}`, is no read within its own initialization. Scalar, pointer, array and struct literals in initializers and
        // assignments, at file scope, in loops, under `&` and nested, say nothing.
        .name = "compound literals are no variables for -Wuninitialized",
        .source = "struct S { int x; int y; };\nstruct T { int n; int *p; };\n"
                  "int *gp = &(int){ 4 };\nstruct S *gs = &(struct S){ .x = 3 };\nint *gq = (int[]){ 5, 6 };\n"
                  "int scalars(void) {\n  int e = (int){ 4 };\n  long l;\n  l = (long){ 5 };\n  int *q;\n  q = (int *){ 0 };\n"
                  "  return e + (int)l + (q != 0) + *gp + gs->x + gq[1];\n}\n"
                  "int aggregates(void) {\n  int a[2] = { (int){ 1 }, (int){ 2 } };\n  int z = { (int){ 3 } };\n"
                  "  struct S s = (struct S){ .x = 1 };\n  struct S m = { .x = (int){ 1 } };\n  int *p = (int[2]){ 1, 2 };\n"
                  "  s = (struct S){ .y = 2 };\n  return a[0] + z + s.x + m.x + p[1];\n}\n"
                  "int loops(int n) {\n  int total = 0;\n"
                  "  for (int i = 0; i < n; i += 1) { int e = (int){ i }; total += e + (int){ 2 }; }\n"
                  "  while (n > 0) { int d = (int){ n }; total += d; n -= 1; }\n  return total;\n}\n"
                  "int addresses(void) {\n  int *p = &(int){ 4 };\n  int **pp = &(int *){ &(int){ 5 } };\n  return *p + **pp;\n}\n"
                  "int nested(void) {\n  int e = (int){ (int){ 4 } };\n  struct T t = (struct T){ .n = (int){ 1 }, .p = &(int){ 2 } };\n"
                  "  int a = (int){ (int){ (int){ 3 } } + 1 };\n  return e + t.n + *t.p + a;\n}\n",
        .options = { "-Wall" },
    },
    {
        // The variables a compound literal's initializer reads keep their warnings (#1447): an uninitialized one is used
        // here, directly or through a nested literal or an assignment's, and the declared variable itself inside its
        // own initializer's literal, braced or not, is used within its own initialization.
        .name = "compound literal initializers keep their variables' uninitialized reads",
        .source = "int reads(void) {\n  int u;\n  int e = (int){ u };\n  return e;\n}\n"
                  "int self(void) {\n  int x = (int){ x };\n  return x;\n}\n"
                  "int nested(void) {\n  int u;\n  int v;\n  int e = (int){ (int){ u } + v };\n  return e;\n}\n"
                  "int assigned(void) {\n  int x;\n  x = (int){ x };\n  return x;\n}\n"
                  "int braced(void) {\n  int y = { (int){ y } };\n  return y;\n}\n"
                  "int pointer(void) {\n  int *p = (int *){ p };\n  return p != 0;\n}\n",
        .options = { "-Wall" },
        .warnings = 7,
        .notes = 4,
    },
    {
        // A logical operator one operand decides though the other isn't known (#1447, clang's CFG tryEvaluateBool):
        // `x && 0` and `x || 1`, through `!` and casts, prune the arm they leave out for the runtime warnings and for
        // -Wreturn-type alike; `x && (x || 1)` and `x && 1` decide nothing.
        .name = "short-circuit conditions one operand decides",
        .source = "int a[2];\nint f(int n) {\n"
                  "  if (n && 0) { n = n / 0; }\n"
                  "  if (!(n || 1)) { n = a[5]; }\n"
                  "  if ((long)(n || 1)) { n += 1; } else { n = n % 0; }\n"
                  "  while (n && 0) { n = *(int *)0; }\n"
                  "  if (n && (n || 1)) { n = n / 0; }\n"
                  "  return n;\n}\n"
                  "int g(int n) {\n  if (n || 1) return 1;\n}\n"
                  "int h(int n) {\n  while (n || 1) { if (n) return 2; }\n}\n"
                  "int k(int n) {\n  if (n && 1) return 1;\n}\n",
        .warnings = 2,
    },
    {
        // The same conditions prune a conditional's arm and a logical operator's right operand (#1447, clang's CFG builder
        // in VisitConditionalOperator and VisitLogicalOperator): `n && 1` and `n || 0` decide nothing.
        .name = "short-circuit operands one operand decides",
        .source = "int a[2];\nint f(int n) {\n"
                  "  int r = (n && 0) ? n / 0 : (n || 1) ? 1 : *(int *)0;\n"
                  "  r += ((n && 0) && n % 0) + ((n || 1) || a[5]) + (!(n || 1) ? a[5] : 0) + ((long)(n && 0) ? *(int *)0 : 1);\n"
                  "  r += (n && 1) ? n / 0 : 0;\n"
                  "  r += (n || 0) || *(int *)0;\n"
                  "  return r;\n}\n",
        .warnings = 2,
        .notes = 1,
    },
    {
        // Clang decides by the context its parser is in (#147): an unselected _Generic association or choose_expr operand in
        // an alignof's operand is unevaluated with it, as are a _Generic's controlling expression and a statement
        // expression in sizeof's; an array declarator's bound, in a sizeof's type name too, is evaluated; and a sizeof of a
        // variable array's element evaluates its operand.
        .name = "void pointer dereferences by context",
        .source = "void *p; int i;\nunsigned long f(int n) {\n  char v[n][n]; int w[(*p, 3)];\n"
                  "  return _Alignof(_Generic(i, int: i, default: *p)) + _Alignof(__builtin_choose_expr(0, i, *p)) + _Generic(*p, void: 1, default: 2)\n"
                  "    + sizeof(({ (void)*p; 1; })) + sizeof(char (*)[(*p, n)]) + sizeof(int[(*p, n)]) + sizeof(v[(*p, 0)]) + sizeof(w);\n}\n"
                  "void g(int a[(*p, 3)]) { (void)a; }\n",
        .warnings = 12,
    },
    {
        // A file-scope declaration of attribute lists and `__extension__`s alone is clang's extra `;` (#1447), which says
        // nothing but its C23 lists' ignored-attribute warnings; GNU attributes after an `__extension__` are specifiers,
        // which declare nothing. A declaration's leading attribute lists, C23's and GNU's in any order, go on past its
        // `__extension__`s.
        .name = "file-scope extension markers after attributes",
        .source = "__extension__;\n__attribute__((unused)) __extension__;\nint a; __extension__ __extension__;\n"
                  "__extension__ __attribute__((unused));\n__attribute__((unused)) [[gnu::unused]] __extension__ int b;\n"
                  "__attribute__((unused)) [[gnu::unused]] __extension__;\n[[gnu::unused]] __extension__ __extension__;\n",
        .warnings = 3,
        .c23 = true,
    },
    {
        // `__extension__`s before a static assertion (#1447), at file scope and in a block: the assertion, checked.
        .name = "extension markers before static assertions",
        .source = "__extension__ _Static_assert(1, \"\");\n__extension__ _Static_assert(0, \"file\");\n"
                  "void f(void) { __extension__ __extension__ _Static_assert(0, \"block\"); }\n",
        .errors = 2,
    },
    {
        // An assignment to a const variable, through an array's subscripts or a record's members to it, or through a
        // pointer to a const record (#147): the variable named with its declared type and the "declared const here" note at
        // its name; a pointer's target is the generic error.
        .name = "const variable assignments",
        .source = "typedef const int CI;\nstruct s { int a; };\nvoid f(const int p, const struct s *q) {\n  const int x = 1; x = 2; x++; p = 3;\n"
                  "  const int a[2] = {0}; a[0] = 1; const int m[2][2] = {0}; m[0][1] = 1;\n"
                  "  CI y = 1; y = 2; const struct s v = {0}; v.a = 1; q->a = 2;\n}\nvoid g(const int *r) { r[0] = 1; *r = 2; }\n",
        .errors = 10,
        .notes = 8,
    },
    {
        // Arithmetic on a pointer to an incomplete type (#147): clang's message of the pointee as written, a subscript's at
        // its `[`, and the "forward declaration" note at the tag's declaring name.
        .name = "incomplete pointer arithmetic",
        .source = "struct s; union u; enum e; typedef struct s S;\nvoid f(struct s *p, struct s *q, union u *w, enum e *n, S *t) {\n"
                  "  (void)(p + 1); (void)(1 + p); (void)(p - q); p++; --p; p += 2; (void)p[1]; (void)1[p];\n"
                  "  (void)(w + 1); (void)(n - 1); (void)(t + 1);\n}\n",
        .errors = 11,
        .notes = 11,
    },
    {
        .name = "independent bodies retain source order at requested lanes",
        .source = "int*f0(void){return 1-1;}\nint*f1(void){return 2-2;}\nint*f2(void){return 3-3;}\n",
        .warnings = 3,
    },
    {
        // A GNU statement expression in an unevaluated operand (#147): its statements are the graph's roots, but clang drops
        // the unused-result and runtime-behavior warnings in sizeof's, _Alignof's, __alignof__'s and typeof's operands
        // (check_common_unevaluated). None of these statement expressions has a side effect, which clang's
        // -Wunevaluated-expression would report.
        .name = "statement expression results in sizeof operands",
        .source = "int *q;\nunsigned long g(void) { return sizeof(({ *q; 1; })); }\nunsigned long k(void) { return sizeof(({ 1 + 2; 1; })); }\n",
    },
    {
        .name = "unused results in unevaluated statement expressions",
        .source = "int *q;\nstruct S { int m; } *ps, vs;\nunsigned long f(int x) {\n"
                  "    return sizeof(({ x == 1; x != 1; x < 1; 1; })) + sizeof(({ (0, x); (void *)q; ps->m; vs.m; 1; })) +\n"
                  "           sizeof(({ x ? 1 : 2; ({ 3; 4; }); 5; })) + sizeof(x ? ({ 1; 2; }) : 3) + sizeof (({ *q; 1; })) +\n"
                  "           __alignof__(({ *q; x == 1; (void *)q; 1; })) + _Alignof(({ 1; 2; }));\n}\n",
        .warnings = 1,
    },
    {
        .name = "runtime behavior warnings in unevaluated statement expressions",
        .source = "int a[2];\nunsigned u;\nint s;\nunsigned long f(int x) {\n"
                  "    return sizeof(({ x / 0; 1; })) + sizeof(({ x % 0; 1; })) + sizeof(({ a[5]; 1; })) + sizeof(({ char c = 300; c; })) +\n"
                  "           sizeof(({ u < s; 1; })) + sizeof(({ int b[2]; b[2]; })) + __alignof__(({ a[2]; x / 0; 1; }));\n}\n",
        .options = { "-Wsign-compare" },
    },
    {
        // A statement expression whose value is unevaluated has no node evaluated either (check_common_region_evaluation):
        // -Wuninitialized reads none of its reads and writes. `sizeof ({ ... })`'s `(` is the statement expression's own.
        .name = "uninitialized variables in unevaluated statement expressions",
        .source = "int f1(void) { int u; (void)sizeof(({ u = 1; })); return u; }\n"
                  "int f2(void) { int u; (void)sizeof(({ u; })); u = 2; return u; }\n"
                  "int f3(int n) { int u; (void)(n ? sizeof(({ u; })) : 0); return 0; }\n"
                  "int f4(void) { int u; (void)({ u = 1; }); return u; }\n"
                  "int f5(void) { int u; return (int)sizeof ({ u; }); }\n"
                  "int f6(void) { int u; return (int)sizeof ({ int v = u; v; }) + (int)__alignof__(({ u; })); }\n"
                  "int f8(void) { int u; return _Generic(1, long: ({ u; }), default: 0); }\n"
                  "int f9(int n) { return sizeof ({ n; }) + sizeof({ (char)n; }) + sizeof ({ int k = n; k; }) * 2; }\n"
                  "int f10(int n) { int u; return (int)sizeof(({ ({ u; }); })) + (int)sizeof(({ (void)sizeof(({ u = 1; })); 1; })) + u; }\n",
        .options = { "-Wall", "-Wno-unevaluated-expression" },
        .warnings = 2,
        .notes = 2,
    },
    {
        // A statement expression whose value is unevaluated has no node evaluated either (check_common_region_evaluation):
        // -Wuninitialized reads none of its reads and writes. `sizeof ({ ... })`'s `(` is the statement expression's own.
        .name = "uninitialized variables in unevaluated statement expressions under -Wextra in C23",
        .source = "int f1(void) { int u; (void)sizeof(({ u = 1; })); return u; }\n"
                  "int f2(void) { int u; (void)sizeof(({ u; })); u = 2; return u; }\n"
                  "int f3(int n) { int u; (void)(n ? sizeof(({ u; })) : 0); return 0; }\n"
                  "int f4(void) { int u; (void)({ u = 1; }); return u; }\n"
                  "int f5(void) { int u; return (int)sizeof ({ u; }); }\n"
                  "int f6(void) { int u; return (int)sizeof ({ int v = u; v; }) + (int)__alignof__(({ u; })); }\n"
                  "int f8(void) { int u; return _Generic(1, long: ({ u; }), default: 0); }\n"
                  "int f9(int n) { return sizeof ({ n; }) + sizeof({ (char)n; }) + sizeof ({ int k = n; k; }) * 2; }\n"
                  "int f10(int n) { int u; return (int)sizeof(({ ({ u; }); })) + (int)sizeof(({ (void)sizeof(({ u = 1; })); 1; })) + u; }\n",
        .options = { "-Wall", "-Wextra", "-Wno-unevaluated-expression" },
        .warnings = 3,
        .notes = 2,
        .c23 = true,
    },
    {
        // A statement expression in a static selection's chosen operand is evaluated where the selection is: its
        // statements, roots of their own, are left out of the selection's unselected roots (#2023), so -Wuninitialized
        // reads them and an initializer's -Wconstant-conversion is given; nested in another selection's other operand,
        // they stay unselected.
        .name = "statement expressions in a chosen selection operand are evaluated",
        .source = "int f1(void) { int u; return _Generic(1, int: ({ u; }), default: 0); }\n"
                  "int f2(void) { int u; return __builtin_choose_expr(1, ({ u; }), 0); }\n"
                  "int f3(void) { return _Generic(1, int: ({ char c = 302; c; }), default: 0); }\n"
                  "int f4(void) { int u; return _Generic(1, int: _Generic(2, long: 0, default: ({ u; })), default: 0); }\n"
                  "int f5(void) { int u; return __builtin_choose_expr(0, _Generic(1, int: ({ u; })), 0); }\n"
                  "int f6(int x) { return _Generic(1, int: ({ x == 1; 0; }), default: 0); }\n"
                  "int f7(void) { int u; return _Generic(1, long: ({ u; }), int: ({ int v = 1; v; })); }\n",
        .options = { "-Wall" },
        .warnings = 5,
        .notes = 4,
    },
    {
        // A discarded statement expression's comparison value warns as clang's DiagnoseUnusedExprResult of the statement
        // expression does (#1447): "expression result unused", no comparison's note, queued after the statements before it,
        // in its own -Wunused-value rather than -Wunused-comparison; a statement's own comparison keeps its warning.
        .name = "a discarded statement expression's comparison value",
        .source = "int g(int);\n"
                  "int f(int x) {\n"
                  "    ({ x; x == 1; });\n"
                  "    ({ l: x == 2; });\n"
                  "    (({ x == 3; }), 0);\n"
                  "    ({ ({ x != 4; }); });\n"
                  "    ({ x > 5; }), x;\n"
                  "    if (x) ({ x == 6; });\n"
                  "    ({ g(x) == 7; });\n"
                  "    ({ (x == 8); });\n"
                  "    x == 9;\n"
                  "    return 0;\n"
                  "}\n",
        .warnings = 11,
        .notes = 1,
    },
    {
        .name = "a discarded statement expression's comparison value under -Wno-unused-comparison in C23",
        .source = "int g(int);\n"
                  "int f(int x) {\n"
                  "    ({ x; x == 1; });\n"
                  "    ({ l: x == 2; });\n"
                  "    (({ x == 3; }), 0);\n"
                  "    ({ ({ x != 4; }); });\n"
                  "    ({ x > 5; }), x;\n"
                  "    if (x) ({ x == 6; });\n"
                  "    ({ g(x) == 7; });\n"
                  "    ({ (x == 8); });\n"
                  "    x == 9;\n"
                  "    return 0;\n"
                  "}\n",
        .options = { "-Wno-unused-comparison" },
        .warnings = 10,
        .c23 = true,
    },
    {
        .name = "typeof statement expressions are unevaluated",
        .source = "int *q;\nvoid f(int x) {\n    __typeof__(({ *q; x == 1; x = x; x / 0; 1; })) t = 0;\n"
                  "    typeof(({ (0, x); (void *)q; 1; })) v = 0;\n    (void)t;\n    (void)v;\n}\n",
        .options = { "-Wself-assign" },
    },
    {
        // The evaluated controls: a statement expression outside the operand, sizeof's own discarded result, and array
        // bounds, which clang evaluates as constant expressions even in a sizeof's type name or in a declaration inside its
        // statement expression, until a nested sizeof makes them unevaluated again.
        .name = "evaluated statement expressions and array bounds keep their warnings",
        .source = "int *q;\nunsigned long f(int x) {\n    ({ *q; 1; });\n    sizeof(({ 1; 2; }));\n"
                  "    unsigned long n = sizeof(int[({ x == 1; 2; })]) + sizeof(({ int w[({ 3; 4; })]; 5; }));\n"
                  "    return n + ({ x < 1; 6; }) + sizeof(int[sizeof(({ 7; 8; }))]);\n}\n",
        .warnings = 6,
        .notes = 1,
    },
    {
        // A sizeof of an expression of variable array type evaluates it after all (clang's TransformToPotentiallyEvaluated),
        // which the contexts mark TRANSFORMED; a statement expression whose value decays to a pointer is no variable array,
        // and stays unevaluated. (A statement expression inside a variable array's operand isn't supported yet.)
        .name = "a statement expression decaying from a variable array stays unevaluated",
        .source = "void g(int n) {\n    char vv[n][n];\n    (void)sizeof(({ 3; vv[0]; }));\n    (void)sizeof(({ 4; vv; }));\n}\n",
    },
    {
        // -Wsign-compare's operands keep their declared typedef's spelling (#1100): a parameter's typedef, a cast's,
        // and sizeof's `__size_t`, each with clang's (aka) of what the sugar hides; the same sugar survives the
        // operands' shared-type arithmetic.
        .name = "sign-compare operands keep their typedef spelling",
        .source = "typedef int wchar_t;\ntypedef unsigned long size_t;\ntypedef int myint;\ntypedef signed char tiny;\n"
                  "int s1(wchar_t wc) { return wc < 0xffU; }\n"
                  "int s2(int n) { int a[4]; return n >= sizeof a / sizeof a[0]; }\n"
                  "int s3(int n, size_t m) { return n < m; }\n"
                  "int s4(myint n, unsigned u) { return n < u; }\n"
                  "int s5(int n, size_t m) { return n < m + 1; }\n"
                  "int s6(int n, size_t m) { return n < 1 + m; }\n"
                  "int s7(int n, size_t m) { return n < (m >> 1); }\n"
                  "int s8(int n, size_t m, unsigned long u) { return n < m + u; }\n"
                  "int s9(int n) { return n < (size_t)1; }\n"
                  "int s10(tiny x, tiny y, unsigned u) { return x + y < u; }\n",
        .options = { "-Wsign-compare" },
        .warnings = 10,
    },
    {
        // A format argument keeps its declared typedef's spelling (#1100), with the (aka) of what it hides.
        .name = "format arguments keep their typedef spelling",
        .source = "typedef unsigned long size_t;\ntypedef long ptrdiff_t;\ntypedef int wchar_t;\ntypedef int myint;\n"
                  "int printf(const char*,...);\n"
                  "void f(size_t m, ptrdiff_t d, wchar_t *w, myint n) { printf(\"%u\", m); printf(\"%u\", d); printf(\"%d\", w); printf(\"%lu\", -n); printf(\"%lu\", +n); printf(\"%lu\", ~n); }\n",
        .options = { "-Wformat" },
        .warnings = 6,
    },
    {
        // A fixed-parameter conversion must not transfer the source cast's typedef to the converted pointer.
        .name = "format fixed conversion does not borrow source typedef",
        .source = "typedef _BitInt(32) T;\nvoid f(const char*, _BitInt(32)*) __attribute__((format(printf,1,2)));\n"
                  "void g(void) { f(\"%d\", (T)0); }\n",
        .options = { "-Wformat", "-Wno-gcc-compat", "-Wno-non-literal-null-conversion" },
        .warnings = 1,
    },
    {
        // A for clause's value in parentheses is the clause's (#1447): clang's IgnoreParens finds the discarded comma,
        // its operand of no effect and its literal 0 idiom through them, in the init and the increment; the condition's
        // value is read.
        .name = "parenthesized comma for clauses discard their values",
        .source = "int g(int);\nvoid f(int n, int *p) {\n    int i = 0, j = 0;\n    for ((i, j); i < n; ++i) {}\n"
                  "    for (; ; (i++, j)) { break; }\n    for (((i, j)); ; ) { break; }\n    for ((j, i++); ; ((i, 2))) { break; }\n"
                  "    for (; (i, j); ) { break; }\n    for ((i == 1, j == 2); ; (i < 2)) { break; }\n"
                  "    for ((i == 1); ; ((i < 2))) { break; }\n    for ((g(1), j); ; (j, (void)0)) { break; }\n"
                  "    for ((i, 0); ; (j, 0)) { break; }\n    for ((void)(i, j); ; ) { break; }\n    for ((p[0], (j)); ; ) { break; }\n}\n",
        .warnings = 14,
        .notes = 1,
    },
    {
        // Clang's Sema gives -Wself-assign as it builds each assignment, so it sorts among the statement's other warnings
        // by place, in unreachable code and in a conditional's or a _Generic's unselected arm too, and before the condition's
        // -Wparentheses at one assignment; types other than int's are spelled.
        .name = "self-assignments in source order among other warnings",
        .source = "void f(int x) { x = x; if (x); }\nvoid g(int x) { x = x; int y = 1 << 40; (void)y; }\n"
                  "void h(int x) { if (0) x = x; return; x = x; }\nvoid k(int x) { x = x; if (x); int y = 0 ? (x = x) : 1; (void)y; }\n"
                  "int m(int x) { return (x = x) + _Generic(x, int: 1, default: (x = x)); }\n"
                  "float fl(float x) { x = x; return x; }\nint *pt(int *p) { p = p; return p; }\nvoid q(int x) { if (x = x) return; }\n",
        .options = { "-Wself-assign" },
        .warnings = 15,
        .notes = 4,
    },
    {
        // __float128 literals (#1250): each value its binary128 bits', printed at 35 digits, a -0's sign too (as every
        // format's), and the format's own limits.
        .name = "__float128 literal conversions and limits",
        .source = "int d = 1.5q;\n_Bool b = 0.1q;\n_Bool t = 0x1p-16494q;\n_Bool z = -0.0q;\nunsigned u = -1.5q;\nint i = 1e40q;\n"
                  "short s = 70000.5q;\nlong long l = 9007199254740993.5q;\n__int128 e = 12345678901234567890123.0q;\n__int128 f = -0.1q;\n"
                  "__float128 big = 1e5000q;\n__float128 small = 1e-5000q;\n__float128 tiny = 0x1p-16495q;\n__float128 rounded = 0x1.8p-16495q;\n"
                  "_Bool zd = -0.0;\nint g(void) { int x = 2.75q; int y = -0.0L; return x + y; }\n",
        .warnings = 15,
    },
    {
        // Clang's ActOnFinishFunctionBody gives a function's unused parameters after its body scope's unused variables
        // and before its missing return.
        .name = "unused parameters after the body's unused variables",
        .source = "int f(int y, int z) { int w; { int q; } if (z) return 1; }\nint g(int y) { int a; return 0; }\n"
                  "void h(int p, int r) { int u; (void)r; }\n",
        .options = { "-Wall", "-Wextra" },
        .warnings = 8,
    },
    {
        // Files that line markers enter (#1954): each diagnostic in one prints clang's "In file included from" chain of the
        // markers' lines when it differs from the last one's, nested, after a flag 2 returns, and for a system header's
        // (silent here) and a user file's entered from the same presumed line; the main file's, the preprocessor's error
        // after them, none.
        .name = "include stacks of files line markers enter",
        .source = "# 1 \"lm_main.c\"\n# 1 \"outer.h\" 1\n# 1 \"inner.h\" 1\nint f(void) { return 1 << 40; }\n"
                  "int f2(void) { return 1 << 41; }\n# 2 \"outer.h\" 2\nint g(void) { return 1 << 33; }\n"
                  "# 3 \"lm_main.c\" 2\nint h(void) { return 1 << 34; }\n# 1 \"again.h\" 1 3 4\nint k(void) { return 1 << 35; }\n"
                  "# 4 \"lm_main.c\" 2\n# 1 \"user.h\" 1\nint m(void) { return 1 << 36; }\n# 5 \"lm_main.c\" 2\n#error done\n",
        .warnings = 5,
        .errors = 1,
    },
    {
        // Clang takes the overflow builtins of _BitInts past 128 bits (#1447) but mul_overflow's of a signed one, as an
        // operand or the destination, at that argument, the first of them alone; an unsigned one's product, and a signed
        // one's sum and difference, are taken.
        .name = "mul_overflow of a signed _BitInt past 128 bits",
        .source = "typedef _BitInt(200) S; typedef unsigned _BitInt(200) U;\n"
                  "int f1(S a, S b, S *r) { return __builtin_mul_overflow(a, b, r); }\n"
                  "int f2(int a, int b, S *r) { return __builtin_mul_overflow(a, b, r); }\n"
                  "int f3(U a, U b, U *r) { return __builtin_mul_overflow(a, b, r); }\n"
                  "int f4(U a, S b, U *r) { return __builtin_mul_overflow(a, b, r); }\n"
                  "int f5(U a, int b, U *r) { return __builtin_mul_overflow(a, b, r); }\n"
                  "int f6(_BitInt(129) a, int b, int *r) { return __builtin_mul_overflow(a, (b), r); }\n"
                  "int f7(__int128 a, __int128 b, _BitInt(256) *r) { return __builtin_mul_overflow(a, b, r); }\n"
                  "int f8(S a, S b, S *r) { return __builtin_add_overflow(a, b, r) + __builtin_sub_overflow(a, b, r); }\n"
                  "int f9(unsigned _BitInt(129) a, U *r) { return __builtin_mul_overflow(a, 3, r); }\n",
        .errors = 5,
    },
    {
        // A tag a prototype's list declares first (#1447, clang's ActOnTag): -Wvisibility at its name (an unnamed enum's
        // keyword), in a definition's list, a nested prototype's (not one the outer list declares already), a member's, a
        // block's and a bound's, a definition shadowing a file-scope tag; a list's redefinition, with its note, and an
        // enum's, which is an unnamed one's too. Not one declared outside, nor an unnamed record.
        .name = "tags declared in a prototype's list",
        .source = "struct S6;\nvoid f1(struct S1 *p);\nvoid f2(struct S2 { int a; } *p);\nvoid f3(union U3 *p);\n"
                  "void f4(enum E4 { A4 } e);\nvoid f5(enum { A5 } e, enum : long { B5 } f);\nvoid f6(struct S6 *p);\n"
                  "void f7(struct S7 *p) { (void)p; }\nint f8(struct S8 (*fp)(struct S9 *q));\nvoid f9(struct S10 *p, int (*g)(struct S10 *));\n"
                  "void (*f10)(struct S11 *);\nvoid f11(void) { void g(struct S12 *); }\nvoid f12(struct { int a; } *p, struct A { struct B *b; } *a);\n"
                  "struct V { void (*fp)(struct U *); };\nstruct T { int b; };\nvoid g1(struct T *p, struct T { int c; } *q);\n"
                  "int g2(struct R { int x; } a, struct R { int y; } b);\nint g3(enum F { B1 } a, enum F { B2 } b);\n"
                  "int g4(int (*a)[sizeof(struct Q { int q; })]);\n",
        .warnings = 22,
        .notes = 2,
    },
    {
        // C23's definitions there are no warning: a compatible definition outside may redefine them.
        .name = "tags declared in a prototype's list under C23",
        .source = "struct S6;\nvoid f1(struct S1 *p);\nvoid f2(struct S2 { int a; } *p);\nvoid f3(union U3 *p);\n"
                  "void f4(enum E4 { A4 } e);\nvoid f5(enum { A5 } e, enum : long { B5 } f);\nvoid f6(struct S6 *p);\n"
                  "void f7(struct S7 *p) { (void)p; }\nint f8(struct S8 (*fp)(struct S9 *q));\nvoid f9(struct S10 *p, int (*g)(struct S10 *));\n"
                  "void (*f10)(struct S11 *);\nvoid f11(void) { void g(struct S12 *); }\nvoid f12(struct { int a; } *p, struct A { struct B *b; } *a);\n"
                  "struct V { void (*fp)(struct U *); };\nstruct T { int b; };\nvoid g1(struct T *p, struct T { int c; } *q);\n"
                  "int g4(int (*a)[sizeof(struct Q { int q; })]);\n",
        .warnings = 10,
        .c23 = true,
    },
    {
        // -Wvisibility's policy: its option and a source pragma.
        .name = "prototype tag visibility policy",
        .source = "void f1(struct S1 *p);\n#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wvisibility\"\n"
                  "void f2(struct S2 *p);\n#pragma clang diagnostic pop\nvoid f3(struct S3 *p, struct S3 { int a; } *q);\n",
        .options = { "-Wno-visibility" },
    },
    {
        .name = "prototype tag visibility pragma",
        .source = "void f1(struct S1 *p);\n#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wvisibility\"\n"
                  "void f2(struct S2 *p);\n#pragma clang diagnostic pop\nint f3(struct S3 { int a; } p, struct S3 { int b; } q);\n",
        .warnings = 3,
        .notes = 1,
    },
    {
        // C23 records redefined with other attributes (#1447): clang's -Wodr is an error by default; ignored, the
        // definitions are compatible, and the name keeps the first's type (its layout, which the assertions read).
        .name = "odr attribute differences ignored",
        .source = "struct S { char c; int a; } __attribute__((packed));\nstruct S { char c; int a; };\n"
                  "_Static_assert(sizeof(struct S) == 5, \"first\");\n"
                  "struct U { char c; int a __attribute__((aligned(16))); };\nstruct U { char c; int a; };\n"
                  "_Static_assert(sizeof(struct U) == 32, \"first u\");\nstruct S s;\nstruct U u;\n",
        .options = { "-Wno-odr" },
        .c23 = true,
    },
    {
        // Demoted, each is a warning with its note at the attribute the earlier list has.
        .name = "odr attribute differences demoted",
        .source = "struct S { char c; int a; } __attribute__((packed));\nstruct S { char c; int a; };\n"
                  "_Static_assert(sizeof(struct S) == 5, \"first\");\n"
                  "struct U { char c; int a __attribute__((aligned(16))); };\nstruct U { char c; int a; };\n"
                  "_Static_assert(sizeof(struct U) == 32, \"first u\");\nstruct S s;\nstruct U u;\n",
        .options = { "-Wno-error=odr" },
        .warnings = 2,
        .notes = 2,
        .c23 = true,
    },
    {
        // Source pragmas: ignored for one, a warning for another (one -Werror leaves), and the default error after them.
        .name = "odr attribute differences under pragmas",
        .source = "struct S { char c; int a; } __attribute__((packed));\n#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wodr\"\n"
                  "struct S { char c; int a; };\n#pragma clang diagnostic pop\n_Static_assert(sizeof(struct S) == 5, \"first\");\n"
                  "struct U { char c; int a __attribute__((aligned(16))); };\n#pragma clang diagnostic push\n#pragma clang diagnostic warning \"-Wodr\"\n"
                  "struct U { char c; int a; };\n#pragma clang diagnostic pop\n_Static_assert(sizeof(struct U) == 32, \"first u\");\n"
                  "struct V { int a; } __attribute__((aligned(8)));\nstruct V { int a; } __attribute__((aligned(16)));\n",
        .warnings = 1,
        .errors = 1,
        .notes = 3,
        .c23 = true,
    },
    {
        // In a system header (here a line marker's), clang ignores it, error or not, without -Wsystem-headers; the name
        // keeps the first definition's type.
        .name = "odr attribute differences in a system header",
        .source = "# 1 \"odr_system.h\" 1 3\nstruct S { char c; int a; } __attribute__((packed));\nstruct S { char c; int a; };\n"
                  "# 4 \"odr_main.c\" 2\n_Static_assert(sizeof(struct S) == 5, \"first\");\nstruct S s;\n",
        .c23 = true,
    },
    {
        // A C23 parameter list's compatible redefinition of its own record (#1869) declares the first's type: the body
        // reads both parameters and a pointer to it as one type, compiled on every path.
        .name = "C23 parameter list redefines its record compatibly",
        .source = "int g(struct T { int x; } a, struct T { int x; } b, struct T *c) { return a.x + b.x + c->x; }\n"
                  "int n(struct W { int a; } *p, int (*q)(struct W { int a; } *r)) { return p->a + (q != 0); }\n",
        .c23 = true,
    },
    {
        // `#pragma pack`'s own warnings (#1849): clang's parser's for malformed directives, at the token they name or past
        // the line's last, and Sema's at the directive (a bad value, a name and a value to pop, an empty stack's pop,
        // show's value: the default's 8, a pack value, options align's).
        .name = "pragma pack syntax and Sema warnings",
        .source = "#pragma pack(3)\n#pragma pack(push, r, 4)\n#pragma pack(pop, r, 2)\n#pragma pack(pop, nosuch)\n#pragma pack\n"
                  "#pragma pack(foo)\n#pragma pack(push, 4) extra\n#pragma pack(pop, 32)\n#pragma pack(push, 4\n#pragma pack(push, r, x)\n"
                  "#pragma pack(+)\n#pragma pack(show)\n#pragma pack(2)\n#pragma pack(show)\n#pragma options align=packed\n#pragma pack(show)\n"
                  "#pragma pack(pop)\n#pragma pack(pop)\nstruct S { char c; int i; };\nint main(void) { return sizeof(struct S) != 6; }\n",
        .warnings = 14,
    },
    {
        // A push left open at the unit's end (#1849): one warning each, the innermost first, the innermost with clang's
        // note at the reset that left the default value.
        .name = "pragma pack pushes left open at the end",
        .source = "#pragma pack(push, 4)\n#pragma pack(push, inner, 1)\nstruct S { char c; int i; };\n#pragma pack()\n"
                  "int main(void) { return sizeof(struct S) != 5; }\n",
        .warnings = 2,
        .notes = 1,
    },
    {
        // The note of a push that no directive set a value after has no place (#1849), which the oracle's count of
        // located notes leaves out; the complete output compares it.
        .name = "pragma pack push left open without a value",
        .source = "#pragma pack(push)\nint main(void) { return 0; }\n",
        .warnings = 1,
    },
    {
        // Sema's show after a function comes after the function's own warnings (#1849): clang handles the annotation at
        // the top level.
        .name = "pragma pack show after a function's warnings",
        .source = "#pragma pack(push, 2)\nint main(void) { int x; return 0; }\n#pragma pack(show)\n#pragma pack(pop)\n",
        .options = { "-Wall" },
        .warnings = 2,
    },
    {
        // The unit's end in clang's Sema::ActOnEndOfTranslationUnit order (#2141): an unterminated pack push, #pragma
        // weak's undeclared identifier, an internal function used but not defined, then the unused file-scope
        // declarations, each after the function's own unused local.
        .name = "unit end warnings in clang's order",
        .source = "#pragma weak nothere\n#pragma pack(push, 4)\nstatic int unused(void) { return 0; }\nstatic int unused_var;\n"
                  "static void declared(void);\nint main(void) { int x; declared(); return 0; }\n",
        .options = { "-Wall" },
        .warnings = 6,
        .notes = 1,
    },
    {
        // Clang diagnoses the unused file-scope declarations only where no error occurred (#2141): under -Werror the
        // undefined internal function's error leaves them out, and alone they reject the unit themselves.
        .name = "unit end unused declarations after an error",
        .source = "static int unused(void) { return 0; }\nstatic int unused_var;\nstatic void declared(void);\nint main(void) { declared(); return 0; }\n",
        .options = { "-Wall" },
        .warnings = 3,
        .notes = 1,
    },
    {
        // An ignored attribute's warning that -Werror promotes is an error too, so clang's ActOnEndOfTranslationUnit gives
        // the unused file-scope variable no warning after it (#2232); without -Werror both warnings are given.
        .name = "unit end unused declarations after a promoted ignored attribute",
        .source = "void rel(int *p);\nstatic int q __attribute__((cleanup(rel)));\n",
        .options = { "-Wall" },
        .warnings = 2,
    },
    {
        // GCC's cast to a union (#2301, clang's CheckCStyleCast): from a member's unqualified type after the operand's
        // lvalue conversion (a qualified, an array's and a char's), -Wgnu-union-cast at the cast's `(` under -pedantic;
        // from no member's type the error "cast to union type from type 'T' not present in union", the operand's type
        // with its sugar; and a cast to a struct from another type "used type 'T' where arithmetic or pointer type is
        // required", at the `(` too.
        .name = "casts to unions",
        .source = "typedef short ms;\nunion U{int*p;long*q;char c;int i;};struct S{int a;};\n"
                  "int f(int n,const int k,int arr[2]){union U u=(union U)&n;union U v=(union U)arr;(void)(union U)(char)n;\n"
                  "(void)(union U)k;(void)v;return *u.p;}\n"
                  "int g(ms s,double d){(void)(union U)s;(void)(union U)d;(void)(struct S)s;return 0;}\n",
        .options = { "-pedantic" },
        .warnings = 4,
        .errors = 3,
    },
    {
        .name = "unit end unused declarations alone",
        .source = "static int unused(void) { return 0; }\nstatic int unused_var;\nint main(void) { return 0; }\n",
        .options = { "-Wall" },
        .warnings = 2,
    },    {
        // GNU's designators (#2202), as clang's parser warns of them: an array designation without its `=` (at the value) and
        // an old-style field designator (at its name), on by default, before the value's own conversion warnings; an array
        // range (at its `...`), -pedantic's; all -Wgnu-designator's, under -Wgnu, and a pragma's ignored region drops them.
        .name = "GNU designators by default",
        .source = check_driver_gnu_designator_source,
        .warnings = 11,
    },
    {
        .name = "GNU designators under -pedantic",
        .source = check_driver_gnu_designator_source,
        .options = { "-pedantic" },
        .warnings = 13,
    },
    {
        .name = "GNU designators under -pedantic-errors",
        .source = check_driver_gnu_designator_source,
        .options = { "-pedantic-errors" },
        .warnings = 2,
        .errors = 11,
    },
    {
        .name = "GNU designators under -Wno-gnu-designator",
        .source = check_driver_gnu_designator_source,
        .options = { "-Wno-gnu-designator" },
        .warnings = 2,
    },
    {
        .name = "GNU designators under -Wno-gnu -Wgnu-designator",
        .source = check_driver_gnu_designator_source,
        .options = { "-Wno-gnu", "-Wgnu-designator" },
        .warnings = 13,
    },
    {
        // Automatic const objects without an initializer (#2168): -Wdefault-const-init-var-unsafe of a const type, an
        // array's, a typedef's ('volatile CI[2]' (aka ...)), a pointer's; -Wdefault-const-init-field-unsafe of a struct with a
        // const member, nested, with its note; both of a const struct; none of a union, an array of structs, an extern or
        // an initialized one; static storage's off by default.
        .name = "default-initialized const objects",
        .source = "typedef const int CI;\n"
                  "typedef const volatile int CVI;\n"
                  "const int file_a;\n"
                  "static const int file_b;\n"
                  "extern const int file_c;\n"
                  "struct M { const int m; int x; };\n"
                  "struct N { struct M inner; int y; };\n"
                  "union U { const int u; float f; };\n"
                  "struct M file_m;\n"
                  "void f(void)\n"
                  "{\n"
                  "    const int a;\n"
                  "    const int b[2];\n"
                  "    const volatile int c;\n"
                  "    volatile CI d[2];\n"
                  "    CVI e;\n"
                  "    static const int s;\n"
                  "    extern const int ex;\n"
                  "    struct M m;\n"
                  "    struct N n;\n"
                  "    union U u;\n"
                  "    const struct M cm;\n"
                  "    struct M ms[2];\n"
                  "    const int ok = 1;\n"
                  "    register const int r;\n"
                  "    struct { const int z; } anon;\n"
                  "    const int *p;\n"
                  "    int *const q;\n"
                  "}\n",
        .warnings = 12,
        .notes = 4,
    },
    {
        // A struct's const member through a union member and an anonymous struct; none of a union itself.
        .name = "const members through unions and anonymous structs",
        .source = "struct M { const int m; };\n"
                  "union V { struct M s; int i; };\n"
                  "struct W { union V v; };\n"
                  "struct X { int a; struct { const int deep; }; };\n"
                  "void f(void) { union V v; struct W w; struct X x; }\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        // A bit-field, a const struct member, a typedef's; not a const array member nor a flexible array member; a
        // variable array's and an _Atomic's const objects.
        .name = "const members' kinds",
        .source = "struct A { const int a[2]; };\n"
                  "struct B { int n; const int fam[]; };\n"
                  "struct C { const int bf : 3; };\n"
                  "struct D { struct A arr[2]; };\n"
                  "struct E { const struct { int q; } s; };\n"
                  "typedef const int CI;\n"
                  "struct F { CI t; };\n"
                  "void f(void) { struct A a; struct B b; struct C c; struct D d; struct E e; struct F g; const struct A ca; struct A *p; }\n"
                  "void k(int n) { const int vla[n]; _Atomic const int at; const int ci = 1, cj; }\n",
        .warnings = 7,
        .notes = 3,
    },
    {
        // -Wc++-compat adds -Wdefault-const-init-var of static storage, and the unsafe ones' "and is incompatible with
        // C++".
        .name = "default-initialized const objects and -Wc++-compat",
        .source = "typedef const int CI;\n"
                  "typedef const volatile int CVI;\n"
                  "const int file_a;\n"
                  "static const int file_b;\n"
                  "extern const int file_c;\n"
                  "struct M { const int m; int x; };\n"
                  "struct N { struct M inner; int y; };\n"
                  "union U { const int u; float f; };\n"
                  "struct M file_m;\n"
                  "void f(void)\n"
                  "{\n"
                  "    const int a;\n"
                  "    const int b[2];\n"
                  "    const volatile int c;\n"
                  "    volatile CI d[2];\n"
                  "    CVI e;\n"
                  "    static const int s;\n"
                  "    extern const int ex;\n"
                  "    struct M m;\n"
                  "    struct N n;\n"
                  "    union U u;\n"
                  "    const struct M cm;\n"
                  "    struct M ms[2];\n"
                  "    const int ok = 1;\n"
                  "    register const int r;\n"
                  "    struct { const int z; } anon;\n"
                  "    const int *p;\n"
                  "    int *const q;\n"
                  "}\n",
        .options = { "-Wc++-compat" },
        .warnings = 15,
        .notes = 4,
    },
    {
        // A file scope's const object warns once, at the unit's end, at its last tentative definition, with its type
        // completed (`'const int[1]'`), after its tentative array's warning, in the order of their first tentative
        // definitions; none where a later declaration initializes it.
        .name = "tentative const definitions",
        .source = "const int a;\n"
                  "const int a;\n"
                  "int arr[];\n"
                  "extern const int x;\n"
                  "const int x;\n"
                  "const int b[];\n"
                  "const int c[];\n"
                  "const int c[3];\n"
                  "const int z;\n"
                  "const int z = 2;\n"
                  "static const int s;\n"
                  "const int t[];\n"
                  "void f(void) { int unused; }\n",
        .options = { "-Wall", "-Wdefault-const-init" },
        .warnings = 11,
    },
    {
        // The words follow -Wc++-compat's pragma, which holds the unsafe warnings too.
        .name = "-Wc++-compat by source pragma",
        .source = "#pragma clang diagnostic warning \"-Wc++-compat\"\n"
                  "void f(void) { const int a; }\n"
                  "#pragma clang diagnostic ignored \"-Wc++-compat\"\n"
                  "void g(void) { const int b; }\n",
        .warnings = 1,
    },
    {
        // A tentative definition's -Wdefault-const-init-var at the unit's end, after the undefined internal functions'
        // and before the unused declarations'.
        .name = "tentative definitions' const objects at the unit's end",
        .source = "#pragma pack(push, 4)\n"
                  "#pragma weak never_declared\n"
                  "static void undefined_internal(void);\n"
                  "static int unused_static;\n"
                  "const int tentative;\n"
                  "static const int tentative_static;\n"
                  "void use(void) { undefined_internal(); }\n",
        .options = { "-Wall", "-Wdefault-const-init", "-Wunused" },
        .warnings = 7,
        .notes = 1,
    },
};

static void check_test_driver_warning_fixtures(CheckTest* test, const char* clang, const char* executable, const char* directory,
                                               const CheckDriverDefaultWarningFixture* fixtures, uint32_t fixture_count)
{
    static const char* const policies[] = { NULL, "-Werror", "-w" };
    static const char* const lanes[] = { "1", "3" };
    // test-checker-driver reports this sweep's time with each run mode (#2071, check_driver_run_modes).
    const uint64_t began = check_test_driver_nanoseconds();
    char source[PATH_MAX];
    char objects[4][PATH_MAX];
    snprintf(source, sizeof(source), "%s/default-warning.c", directory);
    for (uint32_t route = 0; route < BUSTER_ARRAY_LENGTH(objects); route += 1)
    {
        snprintf(objects[route], sizeof(objects[route]), "%s/default-warning-%u.o", directory, route);
    }
    for (uint32_t f = 0; f < fixture_count; f += 1)
    {
        const CheckDriverDefaultWarningFixture* fixture = fixtures + f;
        const bool written = parse_test_write(source, fixture->source, strlen(fixture->source));
        bool mismatch_printed = false;
        check_test_expect(test, written, "original default-warning fixture is written");
        for (uint32_t policy = 0; written && policy < BUSTER_ARRAY_LENGTH(policies); policy += 1)
        {
            for (uint32_t mode = 0; mode < (fixture->syntax_only ? 1u : 2u); mode += 1)
            {
                char* arguments[20] = { (char*)clang, fixture->c23 ? "-std=c23" : "-std=gnu17", "-fno-caret-diagnostics", "-fno-color-diagnostics",
                                        mode == 0 ? "-fsyntax-only" : "-c", source, "-o", objects[2] };
                uint32_t count = 8;
                for (uint32_t option = 0; option < BUSTER_ARRAY_LENGTH(fixture->options); option += 1)
                {
                    if (fixture->options[option] != NULL)
                    {
                        arguments[count++] = (char*)fixture->options[option];
                    }
                }
                if (policies[policy] != NULL)
                {
                    arguments[count++] = (char*)policies[policy];
                }
                unlink(objects[2]);
                char* expected = NULL;
                unlink(objects[3]);
                const int oracle = check_test_driver_spawn(arguments, &expected);
                const CheckDriverDiagnosticCounts counts = check_test_driver_diagnostic_counts(expected);
                const bool oracle_ready = (oracle == 0 || oracle == 1) && expected != NULL;
                char description[256];
                snprintf(description, sizeof(description), "%s policy=%u mode=%u: independent Clang oracle", fixture->name, policy, mode);
                const bool oracle_matches = oracle_ready && (policy != 0 || (oracle == (fixture->errors != 0) && counts.warnings == fixture->warnings &&
                                             counts.errors == fixture->errors && counts.notes == fixture->notes));
                if (policy == 0 && !oracle_matches)
                {
                    fprintf(stderr, "warning fixture oracle mismatch: %s mode=%u\nexpected status=%d warnings=%u errors=%u notes=%u; "
                                    "actual status=%d warnings=%u errors=%u notes=%u\n%s\n",
                            fixture->name, mode, fixture->errors != 0, fixture->warnings, fixture->errors, fixture->notes,
                            oracle, counts.warnings, counts.errors, counts.notes, expected != NULL ? expected : "(no diagnostic output)");
                }
                check_test_expect(test, oracle_matches, description);
                for (uint32_t lane = 0; oracle_ready && lane < BUSTER_ARRAY_LENGTH(lanes); lane += 1)
                {
                    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
                    {
                        arguments[0] = (char*)(executable != NULL ? executable : "/proc/self/exe");
                        arguments[7] = objects[scalar];
                        arguments[count] = "--lanes";
                        arguments[count + 1] = (char*)lanes[lane];
                        arguments[count + 2] = scalar != 0 ? "--scalar" : NULL;
                        arguments[count + 3] = NULL;
                        unlink(objects[scalar]);
                        static const char sentinel[] = "preserved-warning-output";
                        const bool preserve = mode != 0 && oracle == 1;
                        const bool prior = !preserve || parse_test_write(objects[scalar], sentinel, sizeof(sentinel));
                        char* actual = NULL;
                        const int status = check_test_driver_spawn(arguments, &actual);
                        snprintf(description, sizeof(description), "%s policy=%u mode=%u lanes=%s %s: complete Clang diagnostics",
                                 fixture->name, policy, mode, lanes[lane], scalar != 0 ? "scalar" : "vector");
                        const char* expected_text = fixture->compare_warning_text ? strstr(expected, "warning: ") : NULL;
                        expected_text = fixture->compare_warning_text && expected_text == NULL ? strstr(expected, "error: ") : expected_text;
                        const char* actual_text = fixture->compare_warning_text && expected_text != NULL ?
                                                  strstr(actual != NULL ? actual : "", policy == 1 ? "error: " : "warning: ") : NULL;
                        const bool diagnostics_match = expected_text != NULL ? actual_text != NULL && strcmp(actual_text, expected_text) == 0 :
                                                       actual != NULL && strcmp(actual, expected) == 0;
                        const bool same = status == oracle && diagnostics_match;
                        check_test_expect(test, same, description);
                        if (!same && !mismatch_printed)
                        {
                            fprintf(stderr, "%s: status %d (clang %d)\nBuster:\n%s\nClang:\n%s\n", description,
                                    status, oracle, actual != NULL ? actual : "<no output>\n", expected);
                            mismatch_printed = true;
                        }
                        if (preserve)
                        {
                            FILE* retained = fopen(objects[scalar], "rb");
                            char bytes[sizeof(sentinel)] = { 0 };
                            const bool unchanged = retained != NULL && fread(bytes, 1, sizeof(bytes), retained) == sizeof(bytes) &&
                                                   memcmp(bytes, sentinel, sizeof(bytes)) == 0 && fgetc(retained) == EOF;
                            check_test_expect(test, prior && unchanged, "promoted default warnings preserve an existing output file");
                            if (retained != NULL)
                            {
                                fclose(retained);
                            }
                        }
                        else
                        {
                            check_test_expect(test, (access(objects[scalar], F_OK) == 0) == (mode != 0 && status == 0),
                                              "default warning policy controls object publication");
                        }
                        free(actual);
                    }
                    if (mode != 0 && oracle == 0)
                    {
                        check_test_expect(test, check_test_driver_same_files(objects[0], objects[1]),
                                          "default warning source has identical scalar/vector objects at each lane request");
                        if (lane == 0)
                        {
                            check_test_expect(test, link(objects[0], objects[3]) == 0, "the first lane request retains a frozen object twin");
                        }
                        else
                        {
                            check_test_expect(test, check_test_driver_same_files(objects[0], objects[3]),
                                              "default warning source has identical objects across requested lane counts");
                        }
                    }
                }
                free(expected);
                for (uint32_t route = 0; route < BUSTER_ARRAY_LENGTH(objects); route += 1)
                {
                    unlink(objects[route]);
                }
            }
        }
    }
    unlink(source);
    check_driver_run_modes.sweep_fixtures += fixture_count;
    check_driver_run_modes.sweep_nanoseconds += check_test_driver_nanoseconds() - began;
}

static void check_test_driver_default_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_default_warning_fixtures, BUSTER_ARRAY_LENGTH(check_driver_default_warning_fixtures));
}

// -flazy-system-headers defers only system-header bodies. A unit with no deferred body must still publish its
// translation-unit-final warnings, exactly as a run without the flag does.
static void check_test_driver_lazy_final_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source_text[] = "static int missing(void);int f(void){return missing();}\n";
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/lazy-final-warning.c", directory);
    const bool written = parse_test_write(source, source_text, sizeof(source_text) - 1);
    char* expected = NULL;
    char* arguments[] = { (char*)clang, "-std=gnu17", "-fno-caret-diagnostics", "-fno-color-diagnostics", "-fsyntax-only", source, NULL };
    const int oracle = written ? check_test_driver_spawn(arguments, &expected) : -1;
    const CheckDriverDiagnosticCounts counts = check_test_driver_diagnostic_counts(expected);
    check_test_expect(test, oracle == 0 && expected != NULL && counts.warnings == 1 && counts.notes == 1,
                      "a lazy-system-header run without a deferred body has Clang's final warning oracle");
    for (uint32_t route = 0; written && route < 2; route += 1)
    {
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            char* run[] = { (char*)(executable != NULL && route != 0 ? executable : "/proc/self/exe"), "-std=gnu17",
                            "-fno-caret-diagnostics", "-fno-color-diagnostics", "-fsyntax-only", "-flazy-system-headers", source,
                            scalar != 0 ? "--scalar" : NULL, NULL };
            char* actual = NULL;
            const int status = check_test_driver_spawn(run, &actual);
            check_test_expect(test, status == oracle && actual != NULL && expected != NULL && strcmp(actual, expected) == 0,
                              scalar != 0 ? "lazy-system-headers scalar run publishes the same final warning" :
                              "lazy-system-headers vector run publishes the same final warning");
            free(actual);
        }
    }
    free(expected);
    unlink(source);
}
