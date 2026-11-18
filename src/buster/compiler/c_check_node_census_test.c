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
    "    for (i = 0; i < 4; i += 1) x += i; // refuse: for-loop-analysis\n"
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
    "int loose();\n"
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
    "    x = loose(i); // refuse: callee\n"
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

// What warns without an option, read with the default warnings alone: with -Wall -Wextra a comparison goes to the
// census's code whatever its operands are (-Wsign-compare), and a shift of a constant too, which hides the own form's
// other conditions on them ("The tests in the machine's own form"). So these lines' refusals are decided by the
// conditions the first fixture can't reach: a mix of enumerations at a comparison and at another operator; a comparison
// under a bitwise operator, an assignment as a condition and a chained comparison; a constant its conversion changes,
// in an initializer, an assignment and a conditional's third operand alone; a shift's count past its operand's width
// and under 64, and a negative constant shifted; a constant outside its other operand's type (zero for an unsigned
// one's ordering, a negative one, 127 and past it for a signed char, 1 and 2 for a bool); a subscript at its array's
// length; a bit-field compared; the front's one operator outside the census's set (`__extension__`, a selection there);
// a comparison as a statement whose operand writes (no discarded value: the comparison's own test); a call's aggregate
// argument; a write and a read of one object, as a subscript, an operand and an assignment's value; and a returned
// array and a returned address.
// Seventy of one name summed: 139 records, so that the other operand of what is above them is further back than the
// block before.
#define BUSTER_NODE_CENSUS_TEST_TEN(name) name " + " name " + " name " + " name " + " name " + " name " + " name " + " name " + " name " + " name
// Ten of a statement, and seventy: more of them than a block of the loop's has records.
#define BUSTER_NODE_CENSUS_TEST_TEN_OF(text) text " " text " " text " " text " " text " " text " " text " " text " " text " " text
#define BUSTER_NODE_CENSUS_TEST_SEVENTY_OF(text)                                                                                                                              \
    BUSTER_NODE_CENSUS_TEST_TEN_OF(text) " " BUSTER_NODE_CENSUS_TEST_TEN_OF(text) " " BUSTER_NODE_CENSUS_TEST_TEN_OF(text) " " BUSTER_NODE_CENSUS_TEST_TEN_OF(text) " "        \
    BUSTER_NODE_CENSUS_TEST_TEN_OF(text) " " BUSTER_NODE_CENSUS_TEST_TEN_OF(text) " " BUSTER_NODE_CENSUS_TEST_TEN_OF(text)
#define BUSTER_NODE_CENSUS_TEST_SEVENTY(name)                                                                                                                                 \
    BUSTER_NODE_CENSUS_TEST_TEN(name) " + " BUSTER_NODE_CENSUS_TEST_TEN(name) " + " BUSTER_NODE_CENSUS_TEST_TEN(name) " + " BUSTER_NODE_CENSUS_TEST_TEN(name) " + "             \
    BUSTER_NODE_CENSUS_TEST_TEN(name) " + " BUSTER_NODE_CENSUS_TEST_TEN(name) " + " BUSTER_NODE_CENSUS_TEST_TEN(name)
