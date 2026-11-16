// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// measure-node-machine's checks (#2524's steps 0 and 1), on original fixtures. Each fixture's line that ends with
// `// refuse: TEST` must have an expression the census refuses by that test, and each that ends with `// take` an
// expression the census takes and none it refuses. The first fixture gives one warning a line, which the compiler reports
// with -Wall -Wextra (its silence tests must refuse what warns); the second is clean idioms the tests once refused (they
// must take them); the third the set's refusals; and with neither -Wall nor -Wextra, the warnings that are off by default
// refuse nothing. Include after c_check_node_census.c; test-checker calls this through its forward declaration.
#ifndef BUSTER_C_CHECK_NODE_CENSUS_TEST_INCLUDED
#define BUSTER_C_CHECK_NODE_CENSUS_TEST_INCLUDED

static const char node_census_test_warnings[] =
    "enum A { A0, A1 };\n"
    "enum B { B0, B1 };\n"
    "struct P { char c; int i; } __attribute__((packed));\n"
    "__attribute__((warn_unused_result)) int wur(void);\n"
    "__attribute__((deprecated)) int old(int);\n"
    "void narrow(unsigned char);\n"
    "int g(int* p, unsigned u, int i, enum A ea, enum B eb, char* s, void* v, int* q, struct P* pp)\n"
    "{\n"
    "    int x = 0;\n"
    "    int arr[4];\n"
    "    enum A e = A0;\n"
    "    x = i << 40; // refuse: constant\n"
    "    x = i / 0; // refuse: constant\n"
    "    x = -1 << 2; // refuse: constant\n"
    "    x = 1 << 31; // refuse: constant\n"
    "    e = 5; // refuse: constant\n"
    "    if (x = i) x = 1; // refuse: parentheses\n"
    "    x = (x & 2 == 2); // refuse: parentheses\n"
    "    x = !x == 3; // refuse: parentheses\n"
    "    x = i << x + 1; // refuse: parentheses\n"
    "    if (i << 3) x = 2; // refuse: parentheses\n"
    "    x == 3; // refuse: unused-value\n"
    "    i + 1; // refuse: unused-value\n"
    "    wur(); // refuse: unused-value\n"
    "    x = (i, 1); // refuse: unused-value\n"
    "    x = x++ + x; // refuse: unsequenced\n"
    "    x = ea == eb; // refuse: enum-mix\n"
    "    e = eb; // refuse: enum-mix\n"
    "    x = (char)p; // refuse: pointer-cast-or-void-dereference\n"
    "    s = \"abc\" + i; // refuse: string-plus-int\n"
    "    x = sizeof(x++); // refuse: unevaluated-operand\n"
    "    unsigned char c = 300; // refuse: constant\n"
    "    x = arr[5]; // refuse: constant\n"
    "    x = *(int*)0; // refuse: constant\n"
    "    x = 3.7; // refuse: constant\n"
    "    q = (0 + 0); // refuse: constant\n"
    "    x = i ? 1.5f : 16777217; // refuse: constant\n"
    "    narrow(300); // refuse: constant\n"
    "    x = (p == 1); // refuse: action-warning\n"
    "    q = i ? p : 1; // refuse: action-warning\n"
    "    x = u < i; // refuse: sign-compare\n"
    "    x = x; // refuse: self-assign\n"
    "    *v; // refuse: pointer-cast-or-void-dereference\n"
    "    q = &pp->i; // refuse: packed-member-address\n"
    "    x = \"a\\q\"[0]; // refuse: warning-event\n"
    "    x = old(1); // refuse: declaration-flag\n"
    "    return x + c + e; // take\n"
    "}\n"
    "int* h(void)\n"
    "{\n"
    "    int local = 0;\n"
    "    return &local; // refuse: return-address\n"
    "}\n";

static const char node_census_test_clean[] =
    "typedef unsigned long size_t;\n"
    "void* memset(void*, int, size_t);\n"
    "struct S { int a; int b; };\n"
    "int h(int* p, size_t l, unsigned char* z, struct S* sp, int i)\n"
    "{\n"
    "    int x = 0; // take\n"
    "    int arr[16];\n"
    "    struct S st;\n"
    "    unsigned char uc = 255; // take\n"
    "    unsigned char ud = -1; // take\n"
    "    int big = 0x80000000u; // take\n"
    "    int* e = &arr[16]; // take\n"
    "    double d = 86400000; // take\n"
    "    memset(arr, 0, sizeof(arr)); // take\n"
    "    memset(arr, 0, l); // take\n"
    "    (void)i; // take\n"
    "    x = x + 1; // take\n"
    "    sp->a = sp->b = 0; // take\n"
    "    st.a = st.b = 0; // take\n"
    "    x = i < 3 ? (i = 1) : (int)(long)&i; // take\n"
    "    x = l == (size_t)-1; // take\n"
    "    x = z[0] >= 128U; // take\n"
    "    x = l > 0; // take\n"
    "    x += sizeof(arr) / sizeof(arr[0]); // take\n"
    "    if ((x = i)) x = 2; // take\n"
    "    p[x] = *e; // take\n"
    "    return x + uc + ud + big + (int)d; // take\n"
    "}\n"
    "__attribute__((unused)) static const char* left(int a)\n"
    "{\n"
    "    if (a > 2) return \"two\";\n"
    "    return a ? \"one\" : \"none\";\n"
    "}\n"
    "const char* kept(int a)\n"
    "{\n"
    "    return a ? \"one\" : \"none\"; // take\n"
    "}\n";

static const char node_census_test_set[] =
    "struct B { unsigned f : 3; };\n"
    "struct S { int a; int b; };\n"
    "int printf(const char*, ...);\n"
    "unsigned long strlen(const char*);\n"
    "void* memset(void*, int, unsigned long);\n"
    "int abs(int);\n"
    "int k(struct B* b, struct S* s, struct S* t, int i, ...)\n"
    "{\n"
    "    register int r = i;\n"
    "    int x = 0;\n"
    "    int a2[2] = { 1, 2 }; // refuse: control\n"
    "    _Complex double z = 0; // refuse: complex-bitint-atomic-vector-half\n"
    "    __builtin_va_list ap;\n"
    "    b->f = 1; // refuse: bit-field\n"
    "    *s = *t; // refuse: aggregate-value\n"
    "    x = r + 1; // refuse: declaration-flag\n"
    "    x = ((struct S){ 1, 2 }).a; // refuse: compound-literal\n"
    "    printf(\"%d\", x); // refuse: callee\n"
    "    x = (int)strlen(\"ab\"); // refuse: callee\n"
    "    memset(s, 0, sizeof(*s)); // refuse: callee\n"
    "    x = abs(i); // refuse: callee\n"
    "    __builtin_va_start(ap, i); // refuse: operator\n"
    "    __builtin_va_end(ap); // refuse: operator\n"
    "    z = z * 2; // refuse: complex-bitint-atomic-vector-half\n"
    "    x = ({ int y = 1; y; }); // refuse: operator\n"
    "    return x + a2[0];\n"
    "}\n"
    "int fenv(double d)\n"
    "{\n"
    "#pragma STDC FENV_ACCESS ON\n"
    "    d = d * 2; // refuse: region\n"
    "    return (int)d; // refuse: region\n"
    "}\n"
    "int hushed(int i)\n"
    "{\n"
    "#pragma clang diagnostic ignored \"-Wparentheses\"\n"
    "    i = i + 1; // refuse: region\n"
    "    return i; // refuse: region\n"
    "}\n";

// A system header's macro in a user's expression (with -isystem on the fixtures' directory's `system`): counted apart,
// not refused.
static const char node_census_test_system_header[] = "#define NOTHING ((void)0)\n#define ZERO(x) ((x) - (x))\n";

static const char node_census_test_system[] =
    "#include <zero.h>\n"
    "int s(int x)\n"
    "{\n"
    "    NOTHING; // take\n"
    "    x = ZERO(x) + 1; // take\n"
    "    return x; // take\n"
    "}\n";

// The same warnings as the first fixture's where they are off by default: read without the options that enable them,
// they refuse nothing.
static const char node_census_test_quiet[] =
    "enum A { A0, A1 };\n"
    "int g(unsigned u, int i)\n"
    "{\n"
    "    int x = 0;\n"
    "    enum A e = A0;\n"
    "    x = u < i; // take\n"
    "    x = x; // take\n"
    "    x = 1 << 31; // take\n"
    "    e = 5; // take\n"
    "    return x + e; // take\n"
    "}\n";

static const char node_census_test_atomic[] =
    "struct A { int x; };\n"
    "_Atomic(struct A) a;\n"
    "_Atomic(struct A)* p;\n"
    "#pragma clang diagnostic ignored \"-Watomic-access\"\n"
    "int direct(void) { return a.x; } // refuse: complex-bitint-atomic-vector-half\n"
    "int indirect(void) { return p->x; } // refuse: complex-bitint-atomic-vector-half\n"
    "int ordinary(struct A* q) { return q->x + 1; } // take\n";