static const char node_census_test_defaults[] =
    "enum A { A0, A1 };\n"
    "enum B { B0, B1 };\n"
    "struct F { unsigned f : 3; int n; };\n"
    "void use(struct F);\n"
    "int g(int a, int b, int c, unsigned u, unsigned char uc, signed char sc, _Bool bb, enum A ea, enum B eb, struct F* sf, int* p, int i, float fl)\n"
    "{\n"
    "    int x = 0;\n"
    "    int arr[4];\n"
    "    x = ea == eb; // refuse: enum-mix\n"
    "    x = ea | eb; // refuse: enum-mix\n"
    "    x = ea == A1; // take\n"
    "    x = a & b == c; // refuse: parentheses\n"
    "    if (a = b) x = 1; // refuse: parentheses\n"
    "    x = a < b < c; // refuse: parentheses\n"
    "    signed char narrow = 200; // refuse: constant\n"
    "    short small = 40000; // refuse: constant\n"
    "    int whole = 1.5; // refuse: constant\n"
    "    whole = 2.5; // refuse: constant\n"
    "    fl = a ? fl : 16777217; // refuse: constant\n"
    "    x = a << 40; // refuse: constant\n"
    "    x = -1 << 3; // refuse: constant\n"
    "    x = u < 0; // refuse: constant\n"
    "    x = uc == -1; // refuse: constant\n"
    "    x = sc > 127; // refuse: constant\n"
    "    x = bb > 1; // refuse: constant\n"
    "    x = bb == 2; // refuse: constant\n"
    "    x = arr[4]; // refuse: constant\n"
    "    x = sf->f == 9; // refuse: bit-field\n"
    "    x = __extension__ (a + b); // refuse: operator\n"
    "    a == i++; // refuse: unused-value\n"
    "    use(*sf); // refuse: aggregate-value\n"
    "    p[i++] = i; // refuse: unsequenced\n"
    "    x = i + i++; // refuse: unsequenced\n"
    "    i = i++; // refuse: unsequenced\n"
    "    return x + narrow + small + whole + sf->n; // take\n"
    "}\n"
    "int* h(void)\n"
    "{\n"
    "    int a[2];\n"
    "    return a; // refuse: return-address\n"
    "}\n"
    "int* k(void)\n"
    "{\n"
    "    int a = 0;\n"
    "    return &a; // refuse: return-address\n"
    "}\n"
    "int m(int i)\n"
    "{\n"
    "    return i + i++; // refuse: unsequenced\n"
    "}\n"
    // The window's own places ("The tests on the window"): a name's leaf without a line, whose declaration is asked
    // for at each leaf; a register object's member, whose flags are an operator's record's too; an ordering under a
    // bitwise operator; an operator listed for an address constant the record doesn't hold whole alone (a static
    // record's bit-field); and expressions of some 140 records, more than two blocks of the loop's and three words of
    // an expression's bits, whose test is their last operator's (a constant operand further back than the block
    // before, as a first and as a second operand, once with a sum beside it that lists the operator too and once the
    // only thing that does; a comparison under a bitwise operator; a bit-field) or is asked of records in their last
    // words; an initializer of as many, which has no root's record to give its tokens' span its end; and an aggregate
    // that only a root's record copies.
    "enum Wide : unsigned __int128 { WIDE __attribute__((deprecated)) = (unsigned __int128)1 << 70 };\n"
    "struct R { int n; };\n"
    "static struct F gs;\n"
    "int w(int a, int b, int c, float fl, struct F* sf)\n"
    "{\n"
    "    int x = 0;\n"
    "    register struct R rs;\n"
    "    x = WIDE > 1; // refuse: declaration-flag\n"
    "    x = rs.n; // refuse: declaration-flag\n"
    "    x = a & b < c; // refuse: parentheses\n"
    "    x = gs.f + a; // refuse: bit-field\n"
    "    x = -1 << (" BUSTER_NODE_CENSUS_TEST_SEVENTY("a") "); // refuse: constant\n"
    "    fl = 16777217 + (" BUSTER_NODE_CENSUS_TEST_SEVENTY("fl") "); // refuse: constant\n"
    "    fl = a ? 16777217 : (" BUSTER_NODE_CENSUS_TEST_SEVENTY("fl") "); // refuse: constant\n"
    "    x = (" BUSTER_NODE_CENSUS_TEST_SEVENTY("a") ") & b == c; // refuse: parentheses\n"
    "    x = (" BUSTER_NODE_CENSUS_TEST_SEVENTY("a") ") + sf->f; // refuse: bit-field\n"
    "    int sum = " BUSTER_NODE_CENSUS_TEST_SEVENTY("a") "; // take\n"
    "    return x + sum; // take\n"
    "}\n"
    // The loop's quiet rule: an operator listed for a constant operand alone that the loop leaves out, one of each
    // class (a sum, a unary one, an equality with zero and with a small value, a pointer's with zero, an ordering with
    // a small value and with zero against a signed operand, a shift by 3 and by 0, a division, an assignment of 5 and
    // of 0, a compound one, a return's conversion of 1 and of 0, a pointer's sum with 1, and a sum whose constant is in
    // the block before its record's), with the constant second and first; and those it must keep listed: 127 and a
    // value further past 126 (126 is left out), zero against an unsigned operand, a constant of a 64-bit type whose
    // low half alone is small, an enumeration's operand, a bool's, a floating one's under an equality, a sum and a unary
    // operator, a 128-bit one's, a bit-field's, a pointer to void's, a pointer against 1 (which T's rule refuses), a
    // shift of a sum, a shift count and a divisor that fire, a constant that its target doesn't hold (stored,
    // initialized and returned), and a return into a float and into a pointer. An initializer's root is no record of
    // the window's: its lines are the root's own test's.
    "enum Q { Q0, Q1 };\n"
    "int q1(void)\n"
    "{\n"
    "    return 1; // take\n"
    "}\n"
    "char q2(void)\n"
    "{\n"
    "    return 300; // refuse: constant\n"
    "}\n"
    "float q3(void)\n"
    "{\n"
    "    return 1; // take\n"
    "}\n"
    "int q4(void)\n"
    "{\n"
    "    return 0; // take\n"
    "}\n"
    "int* q5(void)\n"
    "{\n"
    "    return 0; // take\n"
    "}\n"
    "int q(int a, int b, unsigned u, long l, int* p, enum Q e, _Bool bl, float fl, __int128 w, void* vp, struct F* sf, char ch)\n"
    "{\n"
    "    int x = 7; // take\n"
    "    char big = 300; // refuse: constant\n"
    "    _Bool b2 = 1; // take\n"
    "    float f2 = 1; // take\n"
    "    ch = 300; // refuse: constant\n"
    "    x = 200 == a; // take\n"
    "    x = 0 <= u; // refuse: constant\n"
    "    x = 1 == bl; // take\n"
    "    x = -1 << 3; // refuse: constant\n"
    "    x = 0 == p; // take\n"
    "    x = a + 1; // take\n"
    "    x = a & 255; // take\n"
    "    x = -1; // take\n"
    "    fl = -1.5f; // take\n"
    "    x = a == 0; // take\n"
    "    x = a != 5; // take\n"
    "    x = p == 0; // take\n"
    "    x = a < 5; // take\n"
    "    x = a >= 0; // take\n"
    "    x = u < 5; // take\n"
    "    x = u < 1; // take\n"
    "    x = a << 3; // take\n"
    "    x = a >> 0; // take\n"
    "    x = 0 == a; // take\n"
    "    x = 0 < a; // take\n"
    "    x = 0; // take\n"
    "    x = a + 1.5; // take\n"
    "    p = p + 1; // take\n"
    "    p = 1 + p; // take\n"
    "    x = p == 1; // refuse: action-warning\n"
    "    x = 1 == p; // refuse: action-warning\n"
    "    x = a / 2; // take\n"
    "    x = 5; // take\n"
    "    x += 2; // take\n"
    "    x = a == 200; // take\n"
    "    x = u >= 0; // refuse: constant\n"
    "    x = l == 5L; // take\n"
    "    x = e == 1; // take\n"
    "    x = bl == 1; // take\n"
    "    x = fl == 0; // take\n"
    "    x = w == 0; // take\n"
    "    x = sf->f == 1; // refuse: bit-field\n"
    "    x = a == 4294967301L; // refuse: constant\n"
    "    x = a == 127; // take\n"
    "    x = a == 126; // take\n"
    "    x = (int*)0 == 0; // take\n"
    "    x = vp == 0; // take\n"
    "    x = (a + b) << 3; // take\n"
    "    x = 5 + (" BUSTER_NODE_CENSUS_TEST_TEN("a") " + " BUSTER_NODE_CENSUS_TEST_TEN("a") " + " BUSTER_NODE_CENSUS_TEST_TEN("a") " + a + a); // take\n"
    "    x = a << 32; // refuse: constant\n"
    "    x = a / 0; // refuse: constant\n"
    "    return x; // take\n"
    "}\n"
    "struct F r(struct F* sf)\n"
    "{\n"
    "    return *sf; // refuse: aggregate-value\n"
    "}\n"
    // The loop's rules of a pointer. A cast that a pointer operand sends has no test where its own type is void (a
    // statement's discard of a pointer's top, and a written one) or a pointer (to void too), and is left out; one to an
    // integer stays listed, a 64-bit one's test finding nothing and a narrower one's, a bool's and an enumeration's
    // firing; and nothing sends an enumeration's cast. A subscript of a plain pointer by a constant is left out (zero,
    // a small value, a large one, a negative one, the constant first, an address constant's), and an array's and a null
    // constant pointer's, on each side, and one by a bool's constant, which is no plain integer's, stay listed. A sum of
    // a plain pointer and a constant is left out (on each side, in each compound assignment, a large constant, and
    // where the pointer is the constant), and a string's, two pointers' difference and one whose constant is further
    // back than the block before stay listed.
    "enum V { V0, V1 };\n"
    "static int vs[4];\n"
    "int v(int* p, char* s, enum V e, long l, int i, int a)\n"
    "{\n"
    "    int x = 0;\n"
    "    int arr[4];\n"
    "    int* r = 0; // take\n"
    "    r = p; // take\n"
    "    (void)p; // take\n"
    "    s = (char*)p; // take\n"
    "    s = (void*)p; // take\n"
    "    l = (long)p; // take\n"
    "    x = (int)p; // refuse: pointer-cast-or-void-dereference\n"
    "    x = (_Bool)p; // refuse: pointer-cast-or-void-dereference\n"
    "    e = (enum V)p; // refuse: pointer-cast-or-void-dereference\n"
    "    x = (int)e; // take\n"
    "    (void)e; // take\n"
    "    e = V1; // take\n"
    "    x = p[0]; // take\n"
    "    x = p[200]; // take\n"
    "    x = p[-1]; // take\n"
    "    x = 2[p]; // take\n"
    "    x = p[100000]; // take\n"
    "    x = p[(_Bool)1]; // take\n"
    "    x = arr[1]; // take\n"
    "    x = (&vs[1])[1]; // take\n"
    "    x = ((int*)0)[1]; // refuse: constant\n"
    "    x = 1[(int*)0]; // refuse: constant\n"
    "    r = p - 200; // take\n"
    "    r = (int*)0 + i; // take\n"
    "    l = p - (int*)0; // take\n"
    "    r = 100000 + p; // take\n"
    "    p += 3; // take\n"
    "    p -= 100000; // take\n"
    "    s = \"abc\" + 1; // refuse: string-plus-int\n"
    "    r = 5 + (p + (" BUSTER_NODE_CENSUS_TEST_SEVENTY("a") ")); // take\n"
    "    return x + (r != p) + (s != 0) + l + i + arr[0]; // take\n"
    "}\n"
    // The tree's tests, which the loop lists by the operator above: a comparison under each bitwise operator, as its
    // first operand and as its second, and in parentheses there (listed, and its test has nothing); `!` before a
    // comparison and an ordering, in parentheses, and after one (not listed); `=` as a condition of each statement that
    // has one and of a `for`, in parentheses, as a `switch`'s (no test), and in a `for`'s other clauses; a comparison
    // discarded whose operand writes, an ordering too, under an explicit cast to void (listed, no test), and in a
    // `for`'s first and last clauses, where the discarded value's own test fires; `!` and `=` further back than the
    // block before the operator above them: a comparison's first operand, and the conditions whose root's record comes
    // after a body of 142 records; `!` 40 and 60 records before its comparison, in the block before it for some; and
    // the other operator's mark where an operator asks for one (`=` before a comparison, `!` as a condition: not
    // listed), and initializers whose top is listed, in each list, which have no root's record. And what an expression
    // with nothing listed has from the window alone: a warning event at its tokens (found in their first word, in
    // their last, and in a word between the two; and the unit's last, at its expression's first token), and no warning
    // of T's actions (a pointer compared with an integer, which lists it); and an address taken lists a return alone.
    "int t(int a, int b, int c, int d, int* p, const char* s)\n"
    "{\n"
    "    int x = 0;\n"
    "    x = a == b & c; // refuse: parentheses\n"
    "    x = a | b != c; // refuse: parentheses\n"
    "    x = a ^ b <= c; // refuse: parentheses\n"
    "    x = a & (b == c); // take\n"
    "    x = (a == b) & c; // take\n"
    "    x = !a == b; // refuse: parentheses\n"
    "    x = !a < b; // refuse: parentheses\n"
    "    x = (!a) == b; // take\n"
    "    x = a == !b; // take\n"
    "    x = !(a == b); // take\n"
    "    while (a = b) x = 1; // refuse: parentheses\n"
    "    do x = 1; while (a = b); // refuse: parentheses\n"
    "    for (; a = b;) x = 1; // refuse: parentheses\n"
    "    if ((a = b)) x = 1; // take\n"
    "    switch (a = b) { default: break; } // take\n"
    "    for (a = b; a < c; a = d) x = 1; // take\n"
    "    a < d++; // refuse: unused-value\n"
    "    (void)(a == d++); // take\n"
    "    for (a == b; a < c; a != b) x = 1; // refuse: unused-value\n"
    "    x = !a == (" BUSTER_NODE_CENSUS_TEST_SEVENTY("b") "); // refuse: parentheses\n"
    "    x = a == (" BUSTER_NODE_CENSUS_TEST_SEVENTY("b") "); // take\n"
    "    if (a = b) x = " BUSTER_NODE_CENSUS_TEST_SEVENTY("c") "; // refuse: parentheses\n"
    "    for (; a = b;) x = " BUSTER_NODE_CENSUS_TEST_SEVENTY("c") "; // refuse: parentheses\n"
    "    while (a = b) x = " BUSTER_NODE_CENSUS_TEST_SEVENTY("c") "; // refuse: parentheses\n"
    "    if (a == b) x = " BUSTER_NODE_CENSUS_TEST_SEVENTY("c") "; // take\n"
    "    s = \"a\\q\"; // refuse: warning-event\n"
    "    x = p == a; // refuse: action-warning\n"
    "    x = (a = b) == c; // take\n"
    "    if (!a) x = 1; // take\n"
    "    int y = a & b == c; // refuse: parentheses\n"
    "    int z = a / 0; // refuse: constant\n"
    "    x = !a == (" BUSTER_NODE_CENSUS_TEST_TEN("b") " + " BUSTER_NODE_CENSUS_TEST_TEN("c") "); // refuse: parentheses\n"
    "    x = !b == (" BUSTER_NODE_CENSUS_TEST_TEN("c") " + " BUSTER_NODE_CENSUS_TEST_TEN("d") " + " BUSTER_NODE_CENSUS_TEST_TEN("a") "); // refuse: parentheses\n"
    "    x = !c == (" BUSTER_NODE_CENSUS_TEST_TEN("d") " + " BUSTER_NODE_CENSUS_TEST_TEN("a") "); // refuse: parentheses\n"
    "    x = *\"a\\q\"; // refuse: warning-event\n"
    "    x = *\"a\\q\" + (" BUSTER_NODE_CENSUS_TEST_SEVENTY("a") "); // refuse: warning-event\n"
    "    x = (" BUSTER_NODE_CENSUS_TEST_SEVENTY("a") ") + *\"a\\q\"; // refuse: warning-event\n"
    "    x = (" BUSTER_NODE_CENSUS_TEST_SEVENTY("a") ") + *\"a\\q\" + (" BUSTER_NODE_CENSUS_TEST_SEVENTY("b") "); // refuse: warning-event\n"
    "    p = &x; // take\n"
    "    \"a\\q\"[a] = 1; // refuse: warning-event\n"
    "    return x + y + z; // take\n"
    "}\n"
    // For the calls with no test to make and a statement that discards nothing. A call the loop leaves out: no
    // constant, a zero and 1 to 126, a null pointer's zero, a static object's address; as a statement, under an
    // assignment, under a cast to another type, as a comma's first operand, as a condition and in a return; through a
    // pointer's name, a dereference and a member. A call it keeps, whose test has nothing: a
    // constant past 126 or under zero, one that fits a narrow parameter, a floating constant to a variable argument, an
    // integer one to a floating parameter, a callee with an enumeration's parameter or one that asks for its result, a
    // constant a cast or a conditional makes, whose record says no value, a callee before the block before. A call
    // whose test has something: a constant its parameter's type changes, the first of two parameters' too, an
    // aggregate result or argument, a printf-family name, near and before the block before, a pointer whose
    // declaration has a format. A call of six arguments, left out, and kept for its fifth and for its sixth. A loud
    // constant whose
    // consumer is no call, and a call's that is no argument of the call above. A statement's top: a cast to void, a cast
    // to another type, a call whose callee asks for its result, at its declaration's first such statement and at a
    // later one, and as a `for`'s last clause, whose discard comes after its body: in the same block, in the block
    // before, and further back, where the call stays listed and a write does not. Seventy statements that are a call
    // left out, and seventy whose callee asks for its result: a cast over one at each place of a block.
    "struct G { int a; int b; };\n"
    "struct H { int (*fn)(int); };\n"
    "int k0(int, int);\n"
    "void k1(int*, unsigned char, long);\n"
    "double k2(double);\n"
    "int k3(_Bool);\n"
    "int k4(enum A);\n"
    "int k5(int, ...);\n"
    "struct G k6(int);\n"
    "int k7(struct G);\n"
    "__attribute__((warn_unused_result)) int k8(int);\n"
    "__attribute__((pure)) int k9(int);\n"
    "int printf(const char*, ...);\n"
    "int (*kp)(int, int);\n"
    "int k10(_Bool, int);\n"
    "static int gx;\n"
    "int k11(int, int, int, int, int, int);\n"
    "int (*kf)(const char*, ...) __attribute__((format(printf, 1, 2)));\n"
    "int calls(int a, int b, int* p, struct G sg, struct H* h, double d)\n"
    "{\n"
    "    int x = 0;\n"
    "    k0(a, b); // take\n"
    "    k0(a, 0); // take\n"
    "    k0(a, 126); // take\n"
    "    k0(a, 127); // take\n"
    "    k0(a, -1); // take\n"
    "    k1(p, 200, 5); // take\n"
    "    k1(p, 300, 5); // refuse: constant\n"
    "    k1(0, 1, 2); // take\n"
    "    k1(&gx, 1, 2); // take\n"
    "    x = k0(a, b); // take\n"
    "    x = (int)k0(a, b); // take\n"
    "    k0(a, b), k1(p, 1, 2); // take\n"
    "    if (k0(a, b)) x = 1; // take\n"
    "    x = k0(k0(a, 1000), b); // take\n"
    "    x = k0(a + 1000, b); // take\n"
    "    d = k2(1); // take\n"
    "    d = k2(d); // take\n"
    "    x = k3(2); // refuse: constant\n"
    "    x = k3(1); // take\n"
    "    x = k10(2, 1); // refuse: constant\n"
    "    x = k0(a, (int)1000); // take\n"
    "    x = k0(a, 1 ? 1000 : 3); // take\n"
    "    x = k11(a, b, a, b, a, 1); // take\n"
    "    x = k11(a, b, a, b, a, 1000); // take\n"
    "    x = k11(a, b, a, b, 1000, a); // take\n"
    "    kf(\"%d\", a); // refuse: callee\n"
    "    x = ((int (*)(int, int))0)(a, b); // take\n"
    "    x = k4(1); // take\n"
    "    x = k5(a, 1.5); // take\n"
    "    x = k5(a, 3); // take\n"
    "    sg = k6(a); // refuse: aggregate-value\n"
    "    x = k7(sg); // refuse: aggregate-value\n"
    "    k8(a); // refuse: unused-value\n"
    "    x = k8(a); // take\n"
    "    k9(a); // refuse: unused-value\n"
    "    k8(b); // refuse: unused-value\n"
    "    for (x = 0; x < a; k0(x, b)) x += 2; // take\n"
    "    for (x = 0; x < a; k8(x)) x += 2; // refuse: unused-value\n"
    "    for (x = 0; x < a; k0(x, b)) x += " BUSTER_NODE_CENSUS_TEST_TEN("a") " + " BUSTER_NODE_CENSUS_TEST_TEN("b") "; // take\n"
    "    for (x = 0; x < a; k8(x)) x += " BUSTER_NODE_CENSUS_TEST_TEN("a") " + " BUSTER_NODE_CENSUS_TEST_TEN("b") "; // refuse: unused-value\n"
    "    for (x = 0; x < a; k0(x, b)) x += " BUSTER_NODE_CENSUS_TEST_TEN("a") " + " BUSTER_NODE_CENSUS_TEST_TEN("b") " + " BUSTER_NODE_CENSUS_TEST_TEN("a") "; // take\n"
    "    for (x = 0; x < a; k8(x)) x += " BUSTER_NODE_CENSUS_TEST_TEN("a") " + " BUSTER_NODE_CENSUS_TEST_TEN("b") " + " BUSTER_NODE_CENSUS_TEST_TEN("a") "; // refuse: unused-value\n"
    "    for (x = 0; x < a; k0(x, b)) x += " BUSTER_NODE_CENSUS_TEST_SEVENTY("a") "; // take\n"
    "    for (x = 0; x < a; k8(x)) x += " BUSTER_NODE_CENSUS_TEST_SEVENTY("a") "; // refuse: unused-value\n"
    "    for (x = 0; x < a; x++) x += " BUSTER_NODE_CENSUS_TEST_SEVENTY("a") "; // take\n"
    "    " BUSTER_NODE_CENSUS_TEST_SEVENTY_OF("k0(a, b);") " // take\n"
    "    " BUSTER_NODE_CENSUS_TEST_SEVENTY_OF("k8(a);") " // refuse: unused-value\n"
    "    printf(\"%d\", a); // refuse: callee\n"
    "    kp(a, b); // take\n"
    "    (*kp)(a, b); // take\n"
    "    h->fn(a); // take\n"
    "    x = (*k0)(a, b); // take\n"
    "    x = (**k0)(a, b); // take\n"
    "    (void)a; // take\n"
    "    (void)0; // take\n"
    "    (int)a; // refuse: unused-value\n"
    "    a; // refuse: unused-value\n"
    "    x = k0(" BUSTER_NODE_CENSUS_TEST_SEVENTY("a") ", b); // take\n"
    "    printf(\"%d\", " BUSTER_NODE_CENSUS_TEST_SEVENTY("a") "); // refuse: callee\n"
    "    return k0(a, b); // take\n"
    "}\n";

// With no warning of a mix of enumerations and -Wassign-enum: an operator with an operand of an enumeration's type is
// then listed for its constant alone, and an enumeration is no plain integer to the loop's quiet rule: its constant's
// test runs.
static const char node_census_test_enums[] =
    "enum E { E0, E1 };\n"
    "int s(enum E e, int a)\n"
    "{\n"
    "    int x = 0;\n"
    "    e = 5; // refuse: constant\n"
    "    x = e == 1; // take\n"
    "    x = a + 1; // take\n"
    "    return x; // take\n"
    "}\n";

// The warnings that list an expression for the tests that read T's records, off: what they would refuse is taken, and
// an expression that only they list has nothing listed (two writes, a returned address; a pointer's cast to an integer,
// which nothing sends to its test then). Its first function's return has nothing listed and ends at the unit's first
// warning event: the events' lower bound, at its equality.
static const char node_census_test_off[] =
    "char e(void)\n"
    "{\n"
    "    return *\"a\\q\"; // refuse: warning-event\n"
    "}\n"
    "int f(int a, int b, int c, int i, int* p)\n"
    "{\n"
    "    int x = 0;\n"
    "    x = (int)p; // take\n"
    "    x = i + i++; // take\n"
    "    x = a & b == c; // take\n"
    "    x = !a == b; // take\n"
    "    if (a = b) x = 1; // take\n"
    "    a == i++; // take\n"
    "    a == b; // take\n"
    "    return x; // take\n"
    "}\n"
    "int* g(void)\n"
    "{\n"
    "    int local = 0;\n"
    "    return &local; // take\n"
    "}\n";

// A warning no test stands for (-Wc++-compat): every expression is refused for it, one with nothing listed too.
static const char node_census_test_whole[] =
    "int w(int a, int b)\n"
    "{\n"
    "    a = a + b; // refuse: unmodeled-warning\n"
    "    return a; // refuse: unmodeled-warning\n"
    "}\n";