// The shadow's fixture (#2524's step 1): each line that ends with `// machine: taken` has expressions the census takes and
// the machine takes them all, each that ends with `// machine: REFUSAL` one the machine refuses for that reason
// (node_machine_refusal_names). Its taken expressions' records and entries must be the pipeline's, by T's rows and by
// the action alike. Its calls: direct and discarded, nested (whose rows the tasks number in preorder, outer first, where
// the entries are in postorder), variadic (a char and a float promoted), of pointers converted to a parameter's (to
// const char and to void, and null), through a pointer, a member and a call's result; and refused, a long double's
// parameter (the templates'), and callees that return twice: a pointer whose first parameter is a jmp_buf's, and a
// function of such a name. Calls under and around `&&`, `||` and `?:` too, whose labels are numbered from their
// subtrees' least records, a call's arguments included, and whose calls' rows in the tasks' preorder. The argument
// memo's keys come again
// (node_machine_argument_memo): a string and a null pointer constant for the same parameters, integer constants where a
// value of their type filled the entry, variadic constants, and a function designator. T's direct rules for what the
// action typed before: integer constant folds (unary + too, comparisons and plain char constants, whose signedness the
// -funsigned-char run turns; the census refuses constant shifts under these warnings); casts of integers and pointers to
// void, an integer, bool and a pointer, constants among them; a static object's members and its array's subscripts,
// constant and not; a pointer's equality with a null pointer constant; sizeof and _Alignof of a type name and an
// expression; the comma; and stores of a null pointer constant and of a pointer to a pointer of another pointee.
// The machine's own front (step 1b) runs over the fixture too, after the walk acted at a chunk, before it in the --scalar
// run and lean in the -funsigned-char one, where the reader's walk is the coded one (a build with the vector paths; in one without, it must make
// nothing): its every record must be the walk's node, and it must make each taken line's expressions but those of the
// lines that say `// front: left` (sizeof and _Alignof, which the front leaves to the walk). For the front alone, with
// no expectation of the machine: a static braced initializer (its elements' value context), an automatic array's and a
// record's with a nested list (their lists' values dropped where nothing takes them, none left waiting), an enumerator's
// value, a library function called by its name and through its
// address (the call's library mark), and a switch (its condition's operator, and a case label's constant, whose context
// the walk's visit gives: unknown to the front). And a named result (#1375, #2882): `named` returns its one local
// through its hidden pointer, so the lowering builds it where that pointer points and each of the local's addresses
// in the stream is the pointer's value, where the tape has the local's address (a store to its member, a member's
// value in an initializer and a condition, its address as an argument); `either` returns two locals and keeps both.
// For C and X at reader time (BUSTER_NODE_FRONT_SWEEPS): a const object's initializer, whose conversion's target is its
// type without the qualifier, one that converts nothing, and one to another type of the same class (a pointer to a
// pointer to const), whose conversion is empty and gives no target; a static local, stored and read (a global's entries,
// by a declaration in the body); a global's updates, prefix and postfix; with the globals' loads, stores and addresses,
// the strings and the direct calls above, whose entries and rows hold what stands for a symbol there; and a for
// statement, whose condition's wrapper has a type of its own that is no target of its root's (the census refuses the
// statement's expressions under -Wfor-loop-analysis, so the run without warnings compares them).
static const char node_shadow_test_text[] =
    "struct S { long m; long n; int a[4]; unsigned char c; int (*cb)(int); };\n"
    "struct __jmp_buf_tag { long b[8]; };\n"
    "int g;\n"
    "int ga[8];\n"
    "struct S gs;\n"
    "double gd;\n"
    "long double gl;\n"
    "int f(int);\n"
    "int v(int, ...);\n"
    "void w(const char*, void*);\n"
    "int (*pick(int))(int);\n"
    "int ldf(long double);\n"
    "int ap(int (*)(int), int);\n"
    "int getcontext(void*);\n"
    "int twice(int) __attribute__((returns_twice));\n"
    "void* memcpy(void*, const void*, unsigned long);\n"
    "void* memmove(void*, const void*, unsigned long) __asm__(\"node_shadow_moved\");\n"
    "int abs(int);\n"
    "int f(int);\n"
    "unsigned long strlen(const char*);\n"
    "enum E { E0, E1 = 5 };\n"
    "long h(struct S* p, struct S s, int a, int b, unsigned u, char c, double d, float e, int* q, const int* r, int (*fp)(int, double),\n"
    "       int (*sj)(struct __jmp_buf_tag*))\n"
    "{\n"
    "    int x = a + b * 2; // machine: taken\n"
    "    long y = 0; // machine: taken\n"
    "    unsigned long z = u; // machine: taken\n"
    "    const int ci = c; // machine: taken\n"
    "    const long cl = a; // machine: taken\n"
    "    const int* rq = q; // machine: taken\n"
    "    static int sl;\n"
    "    p->n = a + b * 2; // machine: taken\n"
    "    s.m = -a; // machine: taken\n"
    "    x = (int)(~u >> 3); // machine: taken\n"
    "    x = !a; // machine: taken\n"
    "    x = c + 1; // machine: taken\n"
    "    x = p->c >> 1; // machine: taken\n"
    "    d = d * e + 0.5; // machine: taken\n"
    "    e = (float)d; // machine: taken\n"
    "    g = x; // machine: taken\n"
    "    gd = g; // machine: taken\n"
    "    sl = ci + (int)cl; // machine: taken\n"
    "    x = sl + g + *rq; // machine: taken\n"
    "    g++; // machine: taken\n"
    "    x = g--; // machine: taken\n"
    "    *q = q[1] + p->a[2] + *r; // machine: taken\n"
    "    q = &g; // machine: taken\n"
    "    q = q + a; // machine: taken\n"
    "    y = (long)sizeof(*p) + (long)q; // front: left // machine: taken\n"
    "    x = (a < b) == (u >= 3u); // machine: taken\n"
    "    x = (a % 7) | ((b & 1) ^ c); // machine: taken\n"
    "    z = z / 3 + (z >> 1) - (unsigned long)y; // machine: taken\n"
    "    s.n = s.m; // machine: taken\n"
    "    (void)a; // machine: taken\n"
    "    if (a > b) // machine: taken\n"
    "        x = 1; // machine: taken\n"
    "    while (q != 0) // machine: taken\n"
    "        q = 0; // machine: taken\n"
    "    x = a && b; // machine: taken\n"
    "    x = a || (b && c); // machine: taken\n"
    "    x = (a && b) || (c && u); // machine: taken\n"
    "    x = !(a && d); // machine: taken\n"
    "    x = a ? b : 1; // machine: taken\n"
    "    d = a ? d : e; // machine: taken\n"
    "    y = q ? *q : (a < b ? a : b); // machine: taken\n"
    "    (void)(a ? (void)0 : (void)b); // machine: taken\n"
    "    if (a && (b || c)) // machine: template\n"
    "        x = 2; // machine: taken\n"
    "    x = (a && b) ? c : u; // machine: template\n"
    "    x = 1 && a; // machine: taken\n"
    "    x = 0 || a; // machine: taken\n"
    "    x = 1 ? a : b; // machine: taken\n"
    "    x = (q = 0, 1) && a; // machine: taken\n"
    "    x = (q = 0, 0) && a; // machine: taken\n"
    "    x = (q = 0, 1) || a; // machine: taken\n"
    "    x = a ?: b; // machine: template\n"
    "    x = f(a); // machine: taken\n"
    "    f(b); // machine: taken\n"
    "    x = f(f(a) + 1) * 2; // machine: taken\n"
    "    x = f(f(a)) + f(b); // machine: taken\n"
    "    x = v(a, c, e, q); // machine: taken\n"
    "    w(\"ab\", q); // machine: taken\n"
    "    w(0, 0); // machine: taken\n"
    "    w(\"cd\", 0); // machine: taken\n"
    "    x = f(2) + f(x) + v(1, 2, a) + v(a, b, 4); // machine: taken\n"
    "    x = ap(f, 1) + ap(f, x); // machine: taken\n"
    "    x = fp(c, e); // machine: taken\n"
    "    x = p->cb(x); // machine: taken\n"
    "    x = pick(a)(b); // machine: taken\n"
    "    x = ldf(d); // machine: template\n"
    "    x = sj(0); // machine: callee\n"
    "    x = getcontext(q); // machine: callee\n"
    "    x = twice(a);\n"
    "    memcpy(&x, &a, 4);\n"
    "    memcpy(&x, &a, u); // machine: taken\n"
    "    memmove(&x, &a, 4); // machine: taken\n"
    "    x = abs(a);\n"
    "    w(0, &ga); // machine: taken\n"
    "    w(0, &gs.a); // machine: taken\n"
    "    x = a && f(b); // machine: taken\n"
    "    x = f(a) || f(b); // machine: taken\n"
    "    x = f(a ? b : c); // machine: taken\n"
    "    x = a ? f(b) : f(c); // machine: taken\n"
    "    x = f(a && b) + f(c || u); // machine: taken\n"
    "    x++; // machine: taken\n"
    "    x += 2; // machine: taken\n"
    "    --y; // machine: taken\n"
    "    g |= x; // machine: taken\n"
    "    c -= 1; // machine: taken\n"
    "    e *= 2.0f; // machine: taken\n"
    "    d++; // machine: taken\n"
    "    q++; // machine: taken\n"
    "    q -= a; // machine: taken\n"
    "    p->n++; // machine: taken\n"
    "    s.m |= a; // machine: taken\n"
    "    p->a[a] *= b; // machine: taken\n"
    "    --*q; // machine: taken\n"
    "    u /= 4; // machine: taken\n"
    "    y = x++ + q[1]--; // machine: taken\n"
    "    y = p->n++ + q[1]-- + p->a[a]++; // machine: taken\n"
    "    x = a ? q[0]++ : (b && p->a[1]--); // machine: taken\n"
    "    gl += 1; // machine: template\n"
    "    x = a && b++; // machine: taken\n"
    "    x = a ? b++ : --c; // machine: taken\n"
    "    x += f(a++); // machine: taken\n"
    "    y = q - r; // machine: template\n"
    "    z = z / 4; // machine: template\n"
    "    x = -1 + 16 - ~0; // machine: taken\n"
    "    x = 16 | 3 | +7 ^ 5 & 6; // machine: taken\n"
    "    z = 10u / 3u % 2u + 40ul * 3ul; // machine: taken\n"
    "    x = (5 > 3) == (2 != 2); // machine: taken\n"
    "    x = (int)(char)200 + -(char)-56; // machine: taken\n"
    "    y = (char)200; // machine: taken\n"
    "    y = (long)a + (long)(unsigned char)u + (long)(_Bool)a; // machine: taken\n"
    "    q = (int*)(void*)q; // machine: taken\n"
    "    q = (int*)0; // machine: taken\n"
    "    y = (long)(char*)ga; // machine: taken\n"
    "    (void)g; // machine: taken\n"
    "    (void)q; // machine: taken\n"
    "    x = (int)gs.m + gs.a[1] + ga[a] + ga[3]; // machine: taken\n"
    "    q = &gs.a[2]; // machine: taken\n"
    "    q = ga + 2; // machine: taken\n"
    "    x = q == 0; // machine: taken\n"
    "    x = 0 != p; // machine: taken\n"
    "    for (x = 0; x < a; x++)\n"
    "        y += x;\n"
    "    if (r == 0) // machine: taken\n"
    "        x = 3; // machine: taken\n"
    "    y = sizeof(struct S) + sizeof(int) + _Alignof(double) + sizeof g + sizeof ga; // front: left // machine: taken\n"
    "    x = (f(a), b); // machine: taken\n"
    "    y = (f(a), q[0]); // machine: taken\n"
    "    r = q; // machine: taken\n"
    "    w(0, q); // machine: taken\n"
    "    p->cb = 0; // machine: taken\n"
    "    static const int st[3] = { 1, 2, 3 };\n"
    "    int au[2] = { a, b };\n"
    "    struct S ls = { a, b, { 1, 2, a, b }, 0, 0 };\n"
    "    x = E1 + st[1] + au[0] + (int)ls.m;\n"
    "    x = (int)strlen(\"ef\") + (int)(&strlen)(\"gh\");\n"
    "    switch (a)\n"
    "    {\n"
    "        case 1: x = 1; break;\n"
    "        default: break;\n"
    "    }\n"
    "    return y + x + (long)z; // machine: taken\n"
    "}\n"
    "struct W { long a; long b; long c; };\n"
    "long total(const struct W* w);\n"
    "struct W named(long x)\n"
    "{\n"
    "    struct W w;\n"
    "    w.a = x; // machine: taken\n"
    "    w.b = x + 1; // machine: taken\n"
    "    long y = w.a - x; // machine: taken\n"
    "    long n = total(&w); // machine: taken\n"
    "    if (w.a > 3) // machine: taken\n"
    "    {\n"
    "        w.c = y + n; // machine: taken\n"
    "    }\n"
    "    return w;\n"
    "}\n"
    "struct W either(long x)\n"
    "{\n"
    "    struct W first;\n"
    "    struct W second;\n"
    "    first.a = x; // machine: taken\n"
    "    second.a = x + 1; // machine: taken\n"
    "    if (x > 3) // machine: taken\n"
    "    {\n"
    "        return first;\n"
    "    }\n"
    "    return second;\n"
    "}\n";

// The refusal whose name `name` (`length` bytes) is, or NODE_MACHINE_REFUSALS.
static uint32_t node_shadow_test_named(const char* name, uint32_t length)
{
    uint32_t found = NODE_MACHINE_REFUSALS;
    for (uint32_t r = 0; r < NODE_MACHINE_REFUSALS && found == NODE_MACHINE_REFUSALS; r += 1)
    {
        found = strlen(node_machine_refusal_names[r]) == length && memcmp(node_machine_refusal_names[r], name, length) == 0 ? r : found;
    }
    return found;
}