// A unit with -Wtype-safety's type tags: its calls' test has something for every call, and the loop leaves none out.
static const char node_census_test_tagged[] =
    "static const int tag_long __attribute__((type_tag_for_datatype(magic, long))) = 42;\n"
    "void take(void* buffer, int tag) __attribute__((pointer_with_type_tag(magic, 1, 2)));\n"
    "int plain(int);\n"
    "int w(long* p, int a)\n"
    "{\n"
    "    take(p, 42); // refuse: callee\n"
    "    plain(a); // refuse: callee\n"
    "    return plain(a); // refuse: callee\n"
    "}\n";

// A unit with clang's no_builtin on two functions (#2968): its calls are made as any unit's. A plain library builtin is the
// declaration's call in the function whose attribute names it, or names none (#1958), and has its own semantics in the
// others, before, between and after them: each is asked by the function that holds it.
static const char node_census_test_builtins[] =
    "typedef unsigned long size_t;\n"
    "void* memset(void*, int, size_t);\n"
    "void* memcpy(void*, const void*, size_t);\n"
    "size_t strlen(const char*);\n"
    "int k0(int, int);\n"
    "int before(char* p, int a)\n"
    "{\n"
    "    memset(p, 0, 8); // refuse: callee\n"
    "    k0(a, a); // take\n"
    "    return (int)strlen(\"abc\"); // refuse: callee\n"
    "}\n"
    "__attribute__((no_builtin(\"memset\"))) int named(char* p, const char* q, int a)\n"
    "{\n"
    "    memset(p, 0, 8); // take\n"
    "    memcpy(p, q, 8); // refuse: callee\n"
    "    k0(a, a); // take\n"
    "    return (int)strlen(\"abc\"); // refuse: callee\n"
    "}\n"
    "int between(char* p, int a)\n"
    "{\n"
    "    memset(p, 0, 8); // refuse: callee\n"
    "    k0(a, a); // take\n"
    "    return a; // take\n"
    "}\n"
    "__attribute__((no_builtin)) int every(char* p, const char* q, int a)\n"
    "{\n"
    "    memset(p, 0, 8); // take\n"
    "    memcpy(p, q, 8); // take\n"
    "    (&memset)(p, 0, 8); // take\n"
    "    k0(a, a); // take\n"
    "    return (int)strlen(\"abc\"); // take\n"
    "}\n"
    "int after(char* p, int a)\n"
    "{\n"
    "    memset(p, 0, 8); // refuse: callee\n"
    "    k0(a, a); // take\n"
    "    return (int)strlen(p); // refuse: callee\n"
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
// subtrees' least records, a call's arguments included, and whose calls' rows in the tasks' preorder. Conditions
// that are branches (#2890, check_lower_branch's): an `&&` and an `||` under an if, a while and a do statement, whose
// control branches where its condition holds; each where true and where false, by a `!` and two of them over it and by
// its place under the other operator, three deep; with a call, an update, an assignment of an `&&`'s value and a
// conditional as operands, a char, a double, a float and a pointer tested; a conditional's condition and an `&&`'s and
// an `||`'s first operand (#2891), to the operator's own label; and what stays a value: a `!` alone, and an `&&` whose
// first operand's truth is known. Three in a row for the labels' places: a branched `&&` with a label of its own over
// an `||` with one, then the same records with the `&&`'s label gone, then with the inner operator's gone (a place
// written for an operator that has no label would land on the other's, whose number the record's column still holds
// from the line before, X's after the other's and the one sweep's after it too). Two `||` with a label each under an
// `&&`, whose places the one sweep numbers in the order it meets them, the right one first. An `&&` and a `!` as an
// initializer's value, the root at reader time, which is no statement's condition. And, for reader time alone (the
// census leaves them, so no line names the machine's answer; node_shadow_test_fixture counts them), operands a truth's
// entries don't test, a cast to __float128: under a statement's condition as either operand, under a conditional's,
// and as a value's first operand's. The argument
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
    "    x = !(a || b) && c; // machine: taken\n"
    "    x = !(a && d); // machine: taken\n"
    "    x = a ? b : 1; // machine: taken\n"
    "    d = a ? d : e; // machine: taken\n"
    "    y = q ? *q : (a < b ? a : b); // machine: taken\n"
    "    (void)(a ? (void)0 : (void)b); // machine: taken\n"
    "    if (a && (b || c)) // machine: taken\n"
    "        x = 2; // machine: taken\n"
    "    x = (a && b) ? c : u; // machine: taken\n"
    "    if (a && b) // machine: taken\n"
    "        x = 3; // machine: taken\n"
    "    if (a || b) // machine: taken\n"
    "        x = 4; // machine: taken\n"
    "    if (!(a && b)) // machine: taken\n"
    "        x = 5; // machine: taken\n"
    "    if (!(a || b)) // machine: taken\n"
    "        x = 6; // machine: taken\n"
    "    if (!!(a && b)) // machine: taken\n"
    "        x = 7; // machine: taken\n"
    "    if ((a && b) || (c && (u || d))) // machine: taken\n"
    "        x = 8; // machine: taken\n"
    "    if (!(a && (q || !(e && p->n)))) // machine: taken\n"
    "        x = 9; // machine: taken\n"
    "    if (f(a) && (x++ || f(b))) // machine: taken\n"
    "        x = 10; // machine: taken\n"
    "    if ((x = a && b) || (a ? b : c)) // machine: taken\n"
    "        x = 11; // machine: taken\n"
    "    while (a < b && u > 3u) // machine: taken\n"
    "        a++; // machine: taken\n"
    "    do\n"
    "        x++; // machine: taken\n"
    "    while (a && b); // machine: taken\n"
    "    do\n"
    "        x--; // machine: taken\n"
    "    while (a || b); // machine: taken\n"
    "    do\n"
    "        x++; // machine: taken\n"
    "    while (!(a || b) && c); // machine: taken\n"
    "    for (; a || b; a = 0)\n"
    "        b = 0;\n"
    "    x = (a || b) ? c : u; // machine: taken\n"
    "    x = !(a && b) ? c : u; // machine: taken\n"
    "    x = ((a && b) || c) && u; // machine: taken\n"
    "    x = (a || b) && (c || u); // machine: taken\n"
    "    x = f((a && b) || c) + (!(a || b) || d); // machine: taken\n"
    "    if (!a) // machine: taken\n"
    "        x = 12; // machine: taken\n"
    "    if (1 && a) // machine: taken\n"
    "        x = 13; // machine: taken\n"
    "    if (a || 1) // machine: taken\n"
    "        x = 14; // machine: taken\n"
    "    if (!((a || b) && c)) // machine: taken\n"
    "        x = 15; // machine: taken\n"
    "    if ((a || b) && c) // machine: taken\n"
    "        x = 16; // machine: taken\n"
    "    if (!((a && b) && c)) // machine: taken\n"
    "        x = 17; // machine: taken\n"
    "    if ((a || b) && (c || d)) // machine: taken\n"
    "        x = 18; // machine: taken\n"
    "    int lw = (a && b) || c; // machine: taken\n"
    "    int ln = !(a && b); // machine: taken\n"
    "    x = lw + ln; // machine: taken\n"
    "    if ((__float128)a && b)\n"
    "        x = 19;\n"
    "    if (a && (__float128)b)\n"
    "        x = 20;\n"
    "    x = ((__float128)a || b) ? c : u;\n"
    "    x = ((__float128)a && b) || c;\n"
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
    "    while (y > 0 && n > 0) // machine: taken\n"
    "    {\n"
    "        y -= 1; // machine: taken\n"
    "    }\n"
    "    return w;\n"
    "}\n"
    "struct W either(long x)\n"
    "{\n"
    "    struct W first;\n"
    "    struct W second;\n"
    "    first.a = x; // machine: taken\n"
    "    second.a = x + 1; // machine: taken\n"
    "    unsigned short narrow = 3;\n"
    "    *&narrow = 4; // machine: taken\n"
    "    x += *&narrow; // machine: taken\n"
    "    if (x > 3) // machine: taken\n"
    "    {\n"
    "        return first;\n"
    "    }\n"
    "    do\n"
    "    {\n"
    "        x -= 1; // machine: taken\n"
    "    } while (x > 9 || x < -9); // machine: taken\n"
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

// The fact words' kernel at reader time (`reader` has BUSTER_NODE_READER_FACTS) as a shadow's run counted it: whether
// it ran on every expression T took there, nothing differed (its records from T's over poison and over decoys, its
// lists from their twin's, the front's inputs from those made of T's columns, as each expression came and made
// again), every expression's inputs came from the front's records or were counted as not given, and the second
// making made as many records' as the first; as many expressions not given as `given`, as many records left to T as
// `left`, as many literals' leaves from a line of their own as `lines`, and as many records of the whole machine's
// expressions as `whole` (none unless the census's tests and C and X ran there too), `whole_made` of them made. In a
// build without the vector paths, or where it isn't asked for, it must have counted nothing.
//
// Where the front writes the window too (BUSTER_NODE_READER_WINDOW): no record's inputs as the front wrote them
// differed from either other making's; every expression's records were there in a row or counted as not, `cut` of
// them; the kernel on the front's columns in place made every record T's, the same ones it makes of the batch where no
// expression is cut and fewer where some are; the names' pass's scalar path gave every leaf the same kind and line;
// the same front without the window made the same records of every chunk; and the calls' arguments' list never held
// more than it has room for. Where it doesn't, none of that was counted.
static bool node_shadow_test_kernel(const NodeShadow* shadow, uint8_t reader, bool fronts, uint64_t given, uint64_t left, uint64_t lines, uint64_t whole,
                                    uint64_t whole_made, uint64_t cut)
{
    const uint64_t* counts = shadow->reader_facts;
    const bool windowed = (reader & BUSTER_NODE_READER_WINDOW) != 0 && (reader & BUSTER_NODE_READER_FACTS) != 0 && fronts;
    bool held = true;
    if ((reader & BUSTER_NODE_READER_FACTS) == 0 || !fronts)
    {
        for (uint32_t c = 0; c < NODE_FACTS_COUNTS; c += 1)
        {
            held = held && counts[c] == 0;
        }
    }
    else
    {
        const uint64_t skipped = counts[NODE_FACTS_COUNT_FRONT_SKIPPED];
        held = counts[NODE_FACTS_COUNT_DIFFERENCES] == 0 && counts[NODE_FACTS_COUNT_DECOY_DIFFERENCES] == 0 && counts[NODE_FACTS_COUNT_LIST_DIFFERENCES] == 0 &&
               counts[NODE_FACTS_COUNT_FAILURES] == 0 && counts[NODE_FACTS_COUNT_FRONT_DIFFERENCES] == 0 &&
               counts[NODE_FACTS_COUNT_EXPRESSIONS] == shadow->typed_refused[NODE_MACHINE_TAKEN] && counts[NODE_FACTS_COUNT_EXPRESSIONS] != 0 &&
               counts[NODE_FACTS_COUNT_MADE] != 0 && counts[NODE_FACTS_COUNT_MADE] + left == counts[NODE_FACTS_COUNT_WANTED] &&
               counts[NODE_FACTS_COUNT_LINES_MADE] == counts[NODE_FACTS_COUNT_LINES] && counts[NODE_FACTS_COUNT_LINES] == lines &&
               counts[NODE_FACTS_COUNT_FRONT_EXPRESSIONS] + skipped == counts[NODE_FACTS_COUNT_EXPRESSIONS] && counts[NODE_FACTS_COUNT_FRONT_EXPRESSIONS] != 0 &&
               counts[NODE_FACTS_COUNT_FRONT_AGAIN_RECORDS] == counts[NODE_FACTS_COUNT_FRONT_RECORDS] && skipped == given &&
               counts[NODE_FACTS_COUNT_TAKEN_RECORDS] == whole && counts[NODE_FACTS_COUNT_TAKEN_MADE] == whole_made;
    }
    if (windowed)
    {
        held = held && counts[NODE_FACTS_COUNT_WINDOW_DIFFERENCES] == 0 && counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES] == 0 &&
               counts[NODE_FACTS_COUNT_WINDOW_EXPRESSIONS] != 0 &&
               counts[NODE_FACTS_COUNT_WINDOW_EXPRESSIONS] + counts[NODE_FACTS_COUNT_WINDOW_SKIPPED] == counts[NODE_FACTS_COUNT_EXPRESSIONS] &&
               counts[NODE_FACTS_COUNT_WINDOW_SKIPPED] == cut && counts[NODE_FACTS_COUNT_WINDOW_MADE] != 0 &&
               (cut == 0 ? counts[NODE_FACTS_COUNT_WINDOW_WANTED] == counts[NODE_FACTS_COUNT_WANTED] && counts[NODE_FACTS_COUNT_WINDOW_MADE] == counts[NODE_FACTS_COUNT_MADE] :
                           counts[NODE_FACTS_COUNT_WINDOW_WANTED] < counts[NODE_FACTS_COUNT_WANTED]) &&
               shadow->front_plain_differences == 0 && shadow->front_plain_records == shadow->front_records && shadow->front_window_name_differences == 0 &&
               shadow->front_window_names != 0 && shadow->front_window_arguments_most <= BUSTER_NODE_FRONT_RECORDS;
    }
    else
    {
        held = held && counts[NODE_FACTS_COUNT_WINDOW_DIFFERENCES] == 0 && counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES] == 0 &&
               counts[NODE_FACTS_COUNT_WINDOW_EXPRESSIONS] == 0 && counts[NODE_FACTS_COUNT_WINDOW_SKIPPED] == 0 && counts[NODE_FACTS_COUNT_WINDOW_RECORDS] == 0 &&
               counts[NODE_FACTS_COUNT_WINDOW_WANTED] == 0 && shadow->front_plain_records == 0 && shadow->front_plain_differences == 0 &&
               shadow->front_window_kept_values == 0 && shadow->front_window_cut_values == 0 && shadow->front_window_names == 0 &&
               shadow->front_window_arguments_most == 0;
    }
    if (!held)
    {
        fprintf(stderr, "test-checker: node-facts at reader time: %" PRIu64 " expressions (T took %" PRIu64 "), %" PRIu64 " records, %" PRIu64 " to make, %" PRIu64
                " made (expected %" PRIu64 " left); %" PRIu64 " literals, %" PRIu64 " from a line (expected %" PRIu64 "), %" PRIu64 " made; %" PRIu64 " differ (%" PRIu64
                " over decoys), %" PRIu64 " lists differ, %" PRIu64 " failures; the front's inputs: %" PRIu64 " expressions, %" PRIu64
                " not given (expected %" PRIu64 "), %" PRIu64 " records, %" PRIu64 " made again, %" PRIu64 " differ; the whole machine's records %" PRIu64 ", made %" PRIu64
                " (expected %" PRIu64 " and %" PRIu64 ")\n",
                counts[NODE_FACTS_COUNT_EXPRESSIONS], shadow->typed_refused[NODE_MACHINE_TAKEN], counts[NODE_FACTS_COUNT_RECORDS], counts[NODE_FACTS_COUNT_WANTED],
                counts[NODE_FACTS_COUNT_MADE], left, counts[NODE_FACTS_COUNT_LITERALS], counts[NODE_FACTS_COUNT_LINES], lines, counts[NODE_FACTS_COUNT_LINES_MADE],
                counts[NODE_FACTS_COUNT_DIFFERENCES], counts[NODE_FACTS_COUNT_DECOY_DIFFERENCES], counts[NODE_FACTS_COUNT_LIST_DIFFERENCES],
                counts[NODE_FACTS_COUNT_FAILURES], counts[NODE_FACTS_COUNT_FRONT_EXPRESSIONS], counts[NODE_FACTS_COUNT_FRONT_SKIPPED], given,
                counts[NODE_FACTS_COUNT_FRONT_RECORDS], counts[NODE_FACTS_COUNT_FRONT_AGAIN_RECORDS], counts[NODE_FACTS_COUNT_FRONT_DIFFERENCES],
                counts[NODE_FACTS_COUNT_TAKEN_RECORDS], counts[NODE_FACTS_COUNT_TAKEN_MADE], whole, whole_made);
        fprintf(stderr, "test-checker: node-facts at reader time: the windows the front wrote%s: %" PRIu64 " expressions, %" PRIu64 " not there (expected %" PRIu64 "), %"
                PRIu64 " records, %" PRIu64 " inputs differ; the kernel in place: %" PRIu64 " to make, %" PRIu64 " made, %" PRIu64 " differ; the front without the window made %"
                PRIu64 " records of %" PRIu64 ", %" PRIu64 " chunks differ; the names' pass: %" PRIu64 " names, %" PRIu64 " differ by its scalar path; at the chunks' ends %"
                PRIu64 " values kept whole (%" PRIu64 " records), %" PRIu64 " cut, the calls' arguments' list at most %" PRIu64 " (room %u)\n",
                windowed ? "" : " (expected none)", counts[NODE_FACTS_COUNT_WINDOW_EXPRESSIONS], counts[NODE_FACTS_COUNT_WINDOW_SKIPPED], cut,
                counts[NODE_FACTS_COUNT_WINDOW_RECORDS], counts[NODE_FACTS_COUNT_WINDOW_DIFFERENCES], counts[NODE_FACTS_COUNT_WINDOW_WANTED],
                counts[NODE_FACTS_COUNT_WINDOW_MADE], counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES], shadow->front_plain_records, shadow->front_records,
                shadow->front_plain_differences, shadow->front_window_names, shadow->front_window_name_differences, shadow->front_window_kept_values,
                shadow->front_window_kept_records, shadow->front_window_cut_values, shadow->front_window_arguments_most, BUSTER_NODE_FRONT_RECORDS);
    }
    return held;
}

// The shadow's fixture written to `directory` and read with `options`, its T by the rows (and verified against the
// action) or (`scalar`) by the action alone, with the machine's front as `front` asks (BUSTER_NODE_FRONT_*) and what
// else runs at reader time as `reader` does (BUSTER_NODE_READER_*): whether every line held and nothing differed from
// the pipeline.
static bool node_shadow_test_fixture(const char* directory, bool scalar, uint8_t front, uint8_t reader, char** options, uint32_t option_count)
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
        .reader = reader,
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
    const bool fronts = true;
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
    // Reader time sweeps what the census leaves too, so its refusals are counted: the fixture's lines C refuses by a
    // template (nine: the five `// machine: template` lines' and the four operands a truth's entries don't test, which C
    // refuses at their operator, where X would refuse their conversion's row), and none by a row.
    const bool swept = (front & BUSTER_NODE_FRONT_SWEEPS) == 0 || !fronts ? shadow.swept_expressions == 0 && shadow.swept_compared == 0 :
                                                                            shadow.swept_differences == 0 && shadow.swept_compared != 0 && shadow.swept_unkept == 0 &&
                                                                                shadow.swept_expressions == shadow.typed_refused[NODE_MACHINE_TAKEN] &&
                                                                                shadow.swept_references >= 20 && shadow.swept_call_targets >= 24 &&
                                                                                shadow.swept_refused[NODE_MACHINE_REFUSED_TEMPLATE] == 9 &&
                                                                                shadow.swept_refused[NODE_MACHINE_REFUSED_ROW] == 0;
    // The flat front, where the front asks for it: every record the lean front made is the flat front's, field for
    // field, neither made a record the other didn't, and the tokens the fixture's fronts leave within an operator's
    // values kept their places, empty.
    const bool flat = (front & BUSTER_NODE_FRONT_FLAT) == 0 || !fronts ? shadow.flat_compared == 0 && shadow.flat_records == 0 :
                                                                         shadow.flat_differences == 0 && shadow.flat_compared == shadow.front_records &&
                                                                             shadow.flat_records == shadow.front_records && shadow.flat_only == 0 &&
                                                                             shadow.flat_lean_only == 0 && shadow.flat_unread == 0 && shadow.flat_empties != 0 &&
                                                                             shadow.flat_beyond == 0;
    const bool same = shadow.value_differences == 0 && shadow.entry_differences == 0 && shadow.twin_differences == 0 && shadow.taken_expressions != 0 &&
                      (scalar || shadow.row_nodes != 0) && shadow.fused_differences == 0 && shadow.fused_expressions >= shadow.taken_expressions &&
                      shadow.front_differences == 0 && shadow.front_checked == shadow.front_records && shadow.front_resets == 0 && shadow.front_waiting == 0 &&
                      (fronts ? shadow.front_units == 1 && shadow.front_unread == 0 && shadow.front_records != 0 : shadow.front_units == 0 && shadow.front_records == 0) && typed &&
                      swept && flat &&
                      // The fact words' kernel at reader time, where asked: the front's records give every expression of
                      // the fixture but the three its chunks' two ends cut, and the kernel leaves 24 of its records to T
                      // (an enumerator's leaf is none of them: its value is the reader's own column's there). Each of its
                      // 125 literals' leaves is a line's. With the tests and C and X there too, the whole machine's
                      // expressions have 1,223 of the 1,313 records, and the kernel makes 1,088 of them. Its last body
                      // dereferences an address twice (`*&narrow`), whose pointer type T derives there: the type's row
                      // is made before the expression is appended, or the dereference's record is not T's. Where the
                      // front writes the window it keeps those three's records whole at the chunks' ends: its window
                      // gives every expression, and so does the copy of its records.
                      node_shadow_test_kernel(&shadow, reader, fronts, (reader & BUSTER_NODE_READER_WINDOW) != 0 ? 0 : 3, 24, 125,
                                              (reader & BUSTER_NODE_READER_TESTS) != 0 ? 1223 : 0, (reader & BUSTER_NODE_READER_TESTS) != 0 ? 1088 : 0, 0);
    if (valid && !swept)
    {
        fprintf(stderr, "test-checker: node-shadow%s: C and X at reader time ran on %" PRIu64 " expressions (T took %" PRIu64 "), %" PRIu64 " compared, %" PRIu64
                " differ (root %" PRIu64 ", refusal %" PRIu64 ", tape %" PRIu64 ", labels and calls %" PRIu64 "), %" PRIu64 " tapes not kept, %" PRIu64
                " globals' entries and %" PRIu64 " direct calls patched; refused there by a template %" PRIu64 " (expected 9), by a row %" PRIu64 " (expected 0)\n",
                scalar ? " --scalar" : "", shadow.swept_expressions, shadow.typed_refused[NODE_MACHINE_TAKEN], shadow.swept_compared, shadow.swept_differences,
                shadow.swept_root_differences, shadow.swept_refusal_differences, shadow.swept_tape_differences, shadow.swept_side_differences, shadow.swept_unkept,
                shadow.swept_references, shadow.swept_call_targets, shadow.swept_refused[NODE_MACHINE_REFUSED_TEMPLATE], shadow.swept_refused[NODE_MACHINE_REFUSED_ROW]);
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
        fprintf(stderr, "test-checker: node-shadow: the flat front's records %" PRIu64 ", compared %" PRIu64 ", %" PRIu64 " differ, %" PRIu64 " only its own, %" PRIu64
                " only the lean front's, %" PRIu64 " empty places, %" PRIu64 " tokens past the rings' reach\n",
                shadow.flat_records, shadow.flat_compared, shadow.flat_differences, shadow.flat_only, shadow.flat_lean_only, shadow.flat_empties, shadow.flat_beyond);
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

// What the tests on the window count on a fixture, which the verdicts don't show: the expressions they run on; the
// records their loop lists for the tree's tests and the operators it lists for their own; the records then sent to the
// census's code and the expressions walked for it; the operands further back than the block before their record's; the
// operators the loop leaves out of its list for a constant operand whose tests have nothing to say; the casts it leaves
// out for their own type (`casts_left`; `casts` are the asks' casts over a call left out); and the integer constants
// whose record says its value (c_check_node_machine_facts.c's node_facts_small), each then its payload's.
typedef struct NodeTestsWindowed NodeTestsWindowed;
struct NodeTestsWindowed
{
    uint32_t expressions;
    uint32_t trees;
    uint32_t operators;
    uint32_t sent;
    uint32_t walks;
    uint32_t far;
    uint32_t quiet;
    uint32_t calls;
    uint32_t calls_checked;
    uint32_t casts;
    uint32_t casts_left;
    uint32_t asks;
    uint32_t values;
    uint32_t spared;
    uint32_t direct;
    uint32_t apart;
};

// The census's tests at reader time (docs/node-machine.md, "The tests where they sit") on fixture `text`, written to
// `directory` as `name` and read with `options`, the front `front` before T: the verdict there against the census's after
// the pipeline. No expression reader time takes is one the census refuses; no test differs but as it may; every root
// the tests ran on is compared; T's rows and rules are the action's there, without a warning; and a line that says
// `// refuse: TEST` of a silence test has an expression that test refuses there, where the tests ran on the line at all
// (the set's lines, and a region's, can be T's own refusals before any test). `refused` expressions are refused there
// by a test, no more (the tests see one expression's warnings, not the unit's so far), and the fixture's `discards` declarations' discards are left alone (an initializer's value cast to
// void at the declaration's `;`, each value of a braced one; an enumerator's value makes no record of the front's, and
// so none).
// With `window` the lean front writes the window, the kernel makes its records in place, and the tests in the machine's
// own form run on it too ("The tests on the window"), on `windowed`'s expressions: each one's bits, its root's kind and
// its span of tokens are the census's code's, a walk for a record sent there reaches as many nodes as T's records, and
// what their loop lists and sends is `windowed`'s counts, which no verdict shows. Whether all of it held.
static bool node_tests_test_fixture_form(const char* directory, const char* name, const char* text, char** options, uint32_t option_count, uint8_t front, uint32_t refused,
                                         uint32_t discards, bool window, const NodeTestsWindowed* windowed)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    bool valid = parse_test_write(path, text, strlen(text));
    NodeShadow shadow = {
        .verify = true,
        .front = (uint8_t)(front | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | (window ? BUSTER_NODE_FRONT_LEAN : 0)),
        .reader = (uint8_t)(BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_VERIFY |
                            (window ? BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS : 0)),
        .passes = 1,
        .repeat = 1,
        .tested_out = calloc(512, sizeof(NodeShadowTested)),
        .tested_out_capacity = 512,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && shadow.tested_out != NULL && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0 &&
            shadow.tested_out_count < shadow.tested_out_capacity;
    const bool fronts = true;
    const bool same = !fronts ? shadow.tested_expressions == 0 && shadow.tested_compared == 0 :
                                shadow.tested_unsound == 0 && shadow.tested_differences == 0 && shadow.tested_kind_differences == 0 && shadow.tested_missed == 0 &&
                                    shadow.tested_policy_differences == 0 && shadow.reader_twin_differences == 0 && shadow.reader_twin_warnings == 0 &&
                                    shadow.tested_expressions != 0 && shadow.tested_compared + shadow.tested_compared_unpopped == shadow.tested_expressions &&
                                    shadow.tested_refused == refused && shadow.typed_discards == discards &&
                                    (!window || (shadow.reader_facts[NODE_FACTS_COUNT_TESTS_DIFFERENCES] == 0 && shadow.own_count_differences == 0 &&
                                                 shadow.own_expressions == windowed->expressions &&
                                                 shadow.own_records == shadow.reader_facts[NODE_FACTS_COUNT_TESTS_RECORDS] &&
                                                 shadow.reader_facts[NODE_FACTS_COUNT_TESTS_APART] == windowed->apart &&
                                                 shadow.reader_facts[NODE_FACTS_COUNT_TESTS_EXPRESSIONS] == windowed->expressions &&
                                                 shadow.own_trees == windowed->trees && shadow.own_operators == windowed->operators &&
                                                 shadow.own_visits == windowed->sent && shadow.own_walks == windowed->walks &&
                                                 shadow.reader_facts[NODE_FACTS_COUNT_TESTS_FAR] == windowed->far &&
                                                 shadow.own_quiet == windowed->quiet && shadow.own_quiet_checked == windowed->quiet && shadow.own_quiet_wrong == 0 &&
                                                 shadow.own_quiet_calls == windowed->calls && shadow.own_quiet_calls_checked == windowed->calls_checked &&
                                                 shadow.own_quiet_calls_wrong == 0 && shadow.own_quiet_calls_loud == 0 && shadow.own_result_asks == windowed->asks &&
                                                 shadow.own_result_casts == windowed->casts &&
                                                 shadow.own_quiet_casts == windowed->casts_left && shadow.own_quiet_casts_checked == windowed->casts_left &&
                                                 shadow.own_quiet_casts_wrong == 0 &&
                                                 shadow.own_values_checked == windowed->values && shadow.own_values_wrong == 0 &&
                                                 shadow.own_tree_checked == windowed->spared && shadow.own_tree_wrong == 0 && shadow.own_lists_past == 0 &&
                                                 shadow.own_direct == windowed->direct && shadow.own_direct_checked == windowed->direct && shadow.own_direct_wrong == 0 &&
                                                 shadow.reader_facts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES] == 0 &&
                                                 shadow.reader_facts[NODE_FACTS_COUNT_WINDOW_DIFFERENCES] == 0));
    if (!valid || !same)
    {
        fprintf(stderr, "test-checker: node-tests %s%s: %s: the tests on the window ran on %" PRIu64 " expressions (%u expected), %" PRIu64
                " not the census's code's, %" PRIu64 " walks of another count; their loop listed %" PRIu64 " records for the tree's tests (%u) and %" PRIu64
                " operators (%u), %" PRIu64 " records went to the census's code (%u) after %" PRIu64 " walks (%u), %" PRIu64
                " operands were before the block before (%u), and it left %" PRIu64 " operators out for their constants (%u): %" PRIu64 " of them checked, %" PRIu64
                " with a test; it left %" PRIu64 " calls out (%u): %" PRIu64 " of them checked (%u), %" PRIu64 " with a test, %" PRIu64 " with a loud argument, and asked %" PRIu64
                " callees for their result (%u) at %" PRIu64 " casts over one (%u); it left %" PRIu64 " casts out (%u): %" PRIu64 " of them checked, %" PRIu64
                " with a test; %" PRIu64 " constants' records say their value (%u), %" PRIu64
                " of them not their payload's; %" PRIu64
                " records the tree's tests read were no listed operator's operand (%u), %" PRIu64 " of them with a test, and %" PRIu64
                " lists' words had a bit past their expression; %" PRIu64
                " expressions had nothing listed (%u), %" PRIu64 " of them checked, %" PRIu64 " with another verdict; the tests' own form counted %" PRIu64
                " records of %" PRIu64 ", and %" PRIu64 " roots were apart from their operand (%u)\n",
                name, window ? " (on the window)" : "", valid ? "read" : "NOT READ", shadow.reader_facts[NODE_FACTS_COUNT_TESTS_EXPRESSIONS],
                window ? windowed->expressions : 0, shadow.reader_facts[NODE_FACTS_COUNT_TESTS_DIFFERENCES], shadow.own_count_differences, shadow.own_trees,
                window ? windowed->trees : 0, shadow.own_operators, window ? windowed->operators : 0, shadow.own_visits, window ? windowed->sent : 0, shadow.own_walks,
                window ? windowed->walks : 0, shadow.reader_facts[NODE_FACTS_COUNT_TESTS_FAR], window ? windowed->far : 0, shadow.own_quiet, window ? windowed->quiet : 0,
                shadow.own_quiet_checked, shadow.own_quiet_wrong, shadow.own_quiet_calls, window ? windowed->calls : 0, shadow.own_quiet_calls_checked,
                window ? windowed->calls_checked : 0, shadow.own_quiet_calls_wrong, shadow.own_quiet_calls_loud, shadow.own_result_asks, window ? windowed->asks : 0,
                shadow.own_result_casts, window ? windowed->casts : 0, shadow.own_quiet_casts, window ? windowed->casts_left : 0, shadow.own_quiet_casts_checked,
                shadow.own_quiet_casts_wrong, shadow.own_values_checked,
                window ? windowed->values : 0, shadow.own_values_wrong,
                shadow.own_tree_checked, window ? windowed->spared : 0, shadow.own_tree_wrong, shadow.own_lists_past, shadow.own_direct, window ? windowed->direct : 0,
                shadow.own_direct_checked,
                shadow.own_direct_wrong, shadow.own_records, shadow.reader_facts[NODE_FACTS_COUNT_TESTS_RECORDS], shadow.reader_facts[NODE_FACTS_COUNT_TESTS_APART],
                window ? windowed->apart : 0);
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

// The tests at reader time on one fixture: the census's code on the view, with the whole front; then the same with the
// lean front and its window, the machine's own form beside it on the `windowed` expressions the kernel makes in place.
static bool node_tests_test_fixture(const char* directory, const char* name, const char* text, char** options, uint32_t option_count, uint8_t front, uint32_t refused,
                                    uint32_t discards, NodeTestsWindowed windowed)
{
    const bool census = node_tests_test_fixture_form(directory, name, text, options, option_count, front, refused, discards, false, &windowed);
    const bool window = node_tests_test_fixture_form(directory, name, text, options, option_count, front, refused, discards, true, &windowed);
    return census && window;
}

// --facts' fixture (#2850's step 1, c_check_node_machine_facts.c): each kind of identifier's leaf (an automatic object, a
// parameter, a static object, a function, enumerators) and each lookup the fact words' rules take: `->` and `.`, chained
// and through a const record; a static record's member, an address constant; `*p`; subscripts of a pointer, of an
// automatic and a static array, at a variable index and at constant ones (a static array's element's address is folded),
// and of a member array; calls of a function, a variadic one, a pointer and a member, and of a function's designator and
// a pointer to one dereferenced (`*f` is the function again, #2970); and discards, written and an
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
    "    x = (*f)(a) + (**f)(a) + (*fp)(a);\n"
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
    static const uint64_t named[4] = { 2, 6, 6, 51 };
    static const uint64_t lookups[BUSTER_NODE_FACTS_LOOKUPS] = { 8, 6, 5, 10, 9, 22 };
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
    // The fixture's 22 discards, which a lean kernel doesn't make: each T's record by the discard's rule. Its other 38
    // lookups less the two left: each one's conversions' entries by its rule. Five go by their rule in any variant (a
    // member through an array's pointer, a constant index of an address constant, and the three dereferences of a
    // function's designator, which is no pointer's value). `p[1].n` is its one taken member of an operand that is no
    // address constant, at an offset: the record a dot's choices by masks would get wrong.
    const bool lean = mode != BUSTER_NODE_FACTS_WHOLE;
    held = held && shadow.facts_discards == (lean ? 22u : 0u) && shadow.facts_discards_derived == shadow.facts_discards &&
           shadow.facts_conversions == (lean ? 36u : 0u) && shadow.facts_conversions_derived == shadow.facts_conversions &&
           shadow.facts_taken_lookups - shadow.facts_short_lookups == 5 && (mode != BUSTER_NODE_FACTS_LOADS || shadow.facts_loads == shadow.facts_short_lookups);
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
static bool node_front_test_limits(const char* directory, bool boundaries, uint8_t front, uint8_t reader, char** options, uint32_t option_count)
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
                                     "unsigned long strlen(const char*);\nvoid sink(void*);\nint two(int, int);\nint late[5];\n"
                                     "enum Wide : unsigned __int128 { WIDE = (unsigned __int128)1 << 70, NARROW = 3 };\nint lb(int a)\n{\n    int x = a;\n");
        for (uint32_t round = 0; round < 1100 && length + 128 < capacity; round += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, "    x += (int)(&strlen)(\"ab\"); x += (int)strlen(\"cd\"); x++; x++;\n");
        }
        // The address of an array no declaration points to: T makes its pointer type itself, in a chunk where nothing
        // else makes a type (BUSTER_NODE_FRONT_TYPES: at reader time its word must be there when T reads it).
        // And an enumerator past 64 bits beside one within them: T's record of the first is no columns', and the fact
        // words' kernel at reader time (BUSTER_NODE_READER_FACTS) has the second's value from the reader's own column.
        length += (uint32_t)snprintf(text + length, capacity - length, "    sink(&late);\n    x += (int)(WIDE >> 68) + NARROW;\n");
        // And a sum whose first operand, a call, is 260 records before it, past a byte's distance: the front's records
        // don't give that expression's inputs, which are then the other making's, its call's arguments among them.
        length += (uint32_t)snprintf(text + length, capacity - length, "    x = two(a, x) + (a");
        for (uint32_t term = 1; term < 130 && length + 64 < capacity; term += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, " + a");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, ");\n    return x;\n}\n");
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
        .reader = reader,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    // In a build without the vector paths the reader's walk is the scalar one, and the front must make nothing.
    const bool fronts = true;
    const bool typed = (front & BUSTER_NODE_FRONT_TYPES) == 0 || !fronts ? shadow.typed_expressions == 0 && shadow.typed_compared == 0 :
                                                                           shadow.typed_differences == 0 && shadow.typed_compared != 0 && shadow.typed_unrun == 0 &&
                                                                               shadow.typed_object_differences == 0 && shadow.typed_library_differences == 0;
    // C and X there too, where asked: the same tapes across the chunks' ends, none left out.
    const bool swept = (front & BUSTER_NODE_FRONT_SWEEPS) == 0 || !fronts ? shadow.swept_expressions == 0 && shadow.swept_compared == 0 :
                                                                            shadow.swept_differences == 0 && shadow.swept_compared != 0 && shadow.swept_unkept == 0;
    // The flat front, where the front asks for it: at the chunks' ends every record is the lean front's and neither has
    // one the other lacks; where the lean front's stack is full the flat front, which has none, makes the records the
    // stack lost (the call astride its end among them), and those both make are equal.
    const bool flat = (front & BUSTER_NODE_FRONT_FLAT) == 0 || !fronts ? shadow.flat_compared == 0 && shadow.flat_records == 0 :
                                                                         shadow.flat_differences == 0 && shadow.flat_compared == shadow.front_records &&
                                                                             shadow.flat_lean_only == 0 && shadow.flat_unread == 0 && shadow.flat_beyond == 0 &&
                                                                             (shadow.flat_only != 0) == !boundaries;
    const bool same = shadow.front_differences == 0 && shadow.front_checked == shadow.front_records && shadow.front_waiting == 0 &&
                      (fronts ? shadow.front_units == 1 && shadow.front_unread == 0 && shadow.front_records != 0 && (shadow.front_resets != 0) == !boundaries :
                                shadow.front_units == 0 && shadow.front_records == 0) && typed && swept && flat &&
                      // The fact words' kernel at reader time, where asked: at the chunks' ends an expression whose records
                      // an earlier chunk made is not given by the front's, and its inputs are those of T's columns: four
                      // such, and the sum whose first operand is past a byte's distance. It leaves three records to T at
                      // the chunks' ends (the wide enumerator's shift and its cast, and their sum) and none where the stack
                      // is full. Either text has one literal in the expressions T takes there (the library calls' strings
                      // and the array's elements are in none), a line's; the wide enumerator's leaf is given. Where the
                      // front writes the window, its window gives all of them: the four whose records it kept whole at
                      // the chunks' ends, which the copy of its records then gives too, and the sum, whose operand's
                      // distance is whole there.
                      node_shadow_test_kernel(&shadow, reader, fronts, !boundaries ? 0 : (reader & BUSTER_NODE_READER_WINDOW) != 0 ? 1 : 5, boundaries ? 3 : 0, 1, 0, 0, 0);
    if (!valid || !same)
    {
        fprintf(stderr, "test-checker: node-front %s%s: %s: records %" PRIu64 ", compared %" PRIu64 ", %" PRIu64 " differ, %" PRIu64 " resets, %" PRIu64
                " values left waiting, in %" PRIu64 " units (%s)\n",
                boundaries ? "chunk boundaries" : "full stack", (front & BUSTER_NODE_FRONT_LEAN) != 0 ? " (lean)" : "", valid ? "read" : "NOT READ", shadow.front_records,
                shadow.front_checked,
                shadow.front_differences, shadow.front_resets, shadow.front_waiting, shadow.front_units,
                fronts ? "expected in the one" : "expected in none: no vector paths in this build");
        fprintf(stderr, "test-checker: node-front: the flat front's records %" PRIu64 ", compared %" PRIu64 ", %" PRIu64 " differ, %" PRIu64 " only its own, %" PRIu64
                " only the lean front's, %" PRIu64 " empty places, %" PRIu64 " tokens past the rings' reach\n",
                shadow.flat_records, shadow.flat_compared, shadow.flat_differences, shadow.flat_only, shadow.flat_lean_only, shadow.flat_empties, shadow.flat_beyond);
    }
    free(text);
    unlink(path);
    return valid && same;
}