// The shadow's fixture written to `directory` and read with `options`, its T by the rows (and verified against the
// action) or (`scalar`) by the action alone, with the machine's front as `front` asks (BUSTER_NODE_FRONT_*): whether
// every line held and nothing differed from the pipeline.
static bool node_shadow_test_fixture(const char* directory, bool scalar, uint8_t front, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/shadow.c", directory);
    const char* text = node_shadow_test_text;
    bool valid = parse_test_write(path, text, strlen(text));
    NodeShadow shadow = {
        .records_out = calloc(512, sizeof(NodeShadowRecord)),
        .record_capacity = 512,
        .scalar = scalar,
        .verify = !scalar,
        .fused = true,
        .interleave = true,
        .front = front,
        .chunk = 3,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && shadow.records_out != NULL && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0 &&
            shadow.record_count < shadow.record_capacity;
    // The front runs beside the reader's coded walk, the vector path's: a build without the vector paths reads by the
    // scalar walk, where the front must make nothing and no line is expected of it.
    const bool fronts = BUSTER_HOST_VECTOR != 0;
    // C and X as one sweep too: every expression T typed, with C's and X's refusal, tape, labels and calls.
    // T at reader time, where the front asks for it: every expression it ran on that the shadow takes has the shadow's
    // records, it ran on each the front made, the objects' columns and the library calls' answers it made are the
    // program's, and it refused the fixture's callees there: `sj`'s call, getcontext's, twice's, the constant memcpy's,
    // abs's and strlen's (the census refuses four of them before the shadow's T, so no comparison reads those).
    const bool typed = (front & BUSTER_NODE_FRONT_TYPES) == 0 || !fronts ? shadow.typed_expressions == 0 && shadow.typed_compared == 0 :
                                                                           shadow.typed_differences == 0 && shadow.typed_compared != 0 && shadow.typed_skipped == 0 &&
                                                                               shadow.typed_unrun == 0 && shadow.typed_object_differences == 0 &&
                                                                               shadow.typed_objects != 0 && shadow.typed_library_differences == 0 &&
                                                                               shadow.typed_library_calls >= 5 && shadow.typed_refused[NODE_MACHINE_REFUSED_CALLEE] == 6;
    // C and X at reader time, where the front asks for them: they ran on every expression T took there and kept every
    // tape; each one compared has the lowering's pop's target and object and the shadow's refusal, tape, labels and
    // calls; and the fixture's globals' entries and direct calls' rows were patched with the program's symbols.
    const bool swept = (front & BUSTER_NODE_FRONT_SWEEPS) == 0 || !fronts ? shadow.swept_expressions == 0 && shadow.swept_compared == 0 :
                                                                            shadow.swept_differences == 0 && shadow.swept_compared != 0 && shadow.swept_unkept == 0 &&
                                                                                shadow.swept_expressions == shadow.typed_refused[NODE_MACHINE_TAKEN] &&
                                                                                shadow.swept_references >= 20 && shadow.swept_call_targets >= 24;
    const bool same = shadow.value_differences == 0 && shadow.entry_differences == 0 && shadow.twin_differences == 0 && shadow.taken_expressions != 0 &&
                      (scalar || shadow.row_nodes != 0) && shadow.fused_differences == 0 && shadow.fused_expressions >= shadow.taken_expressions &&
                      shadow.front_differences == 0 && shadow.front_checked == shadow.front_records && shadow.front_resets == 0 && shadow.front_waiting == 0 &&
                      (fronts ? shadow.front_units == 1 && shadow.front_unread == 0 && shadow.front_records != 0 : shadow.front_units == 0 && shadow.front_records == 0) && typed &&
                      swept;
    if (valid && !swept)
    {
        fprintf(stderr, "test-checker: node-shadow%s: C and X at reader time ran on %" PRIu64 " expressions (T took %" PRIu64 "), %" PRIu64 " compared, %" PRIu64
                " differ (root %" PRIu64 ", refusal %" PRIu64 ", tape %" PRIu64 ", labels and calls %" PRIu64 "), %" PRIu64 " tapes not kept, %" PRIu64
                " globals' entries and %" PRIu64 " direct calls patched\n",
                scalar ? " --scalar" : "", shadow.swept_expressions, shadow.typed_refused[NODE_MACHINE_TAKEN], shadow.swept_compared, shadow.swept_differences,
                shadow.swept_root_differences, shadow.swept_refusal_differences, shadow.swept_tape_differences, shadow.swept_side_differences, shadow.swept_unkept,
                shadow.swept_references, shadow.swept_call_targets);
    }
    if (!valid || !same)
    {
        fprintf(stderr, "test-checker: node-shadow%s: %s: values %" PRIu64 ", entries %" PRIu64 ", twin %" PRIu64 ", fused %" PRIu64 " (of %" PRIu64 ") differ; %" PRIu64
                " expressions taken, %" PRIu64 " row-typed nodes; the front's records %" PRIu64 ", compared %" PRIu64 ", %" PRIu64 " differ, %" PRIu64 " resets, %" PRIu64
                " values left waiting, in %" PRIu64 " units (%s); T at reader time ran on %" PRIu64 " expressions, %" PRIu64 " compared, %" PRIu64 " differ, %" PRIu64
                " roots not run, %" PRIu64 " of the front's expressions not run, %" PRIu64 " callees refused, %" PRIu64 " objects and %" PRIu64 " library calls differ\n",
                scalar ? " --scalar" : "", valid ? "read" : "NOT READ", shadow.value_differences, shadow.entry_differences, shadow.twin_differences, shadow.fused_differences,
                shadow.fused_expressions, shadow.taken_expressions, shadow.row_nodes, shadow.front_records, shadow.front_checked, shadow.front_differences,
                shadow.front_resets, shadow.front_waiting, shadow.front_units, fronts ? "expected in the one" : "expected in none: no vector paths in this build",
                shadow.typed_expressions, shadow.typed_compared, shadow.typed_differences, shadow.typed_skipped, shadow.typed_unrun,
                shadow.typed_refused[NODE_MACHINE_REFUSED_CALLEE], shadow.typed_object_differences, shadow.typed_library_differences);
    }
    uint32_t line = 1;
    const char* at = text;
    while (valid && *at != 0)
    {
        const char* end = strchr(at, '\n');
        end = end != NULL ? end : at + strlen(at);
        const char* expectation = NULL;
        bool left = false;
        for (const char* c = at; c + 2 < end && expectation == NULL; c += 1)
        {
            left = left || strncmp(c, "// front: left", 14) == 0;
            expectation = strncmp(c, "// machine: ", 12) == 0 ? c + 12 : NULL;
        }
        const uint32_t expected = expectation != NULL ? node_shadow_test_named(expectation, (uint32_t)(end - expectation)) : NODE_MACHINE_REFUSALS;
        uint32_t found = 0;
        uint32_t matched = 0;
        uint32_t fronted = 0;
        for (uint32_t r = 0; r < shadow.record_count; r += 1)
        {
            found += shadow.records_out[r].line == line;
            matched += shadow.records_out[r].line == line && shadow.records_out[r].refusal == expected;
            fronted += shadow.records_out[r].line == line && shadow.records_out[r].fronted;
        }
        // A taken line's expressions are the front's too, each of them, or (`// front: left`) none.
        const bool made = expected != NODE_MACHINE_TAKEN || (left || !fronts ? fronted == 0 : fronted == found);
        const bool held = expectation == NULL || (expected != NODE_MACHINE_REFUSALS && (expected == NODE_MACHINE_TAKEN ? found != 0 && matched == found : matched != 0) && made);
        if (!held)
        {
            fprintf(stderr, "test-checker: node-shadow%s: line %u: expected %.*s, found %u expressions, %u as expected, %u the front's%s\n", scalar ? " --scalar" : "", line,
                    (int)(expectation != NULL ? end - expectation : 0), expectation != NULL ? expectation : "", found, matched, fronted, left ? " (expected none)" : "");
        }
        valid = valid && held;
        line += 1;
        at = *end != 0 ? end + 1 : end;
    }
    free(shadow.records_out);
    unlink(path);
    return valid && same;
}

// The test whose name `name` (`length` bytes) is, or NODE_CENSUS_TESTS.
static uint32_t node_census_test_named(const char* name, uint32_t length)
{
    uint32_t found = NODE_CENSUS_TESTS;
    for (uint32_t t = 0; t < NODE_CENSUS_TESTS && found == NODE_CENSUS_TESTS; t += 1)
    {
        found = strlen(node_census_test_names[t]) == length && memcmp(node_census_test_names[t], name, length) == 0 ? t : found;
    }
    return found;
}

// Fixture `text`, written to `directory` as `name` and read with `options`: the census's records against each line's
// expectation, and its expressions with a system header's macro in `system`. Each line that fails is reported on stderr;
// whether every line held.
static bool node_census_test_fixture(const char* directory, const char* name, const char* text, char** options, uint32_t option_count, uint64_t* system)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    bool valid = parse_test_write(path, text, strlen(text));
    NodeCensus census = { .records = calloc(512, sizeof(NodeCensusRecord)), .record_capacity = 512 };
    fflush(stdout);
    valid = valid && census.records != NULL && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0 &&
            census.record_count < census.record_capacity;
    if (!valid)
    {
        fprintf(stderr, "test-checker: node-census: %s wasn't counted\n", name);
    }
    uint32_t line = 1;
    const char* at = text;
    while (valid && *at != 0)
    {
        const char* end = strchr(at, '\n');
        end = end != NULL ? end : at + strlen(at);
        const char* refuse = NULL;
        const char* take = NULL;
        for (const char* c = at; c + 2 < end && refuse == NULL && take == NULL; c += 1)
        {
            refuse = strncmp(c, "// refuse: ", 11) == 0 ? c + 11 : NULL;
            take = strncmp(c, "// take", 7) == 0 ? c : NULL;
        }
        const uint32_t expected = refuse != NULL ? node_census_test_named(refuse, (uint32_t)(end - refuse)) : NODE_CENSUS_TESTS;
        uint32_t found = 0;
        uint32_t refused = 0;
        bool matched = false;
        for (uint32_t r = 0; r < census.record_count; r += 1)
        {
            const NodeCensusRecord* record = census.records + r;
            found += record->line == line;
            refused += record->line == line && record->tests != 0;
            matched |= record->line == line && expected != NODE_CENSUS_TESTS && (record->tests >> expected & 1) != 0;
        }
        const bool held = refuse != NULL ? matched : take != NULL ? found != 0 && refused == 0 : true;
        if (!held)
        {
            fprintf(stderr, "test-checker: node-census: %s line %u: expected %s, found %u expressions, %u refused\n", name, line,
                    refuse != NULL ? "a refusal by its test" : "every expression taken", found, refused);
        }
        valid = valid && (refuse == NULL || expected != NODE_CENSUS_TESTS);
        valid = valid && held;
        line += 1;
        at = *end != 0 ? end + 1 : end;
    }
    *system = census.system_expressions;
    free(census.records);
    unlink(path);
    return valid;
}