// The front that writes the window where its arrays have no room to keep the waiting values' records
// (node_front_keep): an automatic array's initializer of 3,000 sums, whose values wait for the list's end and leave room
// for the records of the first few alone, and after them a statement expression of 7,280 statements of seven kinds in
// turn, each with a sum of five terms (nine records) as one operand of what follows it: an if statement's condition, an
// operator's first and second operand, a conditional's first, second and third, a call's argument. The seven's tokens
// are 115, an odd count, so the chunks' ends, every 1,024 tokens, fall at each of the 115 places within the seven in
// turn, and the statements have over 115 ends. What room the initializer's values leave is a few records, by how many
// values of the statement wait (a sum of two terms often keeps its three), and never nine: a whole sum that waits at an
// end keeps its place alone and is cut, and so is each record made above it. That expression's records are not the
// kernel's to make in place, nor the copy's, and its inputs are those of T's columns. Read with `options`. Whether the
// kernel's counts hold (node_shadow_test_kernel: 64 expressions not there, by either making; the 1,042 literals of the
// if statements' bodies, of `r`'s initializer and of the return's subscript each a line's; one record left to T, the
// statement expression's value's discard), the chunks' ends kept and cut as many values as they must, and nothing
// reset the stack (which a count of waiting values past the arrays' room does, with the window as without). Read again
// with the tests on the window ("The tests on the window"): they run on the 8,259 expressions the kernel makes in place
// and on none of the 64 the chunks' ends cut, whose records are not the window's, and none differs.
static bool node_facts_test_window_room(const char* directory, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/room.c", directory);
    static const char* const statements[] = {
        "if (a + b + a + b + a) { r += 1; }\n",
        "r += (a + b + a + b + a) * a;\n",
        "r += a * (a + b + a + b + a);\n",
        "r += a + b + a + b + a ? a : b;\n",
        "r += a ? a + b + a + b + a : b;\n",
        "r += a ? b : a + b + a + b + a;\n",
        "r += f2(a, a + b + a + b + a);\n",
    };
    const uint32_t count = 1040 * BUSTER_ARRAY_LENGTH(statements);
    const uint32_t capacity = 1u << 19;
    char* text = malloc(capacity);
    uint32_t length = 0;
    bool valid = text != NULL;
    if (valid)
    {
        length += (uint32_t)snprintf(text + length, capacity - length, "int f2(int, int);\nint wr(int a, int b)\n{\n    int big[3100] = {\n");
        for (uint32_t element = 0; element < 3000 && length + 64 < capacity; element += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, (element & 15) == 15 ? "a + 1,\n" : "a + 1, ");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, "({ int r = 0;\n");
        for (uint32_t statement = 0; statement < count && length + 64 < capacity; statement += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, "%s", statements[statement % BUSTER_ARRAY_LENGTH(statements)]);
        }
        length += (uint32_t)snprintf(text + length, capacity - length, "r; }) };\n    return big[0];\n}\n");
    }
    valid = valid && length + 64 < capacity && parse_test_write(path, text, length);
    NodeShadow shadow = {
        .scalar = true,
        .front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_TYPES,
        .reader = BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    const bool fronts = true;
    const uint64_t cut = fronts ? 64 : 0;
    const bool held = valid && node_shadow_test_kernel(&shadow, shadow.reader, fronts, cut, fronts ? 1 : 0, fronts ? 1042 : 0, 0, 0, cut) &&
                      (!fronts || (shadow.reader_window_whys[BUSTER_NODE_FACTS_WHY_CUT] == cut && shadow.reader_facts_whys[BUSTER_NODE_FACTS_WHY_CUT] == cut &&
                                   shadow.front_window_kept_values == 10254 && shadow.front_window_kept_records == 30310 &&
                                   shadow.front_window_cut_values == 357881 && shadow.front_resets == 0 && shadow.front_differences == 0));
    if (!held)
    {
        fprintf(stderr, "test-checker: node-facts window, no room for the waiting values' records: %s; %" PRIu64 " expressions cut for the window and %" PRIu64
                " for the copy (expected %" PRIu64 " of each), %" PRIu64 " values kept whole (expected 10254), their records %" PRIu64 " (expected 30310), %" PRIu64
                " cut (expected 357881), %" PRIu64 " resets\n",
                valid ? "read" : "NOT READ", shadow.reader_window_whys[BUSTER_NODE_FACTS_WHY_CUT], shadow.reader_facts_whys[BUSTER_NODE_FACTS_WHY_CUT], cut,
                shadow.front_window_kept_values, shadow.front_window_kept_records, shadow.front_window_cut_values, shadow.front_resets);
    }
    NodeShadow tested = {
        .front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS,
        .reader = BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus again = { .shadow = &tested };
    fflush(stdout);
    const bool read = valid && node_census_file(&again, path, options, option_count) && again.files == 1 && again.failures == 0;
    const uint64_t placed = 8259;
    const bool tests = read && tested.reader_facts[NODE_FACTS_COUNT_WINDOW_SKIPPED] == cut && tested.tested_expressions == placed + cut &&
                       tested.reader_facts[NODE_FACTS_COUNT_TESTS_EXPRESSIONS] == placed && tested.own_expressions == placed &&
                       tested.reader_facts[NODE_FACTS_COUNT_TESTS_DIFFERENCES] == 0 && tested.own_count_differences == 0;
    if (!tests)
    {
        fprintf(stderr, "test-checker: node-facts window, no room for the waiting values' records, with the tests on the window: %s; the tests ran on %" PRIu64
                " expressions at reader time, of which %" PRIu64 " were cut for the window (expected %" PRIu64 "), and on the window on %" PRIu64 " (expected %" PRIu64
                "), %" PRIu64 " not the census's code's, %" PRIu64 " walks of another count\n",
                read ? "read" : "NOT READ", tested.tested_expressions, tested.reader_facts[NODE_FACTS_COUNT_WINDOW_SKIPPED], cut,
                tested.reader_facts[NODE_FACTS_COUNT_TESTS_EXPRESSIONS], placed, tested.reader_facts[NODE_FACTS_COUNT_TESTS_DIFFERENCES], tested.own_count_differences);
    }
    free(text);
    unlink(path);
    return held && tests;
}

// The front that writes the window where a value waits through several chunks' ends: an if statement's condition, a
// call of two calls with a sum of a literal among their arguments, whose body is two long lists of declarations (no
// record, some 3,000 postorder positions each) with three statements after each. The condition's records are moved
// once, down to the arrays' start, and then kept where they are, through six ends; its literal's line and its three
// calls' arguments are kept with them, and the calls made meanwhile take the arguments' places after theirs. What they
// are moved over, and what is made after them, is there to show a column, a line or an argument that did not move:
// - before the condition, `x = v(9L, ...)`, twelve long's literals in a row, are the records the condition's leaves
//   are moved over: a long's literal where its `1` goes and where a name goes (a kept leaf's type, integer and
//   category, which the copy reads; and that literal's line is where the `1`'s has to be). It, `x += g1(8)` and
//   `x += h2(y += 1, a)` put fifteen arguments before the condition's in the list, so that keeping them moves them,
//   and the last's first argument asks a later round than its last (a call's class has the greatest); `zc + 2.0i` has
//   a literal the columns don't hold whole, an imaginary one, whose leaf is no line's; `__extension__ (a + a)` is an
//   operator the kernel makes no record of, over one it makes, whose class is none;
// - in the body, `x += v(7, ...)`, sixty literals in a row, writes a line at each of sixty records' numbers, the
//   condition's `1`'s first among them; and `x += s1("...")` has a string, of a length a block.
// Read with `options`. Whether the kernel's counts hold (node_shadow_test_kernel: every expression's records there,
// each of the 139 other literals and strings a line's, the complex sum's three records left to T), the condition's ten
// records were kept whole at each end, and the calls' arguments' list held 67 at most: the condition's four and the 63
// of a block's three calls.
static bool node_facts_test_window_wait(const char* directory, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/wait.c", directory);
    const uint32_t capacity = 1u << 17;
    char* text = malloc(capacity);
    uint32_t length = 0;
    bool valid = text != NULL;
    if (valid)
    {
        length += (uint32_t)snprintf(text + length, capacity - length,
                                     "int g1(int);\nint h2(int, int);\nint s1(const char*);\nint v(int, ...);\ndouble _Complex zc;\nint ww(int a)\n{\n"
                                     "    int x;\n    x = v(9L, 9L, 9L, 9L, 9L, 9L, 9L, 9L, 9L, 9L, 9L, 9L);\n    x += g1(8);\n    zc = zc + 2.0i;\n"
                                     "    x += __extension__ (a + a);\n    int y = a;\n    x += h2(y += 1, a);\n    if (h2(g1(a + 1), g1(a)))\n    {\n");
        for (uint32_t block = 0; block < 2 && length + 512 < capacity; block += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, "        extern int d%u_0", block);
            for (uint32_t name = 1; name < 1500 && length + 512 < capacity; name += 1)
            {
                length += (uint32_t)snprintf(text + length, capacity - length, (name & 15) == 15 ? ",\n d%u_%u" : ", d%u_%u", block, name);
            }
            length += (uint32_t)snprintf(text + length, capacity - length, ";\n        x = h2(x, %u);\n        x += v(7", block + 2);
            for (uint32_t literal = 1; literal < 60; literal += 1)
            {
                length += (uint32_t)snprintf(text + length, capacity - length, ", 7");
            }
            length += (uint32_t)snprintf(text + length, capacity - length, ");\n        x += s1(\"a string of block %u%s\");\n", block, block != 0 ? "..." : "");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, "    }\n    return x;\n}\n");
    }
    valid = valid && length + 512 < capacity && parse_test_write(path, text, length);
    NodeShadow shadow = {
        .scalar = true,
        .front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_TYPES,
        .reader = BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    const bool fronts = true;
    const bool held = valid && node_shadow_test_kernel(&shadow, shadow.reader, fronts, 0, fronts ? 3 : 0, fronts ? 139 : 0, 0, 0, 0) &&
                      (!fronts || (shadow.front_window_kept_values == 6 && shadow.front_window_kept_records == 60 && shadow.front_window_cut_values == 0 &&
                                   shadow.front_window_arguments_most == 67 && shadow.front_resets == 0 && shadow.front_differences == 0));
    if (!held)
    {
        fprintf(stderr, "test-checker: node-facts window, a condition waiting through six chunks' ends: %s; %" PRIu64 " values kept whole (expected 6), their records %"
                PRIu64 " (expected 60), %" PRIu64 " cut, the calls' arguments' list at most %" PRIu64 " (expected 67)\n",
                valid ? "read" : "NOT READ", shadow.front_window_kept_values, shadow.front_window_kept_records, shadow.front_window_cut_values,
                shadow.front_window_arguments_most);
    }
    free(text);
    unlink(path);
    return held;
}

// The front that writes the window where the waiting values are calls: an automatic array's initializer of 2,000 calls
// of three arguments. Each waits for the list's end as a row of five records while the rows have room, and as its place
// alone, cut, once they don't. A cut call's arguments are not kept: the list holds an argument for a record the arrays
// hold, 2,133 at most here, and would pass its room at the 1,366th waiting call otherwise. Read with `options`. Whether
// the kernel's counts hold (the three expressions after the array there, their one literal a line's), the chunks' ends
// kept and cut as many values as they must, the list held that many, and nothing reset the stack.
static bool node_facts_test_window_calls(const char* directory, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/calls.c", directory);
    const uint32_t capacity = 1u << 16;
    char* text = malloc(capacity);
    uint32_t length = 0;
    bool valid = text != NULL;
    if (valid)
    {
        length += (uint32_t)snprintf(text + length, capacity - length, "int f3(int, int, int);\nint wc(int a, int b)\n{\n    int big[2100] = {\n");
        for (uint32_t element = 0; element < 2000 && length + 128 < capacity; element += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, (element & 7) == 7 ? "f3(a, b, a),\n" : "f3(a, b, a), ");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, "a + b };\n    int x = big[0];\n    x += f3(a, b, x);\n    return x;\n}\n");
    }
    valid = valid && length + 128 < capacity && parse_test_write(path, text, length);
    NodeShadow shadow = {
        .scalar = true,
        .front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_TYPES,
        .reader = BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    const bool fronts = true;
    const bool held = valid && node_shadow_test_kernel(&shadow, shadow.reader, fronts, 0, 0, fronts ? 1 : 0, 0, 0, 0) &&
                      (!fronts || (shadow.front_window_kept_values == 7015 && shadow.front_window_kept_records == 34939 &&
                                   shadow.front_window_cut_values == 10376 && shadow.front_window_arguments_most == 2133 && shadow.front_resets == 0 &&
                                   shadow.front_differences == 0));
    if (!held)
    {
        fprintf(stderr, "test-checker: node-facts window, waiting calls: %s; %" PRIu64 " values kept whole (expected 7015), their records %" PRIu64
                " (expected 34939), %" PRIu64 " cut (expected 10376), the calls' arguments' list at most %" PRIu64 " (expected 2133, room %u), %" PRIu64 " resets\n",
                valid ? "read" : "NOT READ", shadow.front_window_kept_values, shadow.front_window_kept_records, shadow.front_window_cut_values,
                shadow.front_window_arguments_most, BUSTER_NODE_FRONT_RECORDS, shadow.front_resets);
    }
    free(text);
    unlink(path);
    return held;
}

// The front that writes the window where a value that was cut finds room again: an automatic array's initializer of
// 700 literals and then a call whose first argument is a sum of 300 terms (599 records) and whose other two are
// subscripts of compound literals of 2,300 and of 600 elements. The sum waits for the call. While the first compound
// literal's elements wait above it the rows have room for its records at four chunks' ends and none at the fifth,
// where it keeps its place alone; the literal's end then takes its elements off the stack, and at the next end the
// rows would have room for 599 records again. The sum's are gone: a value cut once is cut for good, its place the one
// record after the kept ones before it. Read with `options`. Whether the kernel's counts hold (the three expressions
// after the array there, their one literal a line's), and the chunks' ends kept and cut what they must: the sum whole
// four times and cut twice.
static bool node_facts_test_window_again(const char* directory, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/again.c", directory);
    const uint32_t capacity = 1u << 16;
    char* text = malloc(capacity);
    uint32_t length = 0;
    bool valid = text != NULL;
    if (valid)
    {
        length += (uint32_t)snprintf(text + length, capacity - length, "int h3(int, int, int);\nint wn(int a, int b)\n{\n    int big[800] = {\n");
        for (uint32_t element = 0; element < 700 && length + 128 < capacity; element += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, (element & 31) == 31 ? "1,\n" : "1, ");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, "h3(a");
        for (uint32_t term = 0; term < 299 && length + 128 < capacity; term += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, (term & 31) == 31 ? " + b\n" : " + b");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, ", (int[2400]){ 1");
        for (uint32_t element = 0; element < 2300 && length + 128 < capacity; element += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, (element & 31) == 31 ? ", 1\n" : ", 1");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, " }[0], (int[700]){ 1");
        for (uint32_t element = 0; element < 600 && length + 128 < capacity; element += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, (element & 31) == 31 ? ", 1\n" : ", 1");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, " }[0]) };\n    int x = big[0];\n    x += a + b;\n    return x;\n}\n");
    }
    valid = valid && length + 128 < capacity && parse_test_write(path, text, length);
    NodeShadow shadow = {
        .scalar = true,
        .front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_TYPES,
        .reader = BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    const bool fronts = true;
    const bool held = valid && node_shadow_test_kernel(&shadow, shadow.reader, fronts, 0, 0, fronts ? 1 : 0, 0, 0, 0) &&
                      (!fronts || (shadow.front_window_kept_values == 10137 && shadow.front_window_kept_records == 12529 && shadow.front_window_cut_values == 2 &&
                                   shadow.front_resets == 0 && shadow.front_differences == 0));
    if (!held)
    {
        fprintf(stderr, "test-checker: node-facts window, a value cut and then with room again: %s; %" PRIu64 " values kept whole (expected 10137), their records %"
                PRIu64 " (expected 12529), %" PRIu64 " cut (expected 2), %" PRIu64 " resets\n",
                valid ? "read" : "NOT READ", shadow.front_window_kept_values, shadow.front_window_kept_records, shadow.front_window_cut_values, shadow.front_resets);
    }
    free(text);
    unlink(path);
    return held;
}

// The fact words' kernel at reader time on more literals than a batch's lines hold (BUSTER_NODE_FACTS_READER_LINES, a
// third of them to each making of the inputs): one body of 16,500 statements `x += 1;`, read with `options`. A batch's
// lines are taken away with the batch, so each literal's leaf is a line's; were the lines kept, the unit's last hundred
// literals would have none. Whether the kernel's counts hold (node_shadow_test_kernel): nothing differs, a line a
// literal, no record left to T, and every expression given by the front's records (none of these is cut by a chunk's
// end). `reader`: what runs at reader time (BUSTER_NODE_READER_FACTS, and BUSTER_NODE_READER_WINDOW for the front's own
// windows, whose makings take three more lines a literal).
static bool node_facts_test_reader_lines(const char* directory, uint8_t reader, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/lines.c", directory);
    const uint32_t statements = 16500;
    const uint32_t capacity = 1u << 18;
    char* text = malloc(capacity);
    uint32_t length = 0;
    bool valid = text != NULL;
    if (valid)
    {
        length += (uint32_t)snprintf(text + length, capacity - length, "int ll(int a)\n{\n    int x = a;\n");
        for (uint32_t statement = 0; statement < statements && length + 64 < capacity; statement += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, (statement & 15) == 15 ? "x += 1;\n" : "x += 1; ");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, "    return x;\n}\n");
    }
    valid = valid && length + 64 < capacity && parse_test_write(path, text, length);
    NodeShadow shadow = {
        .scalar = true,
        .front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_TYPES,
        .reader = reader,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    const bool fronts = true;
    const bool held = valid && node_shadow_test_kernel(&shadow, shadow.reader, fronts, 0, 0, fronts ? statements : 0, 0, 0, 0);
    if (!held)
    {
        fprintf(stderr, "test-checker: node-facts at reader time, %u literals in one body: %s\n", statements, valid ? "read" : "NOT READ");
    }
    free(text);
    unlink(path);
    return held;
}