// The census's tests at reader time (docs/node-machine.md, "The tests where they sit") on fixture `text`, written to
// `directory` as `name` and read with `options`, the front `front` before T: the verdict there against the census's after
// the pipeline. No expression reader time takes is one the census refuses; no test differs but as it may; every root
// the tests ran on is compared; T's rows and rules are the action's there, without a warning; and a line that says
// `// refuse: TEST` of a silence test has an expression that test refuses there, where the tests ran on the line at all
// (the set's lines, and a region's, can be T's own refusals before any test). `refused` expressions are refused there
// by a test, no more (the tests see one expression's warnings, not the unit's so far), and the fixture's `discards` declarations' discards are left alone (an initializer's value cast to
// void at the declaration's `;`, each value of a braced one; an enumerator's value makes no record of the front's, and
// so none).
// In a build without the vector paths the front makes nothing, and no test runs. With `own` the tests there are the
// machine's own form ("The tests in the machine's own form"), and the census's code runs on every expression beside it:
// their bits, their spans of tokens and their counts of nodes are the same. Whether all of it held.
static bool node_tests_test_fixture_form(const char* directory, const char* name, const char* text, char** options, uint32_t option_count, uint8_t front, uint32_t refused,
                                         uint32_t discards, bool own)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    bool valid = parse_test_write(path, text, strlen(text));
    NodeShadow shadow = {
        .verify = true,
        .front = (uint8_t)(front | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS),
        .reader = (uint8_t)(BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_VERIFY | (own ? BUSTER_NODE_READER_OWN | BUSTER_NODE_READER_BOTH : 0)),
        .passes = 1,
        .repeat = 1,
        .tested_out = calloc(512, sizeof(NodeShadowTested)),
        .tested_out_capacity = 512,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && shadow.tested_out != NULL && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0 &&
            shadow.tested_out_count < shadow.tested_out_capacity;
    const bool fronts = BUSTER_HOST_VECTOR != 0;
    const bool same = !fronts ? shadow.tested_expressions == 0 && shadow.tested_compared == 0 :
                                shadow.tested_unsound == 0 && shadow.tested_differences == 0 && shadow.tested_kind_differences == 0 && shadow.tested_missed == 0 &&
                                    shadow.tested_policy_differences == 0 && shadow.reader_twin_differences == 0 && shadow.reader_twin_warnings == 0 &&
                                    shadow.tested_expressions != 0 && shadow.tested_compared + shadow.tested_compared_unpopped == shadow.tested_expressions &&
                                    shadow.tested_refused == refused && shadow.typed_discards == discards &&
                                    (!own || (shadow.own_differences == 0 && shadow.own_count_differences == 0 && shadow.own_expressions == shadow.tested_expressions));
    if (!valid || !same)
    {
        fprintf(stderr, "test-checker: node-tests %s%s: %s: the own form ran on %" PRIu64 " expressions, %" PRIu64 " not the census's code's, %" PRIu64
                " walks of another count\n",
                name, own ? " (own form)" : "", valid ? "read" : "NOT READ", shadow.own_expressions, shadow.own_differences, shadow.own_count_differences);
        fprintf(stderr, "test-checker: node-tests %s: %s: the tests ran on %" PRIu64 " expressions at reader time and refused %" PRIu64 " (%u expected); %" PRIu64
                " compared and %" PRIu64 " never lowered; %" PRIu64 " taken there that the census refuses, %" PRIu64 " differ, %" PRIu64 " roots' kinds differ, %" PRIu64
                " nodes outside the records, %" PRIu64 " units' enabled warnings differ; T's rows and rules there: %" PRIu64 " values not the action's, %" PRIu64
                " nodes whose action warns; %" PRIu64 " declarations' discards left alone (%u expected)\n",
                name, valid ? "read" : "NOT READ", shadow.tested_expressions, shadow.tested_refused, refused, shadow.tested_compared, shadow.tested_compared_unpopped,
                shadow.tested_unsound, shadow.tested_differences, shadow.tested_kind_differences, shadow.tested_missed, shadow.tested_policy_differences,
                shadow.reader_twin_differences, shadow.reader_twin_warnings, shadow.typed_discards, discards);
    }
    uint32_t line = 1;
    const char* at = text;
    while (valid && fronts && *at != 0)
    {
        const char* end = strchr(at, '\n');
        end = end != NULL ? end : at + strlen(at);
        const char* refuse = NULL;
        for (const char* c = at; c + 2 < end && refuse == NULL; c += 1)
        {
            refuse = strncmp(c, "// refuse: ", 11) == 0 ? c + 11 : NULL;
        }
        const uint32_t expected = refuse != NULL ? node_census_test_named(refuse, (uint32_t)(end - refuse)) : NODE_CENSUS_TESTS;
        uint32_t found = 0;
        uint32_t taken = 0;
        uint32_t matched = 0;
        for (uint32_t r = 0; r < shadow.tested_out_count; r += 1)
        {
            const NodeShadowTested* record = shadow.tested_out + r;
            found += record->line == line;
            taken += record->line == line && record->taken;
            matched += record->line == line && expected != NODE_CENSUS_TESTS && (record->reader >> expected & 1) != 0;
        }
        // A silence test's line has an expression that test refuses there, unless the tests ran on none of the line's
        // (T's own refusal first: a floating-point pragma's region is a context of its nodes).
        const bool silence = expected != NODE_CENSUS_TESTS && expected >= NODE_CENSUS_FIRST_SILENCE;
        const bool held = refuse == NULL || !silence || matched != 0 || found == 0;
        if (!held)
        {
            fprintf(stderr, "test-checker: node-tests %s line %u: expected an expression its test refuses at reader time, found %u compared, %u taken, %u refused by the test\n",
                    name, line, found, taken, matched);
        }
        valid = valid && held;
        line += 1;
        at = *end != 0 ? end + 1 : end;
    }
    free(shadow.tested_out);
    unlink(path);
    return valid && same;
}

// Both forms of the tests at reader time on one fixture: the census's code on the view, then the machine's own form
// beside it.
static bool node_tests_test_fixture(const char* directory, const char* name, const char* text, char** options, uint32_t option_count, uint8_t front, uint32_t refused,
                                    uint32_t discards)
{
    const bool census = node_tests_test_fixture_form(directory, name, text, options, option_count, front, refused, discards, false);
    const bool own = node_tests_test_fixture_form(directory, name, text, options, option_count, front, refused, discards, true);
    return census && own;
}

// --facts' fixture (#2850's step 1, c_check_node_machine_facts.c): each kind of identifier's leaf (an automatic object, a
// parameter, a static object, a function, enumerators) and each lookup the fact words' rules take: `->` and `.`, chained
// and through a const record; a static record's member, an address constant; `*p`; subscripts of a pointer, of an
// automatic and a static array, at a variable index and at constant ones (a static array's element's address is folded),
// and of a member array; calls of a function, a variadic one, a pointer and a member; and discards, written and an
// expression statement's. Two the rules leave to T's: the members of a record whose type has an aligned typedef's
// wrapper, through a pointer and of an element (the subscript of a pointer to one they take).
static const char node_facts_test_text[] =
    "struct S { long m; long n; int a[4]; unsigned char c; int (*cb)(int); struct S* next; };\n"
    "typedef struct S Wide __attribute__((aligned(32)));\n"
    "enum E { E0, E1 = 5, EN = -3 };\n"
    "int g;\n"
    "static int sg;\n"
    "int ga[8];\n"
    "struct S gs;\n"
    "int f(int);\n"
    "int v(int, ...);\n"
    "void w(void);\n"
    "long h(struct S* p, int a, int* q, Wide* wp, int (*fp)(int), const struct S* cp)\n"
    "{\n"
    "    long y = 0;\n"
    "    int x = 0;\n"
    "    int la[4];\n"
    "    struct S s;\n"
    "    s.m = a;\n"
    "    la[1] = a;\n"
    "    x = a + g + sg;\n"
    "    x = E1 + EN;\n"
    "    y = p->m + p->next->n;\n"
    "    y = s.m + gs.n;\n"
    "    x = *q;\n"
    "    x = q[a];\n"
    "    x = ga[a];\n"
    "    q = &ga[2];\n"
    "    q = &gs.a[1];\n"
    "    x = la[1];\n"
    "    x = p->a[3];\n"
    "    x = f(a) + v(a, x, y) + fp(a) + p->cb(a);\n"
    "    (void)f(a);\n"
    "    w();\n"
    "    y = wp->m;\n"
    "    y = wp[1].n;\n"
    "    y = p[1].n;\n"
    "    y = cp->m + cp->a[1];\n"
    "    return y + x;\n"
    "}\n";

// The facts' fixture written to `directory` and read with `options` by the kernel's variant `mode`, its windows cut at
// `window` records (0 for the kernel's own cut) and holding several bodies' expressions with `packed`: whether every
// record the fact words' rules made is T's, the kernel's lists are the twin's, and the leaves, the lookups and those
// left are the fixture's; and, of a lean kernel, whether every conversion's entry and every discard it left out is its
// rule's on the kernel's records, and the loads' kernel took by its load what the lean one took by its short path.
static bool node_facts_test_fixture(const char* directory, uint32_t mode, uint32_t window, bool packed, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/facts.c", directory);
    bool valid = parse_test_write(path, node_facts_test_text, strlen(node_facts_test_text));
    NodeShadow shadow = { .facts = true, .facts_mode = mode, .facts_packed = packed, .facts_window = window, .passes = 1, .repeat = 1 };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    // Its leaves by what they name (enumerators, functions, static and automatic objects), its lookups by operator (`->`,
    // `.`, `*`, a subscript, a call, a discard), and those the rules leave.
    static const uint64_t named[4] = { 2, 4, 6, 46 };
    static const uint64_t lookups[BUSTER_NODE_FACTS_LOOKUPS] = { 8, 6, 1, 10, 6, 21 };
    static const uint64_t left[BUSTER_NODE_FACTS_LOOKUPS] = { 1, 1, 0, 0, 0, 0 };
    bool held = shadow.facts_differences == 0 && shadow.facts_list_differences == 0 && shadow.facts_failures == 0 && shadow.value_differences == 0 &&
                shadow.facts_left_identifiers == 0;
    for (uint32_t k = 0; k < 4; k += 1)
    {
        held = held && shadow.facts_named[k] == named[k];
    }
    for (uint32_t lookup = 0; lookup < BUSTER_NODE_FACTS_LOOKUPS; lookup += 1)
    {
        held = held && shadow.facts_lookups[lookup] == lookups[lookup] && shadow.facts_left_lookups[lookup] == left[lookup];
    }
    // The fixture's 21 discards, which a lean kernel doesn't make: each T's record by the discard's rule. Its other 31
    // lookups less the two left: each one's conversions' entries by its rule. Two more go by their rule in any variant
    // (a member through an array's pointer, a constant index of an address constant). `p[1].n` is its one taken member
    // of an operand that is no address constant, at an offset: the record a dot's choices by masks would get wrong.
    const bool lean = mode != BUSTER_NODE_FACTS_WHOLE;
    held = held && shadow.facts_discards == (lean ? 21u : 0u) && shadow.facts_discards_derived == shadow.facts_discards &&
           shadow.facts_conversions == (lean ? 29u : 0u) && shadow.facts_conversions_derived == shadow.facts_conversions &&
           shadow.facts_taken_lookups - shadow.facts_short_lookups == 2 && (mode != BUSTER_NODE_FACTS_LOADS || shadow.facts_loads == shadow.facts_short_lookups);
    if (!valid || !held)
    {
        fprintf(stderr, "test-checker: node-facts: variant %u window %u%s: %s: %" PRIu64 " records differ, %" PRIu64 " lists differ, %" PRIu64 " leaves left; named %" PRIu64
                        " %" PRIu64 " %" PRIu64 " %" PRIu64 "; conversions %" PRIu64 " of %" PRIu64 ", discards %" PRIu64 " of %" PRIu64 ", by a short path %" PRIu64
                        ", by the load %" PRIu64 "; lookups",
                mode, window, packed ? " packed" : "", valid ? "read" : "NOT READ", shadow.facts_differences, shadow.facts_list_differences, shadow.facts_left_identifiers,
                shadow.facts_named[0], shadow.facts_named[1], shadow.facts_named[2], shadow.facts_named[3], shadow.facts_conversions_derived, shadow.facts_conversions,
                shadow.facts_discards_derived, shadow.facts_discards, shadow.facts_short_lookups, shadow.facts_loads);
        for (uint32_t lookup = 0; lookup < BUSTER_NODE_FACTS_LOOKUPS; lookup += 1)
        {
            fprintf(stderr, " %s %" PRIu64 " (left %" PRIu64 ")", node_facts_lookup_names[lookup], shadow.facts_lookups[lookup], shadow.facts_left_lookups[lookup]);
        }
        fprintf(stderr, "\n");
    }
    unlink(path);
    return valid && held;
}