// The flat front past its rings' reach, and its empty records, on generated text read with `options`:
//   - a statement that is one value the front leaves, the unit's first: an unknown value is no place, however few
//     records the unit has;
//   - a condition whose body is some 13,500 tokens, more than the rings hold (BUSTER_NODE_FLAT_WINDOW positions). The
//     lean front's stack keeps the condition's value until its closing parenthesis and makes its record; the flat front
//     can't reach it, and leaves the token;
//   - a call, an operator and a conditional of a value the front leaves (a sizeof, the walk's own visit);
//   - a left condition whose body ends in a later chunk: its consumer reads an earlier chunk's empty record;
//   - four whose bodies are over 255 records, one at least within a chunk: the consumer is found by its place, not by
//     the byte;
//   - a sizeof alone, a declaration of two initializers, one of them left (the other's discard is empty with it), a
//     literal the front leaves under an operator, and a switch;
//   - two declarations whose `;` the flat front leaves, its first value past the rings' reach, where the lean front
//     makes the `;`'s discards: a braced initializer of four values, and 9,000 adjacent strings. A value near the `;`
//     has a consumer in the lean front and none in the flat one, within the byte (the fourth value's, and the
//     strings') and past it (the third's): no difference, where that consumer is a record only the lean front made.
// Whether the long body's condition and those two declarations' five discards are the six records the flat front lacks,
// every other record is equal, and the left values' consumers kept their places, empty.
static bool node_flat_test_reach(const char* directory, bool first, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/reach.c", directory);
    const uint32_t capacity = 1u << 17;
    char* text = malloc(capacity);
    uint32_t length = 0;
    bool valid = text != NULL;
    if (valid)
    {
        length += (uint32_t)snprintf(text + length, capacity - length, "int f3(int, int, int);\nint lr(int a, int b)\n{\n    int x = a;\n    '\\n';\n    if (a > b)\n    {\n");
        for (uint32_t round = 0; round < 450 && length + 128 < capacity; round += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, "        x++; x++; x++; x++; x++; x++; x++; x++; x++; x++;\n");
        }
        // A sizeof is the walk's own visit: the fronts leave it, and with it the call, the sum and the conditional that
        // take its value, and the statements that discard them.
        length += (uint32_t)snprintf(text + length, capacity - length,
                                     "    }\n    x = f3(a, (int)sizeof(b), b) + 1;\n    x = a ? (int)sizeof(a) + b : b;\n    a = b, x = (int)sizeof(x);\n");
        // A left condition before a body of 1,200 tokens, which ends in a later chunk.
        length += (uint32_t)snprintf(text + length, capacity - length, "    if (a > (int)sizeof(b))\n    {\n");
        for (uint32_t round = 0; round < 40 && length + 128 < capacity; round += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, "        x++; x++; x++; x++; x++; x++; x++; x++; x++; x++;\n");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, "    }\n");
        // Four left conditions before bodies of 300 records: a consumer over 255 records after its value.
        for (uint32_t round = 0; round < 4 && length + 2048 < capacity; round += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, "    if (b > (int)sizeof(a))\n    {\n");
            for (uint32_t line = 0; line < 10; line += 1)
            {
                length += (uint32_t)snprintf(text + length, capacity - length, "        x++; x++; x++; x++; x++; x++; x++; x++; x++; x++;\n");
            }
            length += (uint32_t)snprintf(text + length, capacity - length, "    }\n");
        }
        length += (uint32_t)snprintf(text + length, capacity - length,
                                     "    sizeof(a);\n    int p = a, q = (int)sizeof(a) + b;\n    x += p + q;\n    x = a + '\\n';\n    switch (a)\n    {\n        default:\n"
                                     "            x++;\n    }\n    return x;\n}\n");
        // A declaration's `;` whose first value is past the rings' reach and whose last ones are not: the flat front
        // leaves it, and the lean front, whose stack holds every value, makes its discards. A braced initializer of
        // four values: the second 4,200 terms long, the third's consumer over 255 records after it, the fourth's four
        // records after it. And 9,000 adjacent strings, whose one value is the first's.
        length += (uint32_t)snprintf(text + length, capacity - length, "static const unsigned far_values[] = { 1, 0");
        for (uint32_t round = 0; round < 420 && length + 128 < capacity; round += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, " + 0 + 0 + 0 + 0 + 0 + 0 + 0 + 0 + 0 + 0\n");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, ", 2");
        for (uint32_t round = 0; round < 6 && length + 128 < capacity; round += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, " + 0 + 0 + 0 + 0 + 0 + 0 + 0 + 0 + 0 + 0\n");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, ", 3");
        for (uint32_t round = 0; round < 14 && length + 128 < capacity; round += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, " + 0 + 0 + 0 + 0 + 0 + 0 + 0 + 0 + 0 + 0\n");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, " };\nstatic const char far_strings[] =\n");
        for (uint32_t round = 0; round < 900 && length + 128 < capacity; round += 1)
        {
            length += (uint32_t)snprintf(text + length, capacity - length, " \"a\" \"a\" \"a\" \"a\" \"a\" \"a\" \"a\" \"a\" \"a\" \"a\"\n");
        }
        length += (uint32_t)snprintf(text + length, capacity - length, ";\nunsigned far_use(void) { return far_values[0] + (unsigned)far_strings[0]; }\n");
    }
    valid = valid && length + 256 < capacity && parse_test_write(path, text, length);
    NodeShadow shadow = {
        .scalar = true,
        .front = (uint8_t)(BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_FLAT | (first ? BUSTER_NODE_FRONT_FLAT_FIRST : 0)),
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    // In a build without the vector paths the reader's walk is the scalar one, and the fronts must make nothing.
    const bool fronts = true;
    const bool same = fronts ? shadow.front_differences == 0 && shadow.flat_differences == 0 && shadow.flat_beyond == 3 && shadow.flat_lean_only == 6 &&
                                   shadow.flat_only == 0 && shadow.flat_unread == 0 && shadow.flat_compared + 6 == shadow.front_records && shadow.flat_empties >= 8 :
                               shadow.front_units == 0 && shadow.flat_compared == 0 && shadow.flat_records == 0;
    if (!valid || !same)
    {
        fprintf(stderr, "test-checker: node-flat reach%s: %s: the lean front's records %" PRIu64 ", %" PRIu64 " differ; the flat front's %" PRIu64 ", compared %" PRIu64
                ", %" PRIu64 " differ, %" PRIu64 " only its own, %" PRIu64 " only the lean front's, %" PRIu64 " empty places, %" PRIu64 " tokens past the rings' reach\n",
                first ? " (before the walk)" : "", valid ? "read" : "NOT READ", shadow.front_records, shadow.front_differences, shadow.flat_records, shadow.flat_compared,
                shadow.flat_differences, shadow.flat_only, shadow.flat_lean_only, shadow.flat_empties, shadow.flat_beyond);
    }
    free(text);
    unlink(path);
    return valid && same;
}

// The fronts on the no_builtin fixture (#2968), read with `options`: the front `front` makes the unit's calls, their
// `arguments` arguments, though two of its functions have the attribute; each record is the walk's node, the whole
// front's with its call's library mark, which the walk gives by the function that holds the call; and the flat front's
// records beside the lean one's are the lean one's, neither with a record the other lacks.
static bool node_front_test_builtins(const char* directory, uint8_t front, uint64_t arguments, char** options, uint32_t option_count)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/front-builtins.c", directory);
    bool valid = parse_test_write(path, node_census_test_builtins, strlen(node_census_test_builtins));
    NodeShadow shadow = {
        .front = front,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    const bool flat = (front & BUSTER_NODE_FRONT_FLAT) != 0;
    const bool same = shadow.front_differences == 0 && shadow.front_units == 1 && shadow.front_unread == 0 && shadow.front_checked == shadow.front_records &&
                      shadow.front_arguments == arguments &&
                      (flat ? shadow.flat_differences == 0 && shadow.flat_compared == shadow.front_records && shadow.flat_only == 0 && shadow.flat_lean_only == 0 &&
                                  shadow.flat_unread == 0 :
                              shadow.flat_compared == 0 && shadow.flat_records == 0);
    if (!valid || !same)
    {
        fprintf(stderr, "test-checker: node-front builtins (front %u): %s: the front's records %" PRIu64 ", checked %" PRIu64 ", %" PRIu64 " differ, its calls' arguments %" PRIu64
                        " (%" PRIu64 " expected); the flat front's %" PRIu64 ", compared %" PRIu64 ", %" PRIu64 " differ, %" PRIu64 " only its own, %" PRIu64
                        " only the lean front's\n",
                front, valid ? "read" : "NOT READ", shadow.front_records, shadow.front_checked, shadow.front_differences, shadow.front_arguments, arguments, shadow.flat_records,
                shadow.flat_compared, shadow.flat_differences, shadow.flat_only, shadow.flat_lean_only);
    }
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
    check_test_expect(test, ready && node_census_test_fixture(directory, "defaults.c", node_census_test_defaults, quiet, BUSTER_ARRAY_LENGTH(quiet), &system),
                      "node-census refuses what warns without an option, by its test, with the default warnings alone");
    char* enums[] = { "-nostdinc",
                      "-Wassign-enum",
                      "-Wno-enum-compare",
                      "-Wno-enum-enum-conversion",
                      "-Wno-enum-compare-conditional",
                      "-Wno-enum-float-conversion",
                      "-Wno-anon-enum-enum-conversion",
                      "-Wno-implicit-enum-enum-cast" };
    check_test_expect(test, ready && node_census_test_fixture(directory, "enums.c", node_census_test_enums, enums, BUSTER_ARRAY_LENGTH(enums), &system),
                      "node-census refuses an enumeration's constant by its own test where no mix of enumerations is asked about");
    char* off[] = { "-nostdinc",
                    "-Wno-unsequenced",
                    "-Wno-return-stack-address",
                    "-Wno-unused-value",
                    "-Wno-unused-comparison",
                    "-Wno-parentheses",
                    "-Wno-logical-not-parentheses",
                    "-Wno-pointer-to-int-cast" };
    check_test_expect(test, ready && node_census_test_fixture(directory, "off.c", node_census_test_off, off, BUSTER_ARRAY_LENGTH(off), &system),
                      "node-census takes what a warning that is off would refuse");
    char* whole[] = { "-nostdinc", "-Wc++-compat" };
    check_test_expect(test, ready && node_census_test_fixture(directory, "whole.c", node_census_test_whole, whole, BUSTER_ARRAY_LENGTH(whole), &system),
                      "node-census refuses every expression of a unit with a warning no test stands for");
    char* tagged[] = { "-nostdinc", "-Wtype-safety" };
    check_test_expect(test, ready && node_census_test_fixture(directory, "tagged.c", node_census_test_tagged, tagged, BUSTER_ARRAY_LENGTH(tagged), &system),
                      "node-census refuses every call of a unit with type tags");
    check_test_expect(test, ready && node_census_test_fixture(directory, "builtins.c", node_census_test_builtins, quiet, BUSTER_ARRAY_LENGTH(quiet), &system),
                      "node-census asks a library call's own semantics by the function that holds it, in a unit with no_builtin");
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
    // What the tests on the window count on each of the eight fixtures, which no verdict shows.
    static const NodeTestsWindowed windowed_warnings = {
        .expressions = 45,
        .trees = 4,
        .operators = 56,
        .sent = 21,
        .walks = 4,
        .quiet = 6,
        .calls = 1,
        .calls_checked = 1,
        .casts = 1,
        .casts_left = 4,
        .asks = 1,
        .values = 45,
        .spared = 34,
        .direct = 2,
        .apart = 3,
    };
    static const NodeTestsWindowed windowed_clean = {
        .expressions = 23,
        .trees = 1,
        .operators = 23,
        .sent = 10,
        .walks = 4,
        .quiet = 4,
        .calls = 1,
        .calls_checked = 1,
        .casts = 1,
        .casts_left = 1,
        .asks = 1,
        .values = 26,
        .spared = 17,
        .direct = 3,
        .apart = 2,
    };
    static const NodeTestsWindowed windowed_set = {
        .expressions = 13,
        .operators = 8,
        .quiet = 2,
        .values = 8,
        .spared = 5,
        .direct = 2,
    };
    static const NodeTestsWindowed windowed_quiet = {
        .expressions = 7,
        .operators = 3,
        .sent = 2,
        .quiet = 1,
        .values = 7,
        .spared = 5,
        .direct = 2,
    };
    static const NodeTestsWindowed windowed_atomic = {
        .expressions = 3,
        .operators = 3,
        .sent = 3,
        .quiet = 1,
        .values = 1,
    };
    static const NodeTestsWindowed windowed_defaults = {
        .expressions = 419,
        .trees = 29,
        .operators = 95,
        .sent = 29,
        .walks = 11,
        .far = 37,
        .quiet = 67,
        .calls = 173,
        .calls_checked = 173,
        .casts = 157,
        .casts_left = 17,
        .asks = 5,
        .values = 197,
        .spared = 242,
        .direct = 200,
        .apart = 19,
    };
    static const NodeTestsWindowed windowed_enums = {
        .expressions = 5,
        .operators = 2,
        .quiet = 1,
        .values = 4,
        .spared = 4,
        .direct = 2,
    };
    static const NodeTestsWindowed windowed_off = {
        .expressions = 13,
        .trees = 5,
        .quiet = 1,
        .values = 4,
        .spared = 6,
        .direct = 6,
        .apart = 1,
    };
    static const NodeTestsWindowed windowed_whole = {
        .expressions = 2,
        .spared = 1,
        .direct = 2,
    };
    static const NodeTestsWindowed windowed_tagged = {
        .expressions = 4,
        .operators = 3,
        .values = 2,
    };
    static const NodeTestsWindowed windowed_builtins = {
        .expressions = 11,
        .calls = 10,
        .calls_checked = 10,
        .casts = 9,
        .casts_left = 4,
        .asks = 3,
        .values = 7,
        .direct = 10,
    };
    static const NodeTestsWindowed windowed_system = {
        .expressions = 3,
        .operators = 1,
        .quiet = 1,
        .values = 2,
        .spared = 1,
        .direct = 2,
    };
    // The same fixtures with the census's tests at reader time, after T there: the machine's verdict before the pipeline
    // is never a take the census refuses, and every test's bit is the census's.
    check_test_expect(test, ready && node_tests_test_fixture(directory, "warnings.c", node_census_test_warnings, loud, BUSTER_ARRAY_LENGTH(loud),
                                                             BUSTER_NODE_FRONT_AFTER, 40, 3, windowed_warnings),
                      "node-tests: at reader time the tests refuse each expression the compiler warns of, as the census does after the pipeline");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "clean.c", node_census_test_clean, loud, BUSTER_ARRAY_LENGTH(loud),
                                                             BUSTER_NODE_FRONT_BEFORE, 0, 6, windowed_clean),
                      "node-tests: at reader time the tests refuse none of the clean idioms");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "set.c", node_census_test_set, loud, BUSTER_ARRAY_LENGTH(loud),
                                                             BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN, 10, 6, windowed_set),
                      "node-tests: at reader time the set's tests and T's own refusals leave nothing the census refuses");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "quiet.c", node_census_test_quiet, quiet, BUSTER_ARRAY_LENGTH(quiet),
                                                             BUSTER_NODE_FRONT_AFTER, 0, 1, windowed_quiet),
                      "node-tests: at reader time warnings that are off by default refuse nothing");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "atomic.c", node_census_test_atomic, quiet, BUSTER_ARRAY_LENGTH(quiet),
                                                             BUSTER_NODE_FRONT_AFTER, 2, 0, windowed_atomic),
                      "node-tests: at reader time atomic record accesses are refused by type");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "defaults.c", node_census_test_defaults, quiet, BUSTER_ARRAY_LENGTH(quiet),
                                                             BUSTER_NODE_FRONT_AFTER, 170, 17, windowed_defaults),
                      "node-tests: at reader time the tests refuse what warns without an option, with the default warnings alone");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "enums.c", node_census_test_enums, enums, BUSTER_ARRAY_LENGTH(enums),
                                                             BUSTER_NODE_FRONT_AFTER, 1, 1, windowed_enums),
                      "node-tests: at reader time an enumeration's constant is tested where no mix of enumerations is asked about");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "off.c", node_census_test_off, off, BUSTER_ARRAY_LENGTH(off), BUSTER_NODE_FRONT_AFTER, 1, 2, windowed_off),
                      "node-tests: at reader time a warning that is off refuses nothing and lists no expression, and the unit's first warning event refuses its own");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "whole.c", node_census_test_whole, whole, BUSTER_ARRAY_LENGTH(whole), BUSTER_NODE_FRONT_AFTER, 2, 0,
                                                             windowed_whole),
                      "node-tests: at reader time a warning no test stands for refuses every expression");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "tagged.c", node_census_test_tagged, tagged, BUSTER_ARRAY_LENGTH(tagged), BUSTER_NODE_FRONT_AFTER, 3,
                                                             1, windowed_tagged),
                      "node-tests: at reader time a unit with type tags has every call's test, and its loop leaves no call out");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "builtins.c", node_census_test_builtins, quiet, BUSTER_ARRAY_LENGTH(quiet), BUSTER_NODE_FRONT_AFTER,
                                                             0, 0, windowed_builtins),
                      "node-tests: at reader time a unit with no_builtin has its calls made, and each library call's semantics by its own function");
    check_test_expect(test, written && node_tests_test_fixture(directory, "system.c", node_census_test_system, isystem, BUSTER_ARRAY_LENGTH(isystem),
                                                               BUSTER_NODE_FRONT_AFTER, 0, 0, windowed_system),
                      "node-tests: at reader time a system header's macros in a user's expression refuse nothing");
    if (written)
    {
        unlink(header);
        rmdir(headers);
    }
    // With T at reader time too (BUSTER_NODE_FRONT_TYPES): the shadow's records on every expression both type; by the rows
    // there against the action alone here in the --scalar run. And with C and X there (BUSTER_NODE_FRONT_SWEEPS) in the
    // --scalar and the lean runs: their tapes, patched with the program's symbols, the shadow's own.
    check_test_expect(test, ready && node_shadow_test_fixture(directory, false, BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_TYPES, 0, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-shadow: the machine takes its codes with the pipeline's values and entries, T's rows the action's, and refuses the rest by reason");
    check_test_expect(test, ready && node_shadow_test_fixture(directory, true, BUSTER_NODE_FRONT_BEFORE | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS, 0, loud,
                                                              BUSTER_ARRAY_LENGTH(loud)),
                      "node-shadow --scalar: T by the action alone gives the pipeline's values and entries, and C and X at reader time the shadow's tapes");
    // Plain char unsigned: T's rules convert a char constant by the unit's signedness, not the bake's.
    char* unsigned_char[] = { "-nostdinc", "-Wall", "-Wextra", "-Wshift-sign-overflow", "-Wassign-enum", "-funsigned-char" };
    check_test_expect(test, ready && node_shadow_test_fixture(directory, false,
                                                              BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS, 0,
                                                              unsigned_char, BUSTER_ARRAY_LENGTH(unsigned_char)),
                      "node-shadow -funsigned-char: the machine converts plain char constants as the unit's char is signed, at reader time too");
    // Without warnings the census takes a for statement's clauses too, and C and X at reader time are compared on them.
    check_test_expect(test, ready && node_shadow_test_fixture(directory, false,
                                                              BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS, 0, quiet,
                                                              BUSTER_ARRAY_LENGTH(quiet)),
                      "node-shadow without warnings: a for statement's clauses' tapes at reader time are the shadow's");
    // #2850's fact words' kernel at reader time (BUSTER_NODE_READER_FACTS), on the lean front's records: after T alone,
    // and with the census's tests and C and X there too, where it counts the whole machine's expressions.
    const uint8_t lean_types = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_TYPES;
    check_test_expect(test, ready && node_shadow_test_fixture(directory, false, lean_types, BUSTER_NODE_READER_FACTS, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-facts at reader time: the kernel's records from the front's inputs are T's there, and the inputs those made of T's columns");
    check_test_expect(test, ready && node_shadow_test_fixture(directory, true, lean_types | BUSTER_NODE_FRONT_SWEEPS, BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_TESTS,
                                                              unsigned_char, BUSTER_ARRAY_LENGTH(unsigned_char)),
                      "node-facts at reader time, with the tests and C and X there: the same records, and the whole machine's expressions counted");
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
    check_test_expect(test, ready && node_front_test_limits(directory, true, BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_TYPES, 0, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-front: a callee waiting across a chunk's end keeps its use, a library call's mark the walk's");
    check_test_expect(test, ready && node_front_test_limits(directory, true, BUSTER_NODE_FRONT_BEFORE, 0, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-front before the walk: a callee waiting across a chunk's end keeps its use");
    check_test_expect(test, ready && node_front_test_limits(directory, true,
                                                            BUSTER_NODE_FRONT_BEFORE | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS, 0, loud,
                                                            BUSTER_ARRAY_LENGTH(loud)),
                      "node-front, lean: a value waiting across a chunk's end gets its consumer's distance and its place there, and C and X its tape");
    check_test_expect(test, ready && node_front_test_limits(directory, false, BUSTER_NODE_FRONT_AFTER, 0, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-front: a full stack makes every waiting value unknown and leaves the call astride its end to the walk");
    check_test_expect(test, ready && node_front_test_limits(directory, false, BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN, 0, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-front, lean: a full stack makes every waiting value unknown");
    check_test_expect(test, ready && node_front_test_limits(directory, true, lean_types, BUSTER_NODE_READER_FACTS, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-facts at reader time: an expression a chunk's end cuts has the inputs of T's columns, and every other the front's");
    check_test_expect(test, ready && node_front_test_limits(directory, false, lean_types, BUSTER_NODE_READER_FACTS, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-facts at reader time: the same records where the front's stack is full");
    check_test_expect(test, ready && node_facts_test_reader_lines(directory, BUSTER_NODE_READER_FACTS, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-facts at reader time: a batch's lines are taken away with it, and a unit of 16,500 literals has a line for each");
    // The front writes the kernel's window as it makes each record (BUSTER_NODE_READER_WINDOW): on the fixture, with the
    // tests and C and X there, at the chunks' ends, where the stack is full, on more literals than a batch's lines hold,
    // and where the arrays have no room to keep the waiting values' records.
    const uint8_t windowed = BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW;
    check_test_expect(test, ready && node_shadow_test_fixture(directory, false, lean_types, windowed, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-facts window: the windows the front wrote are both other makings', and the kernel's records from them T's");
    check_test_expect(test, ready && node_shadow_test_fixture(directory, true, lean_types | BUSTER_NODE_FRONT_SWEEPS, windowed | BUSTER_NODE_READER_TESTS, unsigned_char,
                                                              BUSTER_ARRAY_LENGTH(unsigned_char)),
                      "node-facts window, with the tests and C and X there: the same windows");
    check_test_expect(test, ready && node_front_test_limits(directory, true, lean_types, windowed, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-facts window: a value waiting across a chunk's end keeps its records, and its expression's window is the front's");
    check_test_expect(test, ready && node_front_test_limits(directory, false, lean_types, windowed, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-facts window: the same windows where the front's stack is full");
    check_test_expect(test, ready && node_facts_test_reader_lines(directory, windowed, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-facts window: a unit of 16,500 literals has a line for each from every making");
    check_test_expect(test, ready && node_facts_test_window_room(directory, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-facts window: a value whose records the arrays have no room to keep is cut, and its expression's inputs are T's columns'");
    check_test_expect(test, ready && node_facts_test_window_wait(directory, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-facts window: a value kept through several chunks' ends keeps its records, its literal's line and its calls' arguments");
    check_test_expect(test, ready && node_facts_test_window_calls(directory, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-facts window: a cut call's arguments are not kept, and the list stays within its room");
    check_test_expect(test, ready && node_facts_test_window_again(directory, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-facts window: a value cut at a chunk's end is not whole again where the rows have room later");
    // The flat front beside the lean one, its twin: on the shadow's fixture, at the chunks' ends, where the lean front's
    // stack is full, and past its own rings' reach.
    const uint8_t flat = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_FLAT;
    check_test_expect(test, ready && node_shadow_test_fixture(directory, false, flat, 0, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-flat: the flat front's records are the lean front's on the shadow's fixture, the left tokens' places empty");
    check_test_expect(test, ready && node_shadow_test_fixture(directory, true, flat | BUSTER_NODE_FRONT_FLAT_FIRST, 0, unsigned_char, BUSTER_ARRAY_LENGTH(unsigned_char)),
                      "node-flat before the walk: the same records");
    check_test_expect(test, ready && node_front_test_builtins(directory, BUSTER_NODE_FRONT_AFTER, 38, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-front: in a unit with no_builtin the front makes the calls, each one's library mark the walk's");
    check_test_expect(test, ready && node_front_test_builtins(directory, BUSTER_NODE_FRONT_BEFORE, 38, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-front before the walk: the same calls and marks");
    check_test_expect(test, ready && node_front_test_builtins(directory, flat, 38, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-flat: in a unit with no_builtin the flat front makes the calls the lean front makes");
    check_test_expect(test, ready && node_front_test_builtins(directory, flat | BUSTER_NODE_FRONT_FLAT_FIRST, 38, quiet, BUSTER_ARRAY_LENGTH(quiet)),
                      "node-flat before the walk: the same calls");
    check_test_expect(test, ready && node_front_test_limits(directory, true, flat, 0, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-flat: a value whose consumer is in the next chunk is found there by its position");
    check_test_expect(test, ready && node_front_test_limits(directory, true, flat | BUSTER_NODE_FRONT_FLAT_FIRST, 0, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-flat before the walk: a value whose consumer is in the next chunk is found there");
    check_test_expect(test, ready && node_front_test_limits(directory, false, flat, 0, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-flat: where the lean front's stack is full the flat front makes the records it lost");
    check_test_expect(test, ready && node_flat_test_reach(directory, false, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-flat: a value past the rings' reach leaves its consumer, and the left values' consumers keep their places empty");
    check_test_expect(test, ready && node_flat_test_reach(directory, true, loud, BUSTER_ARRAY_LENGTH(loud)),
                      "node-flat before the walk: the same past the rings' reach");
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