// --facts-operators' fixture (#2850's step 2's first piece): each operator the operators' kernel makes, by each case of
// its rule, and beside each the shapes its rule leaves to T. The rows' operators, of int, unsigned, a promoted char and
// double operands, lvalues and literals; the constants' folds; a pointer plus and minus an integer, an array's and an
// address constant's too, at a constant index either way; a pointer's equality with a null pointer constant, either
// side, and with another pointer (T's); stores of each case (a value, an integer constant, one that its target's width
// extends, a null pointer, a pointer to another pointer type and to void and back, an array's pointer, a floating
// constant and a truth to bool (T's)); conversions (a return's, an initializer's); truths of an integer, a pointer, a
// floating value, a constant and an array; `&&` and `||` with and without a constant side; `&x` of an object, an
// element, a member and a function; casts between integers and pointers, of a constant and of a floating value (T's);
// every update, of an integer, a char, a double and a pointer, prefix and postfix; a comma; sizeof and _Alignof of a
// type and of an expression; and `?:` of arithmetic arms, either one converted to the other's type, and of void arms,
// and of a constant condition (T's). A store, a call and an update of an operand whose code is after their own (a
// truth, a dereference): each is a round after it, which only the lists' order check sees, since these set the decoys'
// flag themselves. And chains of `->` whose first link is T's (a member of a record whose type has an aligned typedef's
// wrapper): the links after it are of its class and read it, so each must wait for it.
static const char node_facts_test_operators_text[] =
    "struct S { long m; int a[4]; unsigned char c; struct S* next; };\n"
    "typedef struct S Wide __attribute__((aligned(32)));\n"
    "int g;\n"
    "int ga[8];\n"
    "struct S gs;\n"
    "int f(int);\n"
    "void w(void);\n"
    "long ops(struct S* p, int a, int* q, char* s, unsigned char uc, double d, _Bool flag, Wide* wp)\n"
    "{\n"
    "    long y = a;\n"
    "    int x = 0;\n"
    "    unsigned u = 2u;\n"
    "    const char* cs = 0;\n"
    "    void* vp = 0;\n"
    "    x = a + x; x = a - 1; u = u * 2u; x = a / 2; x = a % 3; x = a << 2; x = a >> 1;\n"
    "    x = a < x; x = a <= x; x = a > x; x = a >= x; x = a == x; x = a != x;\n"
    "    x = a & 3; x = a | 4; x = a ^ 5; x = -a; x = ~a; x = uc + 1; d = d * 2.0; d = a + d; y = y + a;\n"
    "    x = -1; x = ~0; x = 1 << 4; x = 3 + 4; x = 7 - 2; x = (2 | 8) & 10; x = +5; x = +a; y = -1;\n"
    "    q = q + 1; q = q - a; q = 1 + q; s = s + a; q = ga + 2; q = &ga[1] + 1; q = &ga[3] - 1;\n"
    "    x = p == 0; x = 0 != p; x = vp == (void*)0; x = q == q; x = q < q; y = q - q;\n"
    "    x = a; y = a; uc = a; d = a; y = 1; uc = 255; p = 0; s = 0; q = q; cs = s; vp = p; p = vp; s = (void*)0; q = ga;\n"
    "    flag = a; flag = 1; d = 1.5;\n"
    "    if (a) x = 1;\n"
    "    if (p) x = 2;\n"
    "    if (!a) x = 3;\n"
    "    while (0) x = 4;\n"
    "    if (d) x = 5;\n"
    "    if (ga) x = 6;\n"
    "    x = a && x; x = a || p; x = 0 && a; x = 1 || a; x = a && 0;\n"
    "    q = &x; q = &ga[0]; p = &gs; q = &p->a[1]; vp = &f;\n"
    "    x = (int)y; y = (long)a; uc = (unsigned char)a; p = (struct S*)vp; vp = (void*)p; y = (long)p; q = (int*)y; x = (int)1; x = (int)d; flag = (_Bool)a;\n"
    "    x++; ++x; x--; --x; x += a; x -= 1; x *= 2; x /= 2; x %= 3; x <<= 1; x >>= 1; x &= 3; x |= 4; x ^= 5;\n"
    "    q++; --q; q += 2; q -= a; uc++; d += 1.0; y = x++ + 1; gs.m++;\n"
    "    x = (a++, x);\n"
    "    y = sizeof(struct S); y = sizeof x; y = sizeof(ga); y = _Alignof(long);\n"
    "    x = a ? x : 1; y = a ? y : x; a ? w() : w(); x = 1 ? a : x; d = a ? d : x; d = a ? x : d;\n"
    "    x = !a; x = f(!a); x = f(*q); *q += 1; (*q)++;\n"
    "    y = wp->next->m; y = wp->next->next->m + wp->next->a[1];\n"
    "    return y + x;\n"
    "}\n";

// The operators' fixture written to `directory` and read with `options` by the operators' kernel as variant `mode`
// (whole or lean), its windows cut at `window` records (0 for the kernel's own cut) and holding several bodies'
// expressions with `packed`: whether every record the kernel made is T's, its lists are the twin's, each operator's
// records, those made and those a class's loop made are the fixture's, every literal's leaf was its line, and, of a lean
// kernel, every conversion's entry it left out is its rule's on the kernel's records.
static bool node_facts_test_operators(const char* directory, uint32_t mode, uint32_t window, bool packed, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/operators.c", directory);
    bool valid = parse_test_write(path, node_facts_test_operators_text, strlen(node_facts_test_operators_text));
    NodeShadow shadow = { .facts = true, .facts_operators = true, .facts_mode = mode, .facts_packed = packed, .facts_window = window, .passes = 1, .repeat = 1 };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    // Each operator's records, those the kernel made, and of them those a class's loop made (the rest by a rule).
    static const struct
    {
        CheckOp op;
        uint16_t records;
        uint16_t taken;
        uint16_t brief;
    } pins[] = {
        { CHECK_OP_POSITIVE, 2, 1, 0 },
        { CHECK_OP_NEGATIVE, 3, 3, 1 },
        { CHECK_OP_NOT, 3, 3, 3 },
        { CHECK_OP_BIT_NOT, 2, 2, 1 },
        { CHECK_OP_ADD, 12, 12, 7 },
        { CHECK_OP_SUBTRACT, 4, 4, 1 },
        { CHECK_OP_MULTIPLY, 2, 2, 2 },
        { CHECK_OP_REMAINDER, 1, 1, 1 },
        { CHECK_OP_SHIFT_LEFT, 2, 2, 1 },
        { CHECK_OP_SHIFT_RIGHT, 1, 1, 1 },
        { CHECK_OP_LESS, 2, 1, 1 },
        { CHECK_OP_LESS_EQUAL, 1, 1, 1 },
        { CHECK_OP_GREATER, 1, 1, 1 },
        { CHECK_OP_GREATER_EQUAL, 1, 1, 1 },
        { CHECK_OP_EQUAL, 4, 3, 1 },
        { CHECK_OP_NOT_EQUAL, 2, 2, 1 },
        { CHECK_OP_BIT_AND, 2, 2, 1 },
        { CHECK_OP_BIT_OR, 2, 2, 1 },
        { CHECK_OP_BIT_XOR, 1, 1, 1 },
        { CHECK_OP_LOGICAL_AND, 3, 3, 1 },
        { CHECK_OP_LOGICAL_OR, 2, 2, 1 },
        { CHECK_OP_CONDITIONAL, 6, 5, 0 },
        { CHECK_OP_CAST, 12, 8, 8 },
        { CHECK_OP_ADDRESS, 7, 7, 7 },
        { CHECK_OP_ASSIGN, 100, 97, 95 },
        { CHECK_OP_COMMA, 1, 1, 0 },
        { CHECK_OP_SIZEOF, 3, 3, 0 },
        { CHECK_OP_ALIGNOF, 1, 1, 0 },
        { CHECK_OP_CONVERT, 1, 1, 1 },
        { CHECK_OP_CONDITION, 6, 5, 4 },
        { CHECK_OP_PRE_INCREMENT, 1, 1, 1 },
        { CHECK_OP_PRE_DECREMENT, 2, 2, 2 },
        { CHECK_OP_POST_INCREMENT, 7, 7, 7 },
        { CHECK_OP_POST_DECREMENT, 1, 1, 1 },
        { CHECK_OP_ADD_ASSIGN, 4, 4, 4 },
        { CHECK_OP_SUBTRACT_ASSIGN, 2, 2, 2 },
        { CHECK_OP_MULTIPLY_ASSIGN, 1, 1, 1 },
        { CHECK_OP_REMAINDER_ASSIGN, 1, 1, 1 },
        { CHECK_OP_SHIFT_LEFT_ASSIGN, 1, 1, 1 },
        { CHECK_OP_SHIFT_RIGHT_ASSIGN, 1, 1, 1 },
        { CHECK_OP_BIT_AND_ASSIGN, 1, 1, 1 },
        { CHECK_OP_BIT_OR_ASSIGN, 1, 1, 1 },
        { CHECK_OP_BIT_XOR_ASSIGN, 1, 1, 1 },
    };
    bool held = shadow.facts_differences == 0 && shadow.facts_list_differences == 0 && shadow.facts_failures == 0 && shadow.value_differences == 0 &&
                shadow.facts_left_identifiers == 0 && shadow.facts_literal_lines == shadow.facts_taken_lines && shadow.facts_literal_lines == shadow.facts_literals &&
                shadow.facts_literal_lines != 0;
    uint64_t pinned = 0;
    uint64_t operators = 0;
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(pins); k += 1)
    {
        held = held && shadow.facts_operator_records[pins[k].op] == pins[k].records && shadow.facts_operator_taken[pins[k].op] == pins[k].taken &&
               shadow.facts_operator_short[pins[k].op] == pins[k].brief;
        pinned += pins[k].records;
    }
    for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
    {
        operators += shadow.facts_operator_records[op];
    }
    const bool lean = mode != BUSTER_NODE_FACTS_WHOLE;
    held = held && operators == pinned && shadow.facts_conversions_derived == shadow.facts_conversions && (shadow.facts_conversions != 0) == lean &&
           shadow.facts_discards_derived == shadow.facts_discards;
    if (!valid || !held)
    {
        fprintf(stderr, "test-checker: node-facts operators: variant %u window %u%s: %s: %" PRIu64 " records differ, %" PRIu64 " lists differ, %" PRIu64
                        " leaves left; literals %" PRIu64 ", their lines %" PRIu64 ", taken %" PRIu64 "; conversions %" PRIu64 " of %" PRIu64 "; operators %" PRIu64
                        " (pinned %" PRIu64 "):\n",
                mode, window, packed ? " packed" : "", valid ? "read" : "NOT READ", shadow.facts_differences, shadow.facts_list_differences, shadow.facts_left_identifiers,
                shadow.facts_literals, shadow.facts_literal_lines, shadow.facts_taken_lines, shadow.facts_conversions_derived, shadow.facts_conversions, operators, pinned);
        for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
        {
            if (shadow.facts_operator_records[op] != 0)
            {
                fprintf(stderr, "        { %u, %" PRIu64 ", %" PRIu64 ", %" PRIu64 " }, // %s\n", op, shadow.facts_operator_records[op], shadow.facts_operator_taken[op],
                        shadow.facts_operator_short[op], lower_inventory_op_names[op] != NULL ? lower_inventory_op_names[op] : "?");
            }
        }
    }
    unlink(path);
    return valid && held;
}

// --facts-reads' fixture (#2850's step 2's second piece, c_check_node_machine_reads.c): records whose wide fields are
// read, each by a reader the census has to find, beside records nobody reads a wide field of. A row's operator of
// builtin operands (`a + 1`, `a * 8`): nothing. A fold and a negative literal (`3 + 4`, `-1`): a payload new at the
// record, which X writes. A pointer plus an integer whose type a store's pointer conversion, a dereference and a call's
// argument's conversion read (`q + 1`, `*(q + a)`, `f(q + 2)`): its operand's type, a copy. A store whose type a member
// access reads through to the member's offset (`(p = p->next)->m`): its operand's. `&x` as a call's argument: the
// pointer to its operand's type, a row's. sizeof: its type's size, a row's, which X writes. A cast whose type a member
// access reads (`((struct T*)p)->m`): the node's own input. A member of an address constant (`gs.a[1]`): an offset
// new at the record, and whether it is zero decides X's entries. The two records' types and the three pointers' are
// each other's other selves: a type with no other of its word in the unit has no identity the census can change.
// A fold whose value is one of its operands' (`1 << 0`): computed, so new, where a returned constant's conversion to a
// type that holds every value copies it (`return 5` as long); one to a type that doesn't copies it where it fits
// (`return 65` as char), and a constant's cast is T's own rule, which the census can't run again (`(long)3`): both are
// counted new, and apart. And the address of a volatile object as an argument (`&gv`): the pointer to its operand's
// type, which that pointer type's own row names where the operand's side row doesn't hold it. With `volatile int*`
// among the pointers, `int*`'s other self has to be `long*`, whose pointee's size differs: C's and X's reads of a
// pointer sum's conversions show by that. And a returned null pointer constant (`return 0` as `int*`): its literal's
// zero decides for the conversion's rule. `byvalue`, which nothing calls, is there for its type: `f`'s type's other
// self, a function whose parameter is no plain scalar, which C asks of a callee's type before it takes a call. The two
// calls of `f` are the records C decides on an identity of. And a subscript of an array of long (`gla[1]`): its first
// conversion's type is the pointer its operand's array type decays to, which only that type's row gives (the fixture's
// other subscript's is also the pointer to its index's type).
// And the two kinds of record the census leaves: a sum of 129 terms, an expression of 260 records, past what C and X
// are run again on; and a fold of two floating literals, which the kernel leaves to T, and so does the store of it:
// the operands of both aren't measured, nor is the constant's cast's.
static const char node_reads_test_text[] =
    "#define A4 a + a + a + a\n"
    "#define A16 A4 + A4 + A4 + A4\n"
    "#define A64 A16 + A16 + A16 + A16\n"
    "struct S { long m; int a[4]; struct S* next; };\n"
    "struct T { char c; long m; };\n"
    "int g;\n"
    "int ga[8];\n"
    "struct S gs;\n"
    "struct T gt;\n"
    "double gd;\n"
    "volatile int gv;\n"
    "long gla[4];\n"
    "int f(int*);\n"
    "int byvalue(struct T);\n"
    "void takev(volatile int*);\n"
    "long reads(struct S* p, struct T* t, int a, int* q, long y, long* lp)\n"
    "{\n"
    "    int x = 0;\n"
    "    x = a + 1;\n"
    "    x = 3 + 4;\n"
    "    y = -1;\n"
    "    x = a * 8;\n"
    "    q = q + 1;\n"
    "    y = (p = p->next)->m;\n"
    "    x = *(q + a);\n"
    "    x = f(&x);\n"
    "    x = f(q + 2);\n"
    "    y = sizeof(struct S);\n"
    "    x = p == 0;\n"
    "    x = gs.a[1];\n"
    "    y = ((struct T*)p)->m + t->m + *lp;\n"
    "    x = 1 << 0;\n"
    "    y = (long)3;\n"
    "    takev(&gv);\n"
    "    y = gla[1];\n"
    "    x = A64 + A64 + a;\n"
    "    gd = 1.5 + 2.5;\n"
    "    return y + x;\n"
    "}\n"
    "long five(void)\n"
    "{\n"
    "    return 5;\n"
    "}\n"
    "char sixty(void)\n"
    "{\n"
    "    return 65;\n"
    "}\n"
    "int* none(void)\n"
    "{\n"
    "    return 0;\n"
    "}\n";

// The reads' fixture written to `directory` and read with `options` by the census of what reads T's record, its
// windows cut at `window` records (0 for the kernel's own cut): whether nothing changed made what was made, the
// records the census leaves are the fixture's two kinds and every other was measured, the measured records by kind and
// class are the fixture's, and so are the reads the fixture is there for.
static bool node_reads_test(const char* directory, uint32_t window, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/reads.c", directory);
    bool valid = parse_test_write(path, node_reads_test_text, strlen(node_reads_test_text));
    NodeShadow shadow = { .facts = true, .facts_operators = true, .facts_reads = true, .facts_mode = BUSTER_NODE_FACTS_WHOLE, .facts_window = window, .passes = 1, .repeat = 1 };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    const NodeReadsCounts* counts = &shadow.reads;
    // The records by kind (a leaf, a lookup, another operator) and class (nothing wide read, a named type, a copy, a
    // value new at the record).
    static const uint64_t classes[BUSTER_NODE_READS_KINDS][BUSTER_NODE_READS_CLASSES] = {
        { 0, 0, 52, 0 },
        { 23, 1, 2, 4 },
        { 24, 1, 9, 5 },
    };
    // By kind: the measured records each of whose wide fields is read (its type, its object, its payload, a member's
    // offset, a conversion's type), those whose read field is new at the record, and those each reader (T, C, X) reads
    // a wide field of.
    static const uint64_t fields[BUSTER_NODE_READS_KINDS][5] = {
        { 14, 33, 16, 0, 0 },
        { 5, 1, 1, 4, 2 },
        { 8, 1, 7, 0, 3 },
    };
    static const uint64_t fresh[BUSTER_NODE_READS_KINDS][5] = {
        { 0, 0, 0, 0, 0 },
        { 1, 0, 1, 3, 0 },
        { 0, 0, 5, 0, 0 },
    };
    static const uint64_t by_reader[BUSTER_NODE_READS_KINDS][BUSTER_NODE_READS_READERS] = {
        { 24, 2, 40 },
        { 5, 2, 7 },
        { 8, 3, 11 },
    };
    // The records C or X decide on an identity of: the two calls' callee.
    static const uint64_t identities[BUSTER_NODE_READS_KINDS] = { 2, 0, 0 };
    const uint32_t decides = (uint32_t)__builtin_ctz(BUSTER_NODE_READS_TYPE_DECIDES);
    const uint32_t carried = (uint32_t)__builtin_ctz(BUSTER_NODE_READS_TYPE_CARRIED);
    const uint32_t written = (uint32_t)__builtin_ctz(BUSTER_NODE_READS_PAYLOAD_CARRIED);
    // The records by kind, and those the census left: the long sum's 260, the floating fold's four operands, the
    // cast's one, and the volatile object's leaf under an address the kernel leaves to T.
    static const uint64_t records[BUSTER_NODE_READS_KINDS] = { 187, 31, 169 };
    static const uint64_t left[BUSTER_NODE_READS_KINDS] = { 135, 1, 130 };
    bool held = shadow.facts_differences == 0 && shadow.facts_list_differences == 0 && shadow.facts_failures == 0 && counts->same_differences == 0 && counts->misplaced == 0 &&
                counts->left_consumers == 4 && counts->long_expressions == 1 && counts->long_records == 260 && counts->same_runs != 0 && counts->rule_runs != 0 &&
                counts->sweep_runs != 0;
    for (uint32_t kind = 0; kind < BUSTER_NODE_READS_KINDS; kind += 1)
    {
        held = held && counts->kind_records[kind] == records[kind] && counts->kind_measured[kind] == records[kind] - left[kind] &&
               counts->kind_identities[kind] == identities[kind] && counts->kind_lone[kind] == 0 && counts->kind_fits[kind] == (kind == BUSTER_NODE_READS_KIND_OPERATOR) &&
               counts->kind_unseen[kind] == (kind == BUSTER_NODE_READS_KIND_OPERATOR);
        for (uint32_t class = 0; class < BUSTER_NODE_READS_CLASSES; class += 1)
        {
            held = held && counts->kind_classes[kind][class] == classes[kind][class];
        }
        for (uint32_t field = 0; field < 5; field += 1)
        {
            held = held && counts->kind_fields[kind][field] == fields[kind][field] && counts->kind_new[kind][field] == fresh[kind][field];
        }
        for (uint32_t reader = 0; reader < BUSTER_NODE_READS_READERS; reader += 1)
        {
            held = held && counts->kind_readers[kind][reader] == by_reader[kind][reader];
        }
    }
    // The reads the fixture is there for: a call's rule decides on its argument's type (`&x`, `&gv`, `q + 2`), a store's
    // conversion and a dereference on a pointer sum's; a member access carries a store's and a cast's type to its
    // offset; X writes a fold's, a negative literal's and a sizeof's payload; whether a member's offset in an
    // address constant is zero decides X's entries; and a literal's value decides for a rule of T (a null pointer
    // constant's zero in `p == 0` and in `return 0` as a pointer, and two more).
    held = held && counts->bits[BUSTER_NODE_READS_T][CHECK_OP_ADDRESS][decides] == 2 && counts->bits[BUSTER_NODE_READS_T][CHECK_OP_ADD][decides] == 4 &&
           counts->bits[BUSTER_NODE_READS_T][CHECK_OP_ASSIGN][carried] == 1 && counts->bits[BUSTER_NODE_READS_T][CHECK_OP_CAST][carried] == 1 &&
           counts->bits[BUSTER_NODE_READS_X][CHECK_OP_ADD][written] == 1 && counts->bits[BUSTER_NODE_READS_X][CHECK_OP_NEGATIVE][written] == 1 &&
           counts->bits[BUSTER_NODE_READS_X][CHECK_OP_SIZEOF][written] == 1 && counts->payload_values[CHECK_OP_MEMBER][1] == 1 &&
           counts->bits[BUSTER_NODE_READS_T][BUSTER_NODE_READS_LITERAL][(uint32_t)__builtin_ctz(BUSTER_NODE_READS_PAYLOAD_DECIDES)] == 4;
    // The flags: C reads whether each store's destination is a bit-field, and T's rules whether an operand is one; a
    // store's side effect is handed on by its consumer, which is no read of it, and C reads it of the six stores of a
    // constant; a call hands its argument's flags on, so its rule reads no flag of `&x`. And an identifier's object's
    // kind (a local, a global) decides for C.
    const uint32_t bit_field = (uint32_t)__builtin_ctz(BUSTER_CHECK_BIT_FIELD);
    const uint32_t side_effect = (uint32_t)__builtin_ctz(BUSTER_CHECK_SIDE_EFFECT);
    const uint32_t flags = (uint32_t)__builtin_ctz(BUSTER_NODE_READS_FLAGS);
    const uint32_t object_kind = (uint32_t)__builtin_ctz(BUSTER_NODE_READS_OBJECT_KIND);
    held = held && counts->flag_bits[BUSTER_NODE_READS_C][CHECK_OP_VALUE][bit_field] == 17 && counts->flag_bits[BUSTER_NODE_READS_T][CHECK_OP_VALUE][bit_field] == 23 &&
           counts->flag_bits[BUSTER_NODE_READS_T][CHECK_OP_ASSIGN][side_effect] == 0 && counts->flag_bits[BUSTER_NODE_READS_C][CHECK_OP_ASSIGN][side_effect] == 6 &&
           counts->bits[BUSTER_NODE_READS_T][CHECK_OP_ADDRESS][flags] == 0 && counts->bits[BUSTER_NODE_READS_C][CHECK_OP_VALUE][object_kind] == 33;
    // A type of another word: the pointer sums' and the dereferenced sum's for a rule of T, and most leaves'; three
    // identifiers' for C.
    // C decides on the callee's type's identity at the two calls of `f`. And a conversion's flags: C reads whether a
    // discard's and a call's argument's are to a union.
    const uint32_t type_word = (uint32_t)__builtin_ctz(BUSTER_NODE_READS_TYPE_WORD);
    held = held && counts->bits[BUSTER_NODE_READS_T][CHECK_OP_ADD][type_word] == 4 && counts->bits[BUSTER_NODE_READS_C][CHECK_OP_VALUE][type_word] == 3 &&
           counts->bits[BUSTER_NODE_READS_T][CHECK_OP_VALUE][type_word] == 35 && counts->bits[BUSTER_NODE_READS_T][BUSTER_NODE_READS_LITERAL][type_word] == 11 &&
           counts->bits[BUSTER_NODE_READS_C][CHECK_OP_VALUE][decides] == 2 &&
           counts->conversion_bits[BUSTER_NODE_READS_C][BUSTER_NODE_READS_DISCARD][(uint32_t)__builtin_ctz(BUSTER_CHECK_CONVERT_TO_UNION)] == 18 &&
           counts->conversion_bits[BUSTER_NODE_READS_C][CHECK_OP_CALL][(uint32_t)__builtin_ctz(BUSTER_CHECK_CONVERT_TRANSPARENT_UNION)] == 3;
    // And where each read field comes from.
    held = held && counts->sources[BUSTER_NODE_READS_FIELD_TYPE][CHECK_OP_ADDRESS][BUSTER_NODE_READS_SOURCE_ROW] == 2 &&
           counts->sources[BUSTER_NODE_READS_FIELD_TYPE][CHECK_OP_ADDRESS][BUSTER_NODE_READS_SOURCE_NEW] == 0 &&
           counts->sources[BUSTER_NODE_READS_FIELD_TYPE][CHECK_OP_CAST][BUSTER_NODE_READS_SOURCE_INPUT] == 1 &&
           counts->sources[BUSTER_NODE_READS_FIELD_TYPE][CHECK_OP_ASSIGN][BUSTER_NODE_READS_SOURCE_OPERAND] == 1 &&
           counts->sources[BUSTER_NODE_READS_FIELD_TYPE][CHECK_OP_ADD][BUSTER_NODE_READS_SOURCE_OPERAND] == 3 &&
           counts->sources[BUSTER_NODE_READS_FIELD_PAYLOAD][CHECK_OP_ADD][BUSTER_NODE_READS_SOURCE_NEW] == 1 &&
           counts->sources[BUSTER_NODE_READS_FIELD_PAYLOAD][CHECK_OP_NEGATIVE][BUSTER_NODE_READS_SOURCE_NEW] == 1 &&
           counts->sources[BUSTER_NODE_READS_FIELD_PAYLOAD][CHECK_OP_SIZEOF][BUSTER_NODE_READS_SOURCE_ROW] == 1 &&
           counts->sources[BUSTER_NODE_READS_FIELD_OBJECT][CHECK_OP_MEMBER][BUSTER_NODE_READS_SOURCE_OPERAND] == 1 &&
           counts->sources[BUSTER_NODE_READS_FIELD_PAYLOAD][CHECK_OP_SHIFT_LEFT][BUSTER_NODE_READS_SOURCE_NEW] == 1 &&
           counts->sources[BUSTER_NODE_READS_FIELD_PAYLOAD][CHECK_OP_SHIFT_LEFT][BUSTER_NODE_READS_SOURCE_OPERAND] == 0 &&
           counts->sources[BUSTER_NODE_READS_FIELD_PAYLOAD][CHECK_OP_CONVERT][BUSTER_NODE_READS_SOURCE_OPERAND] == 1 &&
           counts->sources[BUSTER_NODE_READS_FIELD_PAYLOAD][CHECK_OP_CONVERT][BUSTER_NODE_READS_SOURCE_NEW] == 1 &&
           counts->sources[BUSTER_NODE_READS_FIELD_PAYLOAD][CHECK_OP_CAST][BUSTER_NODE_READS_SOURCE_NEW] == 1 &&
           counts->sources[BUSTER_NODE_READS_FIELD_CONVERTED][CHECK_OP_SUBSCRIPT][BUSTER_NODE_READS_SOURCE_ROW] == 2;
    if (!valid || !held)
    {
        fprintf(stderr, "test-checker: node-reads: window %u: %s: %" PRIu64 " differences with nothing changed, %" PRIu64 " misplaced, %" PRIu64 " consumers left:\n", window,
                valid ? "read" : "NOT READ", counts->same_differences, counts->misplaced, counts->left_consumers);
        for (uint32_t kind = 0; kind < BUSTER_NODE_READS_KINDS; kind += 1)
        {
            fprintf(stderr, "        { %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 " }, // %" PRIu64 " of %" PRIu64 " measured\n", counts->kind_classes[kind][0],
                    counts->kind_classes[kind][1], counts->kind_classes[kind][2], counts->kind_classes[kind][3], counts->kind_measured[kind], counts->kind_records[kind]);
        }
    }
    unlink(path);
    return valid && held;
}

// The front at its limits, on generated text read with `options`, the front run as `front` asks (BUSTER_NODE_FRONT_*:
// after the walk acted at a chunk or before it, whole or lean). `boundaries`: a body of statements whose tokens are 29 a round, so that the chunks' ends (1,024
// postorder positions each) fall at every place within a call of a library function by its name and through its address:
// a callee's record waits for its call in the next chunk, kept with its use. Else (`!boundaries`) more values waiting than
// the front's stack holds: an automatic array's initializer of some 4,150 elements with ten calls about its 4,096th
// value, one of them astride the stack's last entry, which must make every waiting value unknown (a reset) and leave the
// call to the walk. Whether every record was the walk's node, none was left waiting, and the stack was reset as expected.
static bool node_front_test_limits(const char* directory, bool boundaries, uint8_t front, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/limits.c", directory);
    const uint32_t capacity = 1u << 18;
    char* text = malloc(capacity);
    uint32_t length = 0;
    bool valid = text != NULL;
    if (valid && boundaries)
    {
        length += (uint32_t)snprintf(text + length, capacity - length,
                                     "unsigned long strlen(const char*);\nvoid sink(void*);\nint late[5];\nint lb(int a)\n{\n    int x = a;\n");
        for (uint32_t round = 0; round < 1100 && length + 128 < capacity; round += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, "    x += (int)(&strlen)(\"ab\"); x += (int)strlen(\"cd\"); x++; x++;\n");
        }
        // The address of an array no declaration points to: T makes its pointer type itself, in a chunk where nothing
        // else makes a type (BUSTER_NODE_FRONT_TYPES: at reader time its word must be there when T reads it).
        length += (uint32_t)snprintf(text + length, capacity - length, "    sink(&late);\n    return x;\n}\n");
    }
    else if (valid)
    {
        length += (uint32_t)snprintf(text + length, capacity - length, "int f2(int, int);\nint lc(int a, int b)\n{\n    int big[4200] = {\n");
        for (uint32_t element = 0; element < 4150 && length + 64 < capacity; element += 1)
        {
            const bool call = element >= 4089 && element < 4099;
            length += (uint32_t)snprintf(text + length, capacity - length, call ? "f2(a, b),\n" : (element & 31) == 31 ? "1,\n" : "1, ");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, "1 };\n    return big[0];\n}\n");
    }
    valid = valid && length + 256 < capacity && parse_test_write(path, text, length);
    NodeShadow shadow = {
        .scalar = true,
        .front = front,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    // In a build without the vector paths the reader's walk is the scalar one, and the front must make nothing.
    const bool fronts = BUSTER_HOST_VECTOR != 0;
    const bool typed = (front & BUSTER_NODE_FRONT_TYPES) == 0 || !fronts ? shadow.typed_expressions == 0 && shadow.typed_compared == 0 :
                                                                           shadow.typed_differences == 0 && shadow.typed_compared != 0 && shadow.typed_unrun == 0 &&
                                                                               shadow.typed_object_differences == 0 && shadow.typed_library_differences == 0;
    // C and X there too, where asked: the same tapes across the chunks' ends, none left out.
    const bool swept = (front & BUSTER_NODE_FRONT_SWEEPS) == 0 || !fronts ? shadow.swept_expressions == 0 && shadow.swept_compared == 0 :
                                                                            shadow.swept_differences == 0 && shadow.swept_compared != 0 && shadow.swept_unkept == 0;
    const bool same = shadow.front_differences == 0 && shadow.front_checked == shadow.front_records && shadow.front_waiting == 0 &&
                      (fronts ? shadow.front_units == 1 && shadow.front_unread == 0 && shadow.front_records != 0 && (shadow.front_resets != 0) == !boundaries :
                                shadow.front_units == 0 && shadow.front_records == 0) && typed && swept;
    if (!valid || !same)
    {
        fprintf(stderr, "test-checker: node-front %s%s: %s: records %" PRIu64 ", compared %" PRIu64 ", %" PRIu64 " differ, %" PRIu64 " resets, %" PRIu64
                " values left waiting, in %" PRIu64 " units (%s)\n",
                boundaries ? "chunk boundaries" : "full stack", (front & BUSTER_NODE_FRONT_LEAN) != 0 ? " (lean)" : "", valid ? "read" : "NOT READ", shadow.front_records,
                shadow.front_checked,
                shadow.front_differences, shadow.front_resets, shadow.front_waiting, shadow.front_units,
                fronts ? "expected in the one" : "expected in none: no vector paths in this build");
    }
    free(text);
    unlink(path);
    return valid && same;
}

static __attribute__((noinline)) void check_test_node_census(CheckTest* test)
{
    char directory[] = "/tmp/buster-node-census-XXXXXX";
    const bool ready = mkdtemp(directory) != 0;
    check_test_expect(test, ready, "node-census fixtures' directory is made");
    char* loud[] = { "-nostdinc", "-Wall", "-Wextra", "-Wshift-sign-overflow", "-Wassign-enum" };
    char* quiet[] = { "-nostdinc" };
    uint64_t system = 0;
    check_test_expect(test, ready && node_census_test_fixture(directory, "warnings.c", node_census_test_warnings, loud, BUSTER_ARRAY_LENGTH(loud), &system),
                      "node-census refuses each expression the compiler warns of, by the test that stands for its warning");
    check_test_expect(test, ready && node_census_test_fixture(directory, "clean.c", node_census_test_clean, loud, BUSTER_ARRAY_LENGTH(loud), &system),
                      "node-census takes the clean idioms its silence tests must not refuse");
    check_test_expect(test, ready && node_census_test_fixture(directory, "set.c", node_census_test_set, loud, BUSTER_ARRAY_LENGTH(loud), &system),
                      "node-census refuses what the machine's set has no code for, by its test");
    check_test_expect(test, ready && node_census_test_fixture(directory, "quiet.c", node_census_test_quiet, quiet, BUSTER_ARRAY_LENGTH(quiet), &system),
                      "node-census refuses nothing for warnings that are off by default");
    check_test_expect(test, ready && node_census_test_fixture(directory, "atomic.c", node_census_test_atomic, quiet, BUSTER_ARRAY_LENGTH(quiet), &system),
                      "node-census refuses atomic record accesses by type without refusing ordinary members or the whole unit");
    char headers[512];
    char header[640];
    snprintf(headers, sizeof(headers), "%s/system", directory);
    snprintf(header, sizeof(header), "%s/zero.h", headers);
    const bool written = ready && mkdir(headers, 0700) == 0 && parse_test_write(header, node_census_test_system_header, strlen(node_census_test_system_header));
    char* isystem[] = { "-nostdinc", "-Wall", "-Wextra", "-isystem", headers };
    system = 0;
    check_test_expect(test, written && node_census_test_fixture(directory, "system.c", node_census_test_system, isystem, BUSTER_ARRAY_LENGTH(isystem), &system) &&
                                system == 2,
                      "node-census takes a system header's macros in a user's expression and counts them apart");
    // The same fixtures with the census's tests at reader time, after T there: the machine's verdict before the pipeline
    // is never a take the census refuses, and every test's bit is the census's.
    check_test_expect(test, ready && node_tests_test_fixture(directory, "warnings.c", node_census_test_warnings, loud, BUSTER_ARRAY_LENGTH(loud), BUSTER_NODE_FRONT_AFTER, 36, 3),
                      "node-tests: at reader time the tests refuse each expression the compiler warns of, as the census does after the pipeline");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "clean.c", node_census_test_clean, loud, BUSTER_ARRAY_LENGTH(loud), BUSTER_NODE_FRONT_BEFORE, 0, 6),
                      "node-tests: at reader time the tests refuse none of the clean idioms");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "set.c", node_census_test_set, loud, BUSTER_ARRAY_LENGTH(loud),
                                                             BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN, 10, 6),
                      "node-tests: at reader time the set's tests and T's own refusals leave nothing the census refuses");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "quiet.c", node_census_test_quiet, quiet, BUSTER_ARRAY_LENGTH(quiet), BUSTER_NODE_FRONT_AFTER, 0, 1),
                      "node-tests: at reader time warnings that are off by default refuse nothing");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "atomic.c", node_census_test_atomic, quiet, BUSTER_ARRAY_LENGTH(quiet), BUSTER_NODE_FRONT_AFTER, 2, 0),
                      "node-tests: at reader time atomic record accesses are refused by type");
    check_test_expect(test, written && node_tests_test_fixture(directory, "system.c", node_census_test_system, isystem, BUSTER_ARRAY_LENGTH(isystem), BUSTER_NODE_FRONT_AFTER, 0, 0),
                      "node-tests: at reader time a system header's macros in a user's expression refuse nothing");
    if (written)
    {
        unlink(header);
        rmdir(headers);
    }
    // With T at reader time too (BUSTER_NODE_FRONT_TYPES): the shadow's records on every expression both type; by the rows
    // there against the action alone here in the --scalar run. And with C and X there (BUSTER_NODE_FRONT_SWEEPS) in the
    // --scalar and the lean runs: their tapes, patched with the program's symbols, the shadow's own.
    check_test_expect(test, ready && node_shadow_test_fixture(directory, false, BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_TYPES, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-shadow: the machine takes its codes with the pipeline's values and entries, T's rows the action's, and refuses the rest by reason");
    check_test_expect(test, ready && node_shadow_test_fixture(directory, true, BUSTER_NODE_FRONT_BEFORE | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS, loud,
                                                              BUSTER_ARRAY_LENGTH(loud)),
                      "node-shadow --scalar: T by the action alone gives the pipeline's values and entries, and C and X at reader time the shadow's tapes");
    // Plain char unsigned: T's rules convert a char constant by the unit's signedness, not the bake's.
    char* unsigned_char[] = { "-nostdinc", "-Wall", "-Wextra", "-Wshift-sign-overflow", "-Wassign-enum", "-funsigned-char" };
    check_test_expect(test, ready && node_shadow_test_fixture(directory, false,
                                                              BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS,
                                                              unsigned_char, BUSTER_ARRAY_LENGTH(unsigned_char)),
                      "node-shadow -funsigned-char: the machine converts plain char constants as the unit's char is signed, at reader time too");
    // Without warnings the census takes a for statement's clauses too, and C and X at reader time are compared on them.
    check_test_expect(test, ready && node_shadow_test_fixture(directory, false,
                                                              BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS, quiet,
                                                              BUSTER_ARRAY_LENGTH(quiet)),
                      "node-shadow without warnings: a for statement's clauses' tapes at reader time are the shadow's");
    check_test_expect(test, ready && node_facts_test_fixture(directory, BUSTER_NODE_FACTS_WHOLE, 0, false, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-facts: every leaf and lookup the fact words' rules make is T's record, and what they leave is the fixture's");
    check_test_expect(test, ready && node_facts_test_fixture(directory, BUSTER_NODE_FACTS_WHOLE, 16, false, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-facts: the same records where a window is cut every 16 records");
    check_test_expect(test, ready && node_facts_test_fixture(directory, BUSTER_NODE_FACTS_WHOLE, BUSTER_NODE_FACTS_WINDOW, true, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-facts: the same records where a window packs the bodies' expressions to its most");
    check_test_expect(test, ready && node_facts_test_fixture(directory, BUSTER_NODE_FACTS_LEAN, 0, false, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-facts lean: a record's first half is T's, and the conversions and discards left out are their rules'");
    check_test_expect(test, ready && node_facts_test_fixture(directory, BUSTER_NODE_FACTS_LEAN, 16, false, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-facts lean: the same where a window is cut every 16 records");
    check_test_expect(test, ready && node_facts_test_fixture(directory, BUSTER_NODE_FACTS_LOADS, 0, false, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-facts loads: a lookup's type, word and member's offset from its load alone are T's");
    check_test_expect(test, ready && node_facts_test_fixture(directory, BUSTER_NODE_FACTS_LOADS, BUSTER_NODE_FACTS_WINDOW, true, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-facts loads: the same where a window packs the bodies' expressions to its most");
    check_test_expect(test, ready && node_facts_test_operators(directory, BUSTER_NODE_FACTS_WHOLE, 0, false, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-facts operators: every operator the kernel's rules make is T's record, and what they leave is the fixture's");
    check_test_expect(test, ready && node_facts_test_operators(directory, BUSTER_NODE_FACTS_WHOLE, 16, false, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-facts operators: the same records where a window is cut every 16 records");
    check_test_expect(test, ready && node_facts_test_operators(directory, BUSTER_NODE_FACTS_WHOLE, BUSTER_NODE_FACTS_WINDOW, true, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-facts operators: the same records where a window packs the bodies' expressions to its most");
    check_test_expect(test, ready && node_facts_test_operators(directory, BUSTER_NODE_FACTS_LEAN, 0, false, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-facts operators lean: a record's first half is T's, and the conversions left out are their rules'");
    check_test_expect(test, ready && node_facts_test_operators(directory, BUSTER_NODE_FACTS_LEAN, 16, false, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-facts operators lean: the same where a window is cut every 16 records");
    check_test_expect(test, ready && node_reads_test(directory, 0, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-reads: the readers of a record's wide fields are found, and each read field's source is the fixture's");
    check_test_expect(test, ready && node_reads_test(directory, 16, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-reads: the same reads where a window is cut every 16 records");
    // The machine's own front at its limits (step 1b): a chunk's end at every place within a call, and a full stack.
    check_test_expect(test, ready && node_front_test_limits(directory, true, BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_TYPES, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-front: a callee waiting across a chunk's end keeps its use, a library call's mark the walk's");
    check_test_expect(test, ready && node_front_test_limits(directory, true, BUSTER_NODE_FRONT_BEFORE, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-front before the walk: a callee waiting across a chunk's end keeps its use");
    check_test_expect(test, ready && node_front_test_limits(directory, true,
                                                            BUSTER_NODE_FRONT_BEFORE | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS, loud,
                                                            BUSTER_ARRAY_LENGTH(loud)),
                      "node-front, lean: a value waiting across a chunk's end gets its consumer's distance and its place there, and C and X its tape");
    check_test_expect(test, ready && node_front_test_limits(directory, false, BUSTER_NODE_FRONT_AFTER, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-front: a full stack makes every waiting value unknown and leaves the call astride its end to the walk");
    check_test_expect(test, ready && node_front_test_limits(directory, false, BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-front, lean: a full stack makes every waiting value unknown");
    // Every warning has the census's class (node_census_warning_classes): one added since fails here until it has one.
    uint32_t unclassified = 0;
    for (uint32_t id = 0; id < CHECK_WARNING_ID_COUNT; id += 1)
    {
        if (node_census_warning_classes[id] == NODE_CENSUS_WARNING_UNCLASSIFIED)
        {
            unclassified += 1;
            fprintf(stderr, "test-checker: node-census: warning %u (%s) has no class of the census's\n", id,
                    id < BUSTER_ARRAY_LENGTH(check_warning_descriptions) && check_warning_descriptions[id].option != NULL ? check_warning_descriptions[id].option : "-");
        }
    }
    check_test_expect(test, unclassified == 0, "node-census classifies every warning: a test stands for it, or it is no expression's");
    // The command refuses a run without a file.
    char* missing[] = { "--list", "1" };
    fflush(stdout);
    const int saved = dup(STDERR_FILENO);
    const int null = open("/dev/null", O_WRONLY);
    const bool silenced = saved >= 0 && null >= 0 && dup2(null, STDERR_FILENO) >= 0;
    const int empty = silenced ? run_node_census(BUSTER_ARRAY_LENGTH(missing), missing) : 0;
    if (saved >= 0)
    {
        dup2(saved, STDERR_FILENO);
        close(saved);
    }
    if (null >= 0)
    {
        close(null);
    }
    node_census_list = 0;
    check_test_expect(test, silenced && empty == 1, "measure-node-machine refuses a run without a file");
    if (ready)
    {
        rmdir(directory);
    }
}

#endif
