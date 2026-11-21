// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// measure-node-machine's checks (#2524's steps 0 and 1), on original fixtures. Each fixture's line that ends with
// `// refuse: TEST` must have an expression the census refuses by that test, and each that ends with `// take` an
// expression the census takes and none it refuses. (A `// reader: TEST` before the `// refuse: TEST` names the test that
// refuses the line at reader time, where the census after the pipeline refuses it by a warning's event too, which
// comes after reader time.) The first fixture gives one warning a line, which the compiler reports
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
// array and a returned address. Its last function is a null pointer constant's (#3012, -Wnon-literal-null-conversion):
// a zero that is no literal `0` assigned, initialized with, passed and returned as a pointer (an operator's value, an
// enumerator, a cast, a cast of a floating constant), each beside what is no warning (a literal, in parentheses too, a
// bool's zero, a pointer's); an argument and its own parameter (a zero that is no literal to an integer's parameter
// beside a literal to a pointer's is quiet, and the other way round is not; past the parameters none is asked); an
// initializer that starts with a parenthesis, whose warning's event is before its first node's token (#3017), beside
// a for statement's clauses a line each, whose first one's event is no other's; an atomic pointer assigned, whose own
// type is no pointer's where its conversion's is; and an integer constant folded from a floating one, whose record's
// payload and exponent are the floating value's (#3014): it is no small divisor by that exponent.
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
    "}\n"
    "void np(const char*);\n"
    "void n2(int, char*);\n"
    "void np2(char*, int);\n"
    "void nv(int, ...);\n"
    "char* nulls(int* q, int a)\n"
    "{\n"
    "    _Atomic(int*) ap;\n"
    "    int x = 0;\n"
    "    q = 0; // take\n"
    "    q = (0); // take\n"
    "    q = 0L; // take\n"
    "    q = (_Bool)0; // take\n"
    "    q = (void*)0; // take\n"
    "    q = 1 - 1; // refuse: constant\n"
    "    q = A0; // refuse: constant\n"
    "    q = (A0); // refuse: constant\n"
    "    q = (char)0; // refuse: constant\n"
    "    q = (int)0.0; // refuse: constant\n"
    "    q = (int)0.5; // refuse: constant\n"
    "    ap = A0; // refuse: constant\n"
    "    int* n0 = 0; // take\n"
    "    const char* lit = \"abc\"; // take\n"
    "    int* n1 = (_Bool)0; // take\n"
    "    int* n2a = 1 - 1; // refuse: constant\n"
    "    int* n3 = A0; // refuse: constant\n"
    "    int* n4 = (char)0; // refuse: constant\n"
    "    int* n5 = (char)0; // reader: constant // refuse: warning-event\n"
    "    int* n6 = (1 - 1); // reader: constant // refuse: warning-event\n"
    "    int* n7 = (A0); // reader: constant // refuse: warning-event\n"
    "    int* n8 = (int)1e-4900L; // refuse: constant\n"
    "    np(0); // take\n"
    "    np((0)); // take\n"
    "    np((_Bool)0); // take\n"
    "    np(1 - 1); // refuse: constant\n"
    "    np(A0); // refuse: constant\n"
    "    np((char)0); // refuse: constant\n"
    "    np((int)0.5); // refuse: constant\n"
    "    n2(A0, 0); // take\n"
    "    n2(0, A0); // refuse: constant\n"
    "    n2(a, (short)0); // refuse: constant\n"
    "    np2(A0, a); // refuse: constant\n"
    "    np2(0, A0); // take\n"
    "    n2(1 - 1, (char*)q); // take\n"
    "    nv(a, A0, 1 - 1); // take\n"
    "    a = (int)0.5; // take\n"
    "    a = a + (int)0.5; // take\n"
    "    a = a << (int)1e-4900L; // take\n"
    "    a = a / (int)1e-4900L; // refuse: constant\n"
    "    a = a % (int)1e-4900L; // refuse: constant\n"
    "    a /= (int)1e-4900L; // refuse: constant\n"
    "    n2((int)1e-4900L, 0); // take\n"
    "    nv((int)0.5, (int)1e-4900L); // take\n"
    "    for (x = \"a\\q\"[0]; // refuse: warning-event\n"
    "         x < a; // take\n"
    "         x++) a += 2; // take\n"
    "    if (a) return A0; // refuse: constant\n"
    "    if (a > 1) return (char)0; // refuse: constant\n"
    "    if (a > 2) return (int)0.5; // refuse: constant\n"
    "    return 0; // take\n"
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
// which nothing sends to its test then; a zero that is no literal made a pointer, in each place the warning has). Its first function's return has nothing listed and ends at the unit's first
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
    "}\n"
    "void np(int*);\n"
    "int* nul(int* q)\n"
    "{\n"
    "    q = (char)0; // take\n"
    "    int* n = 1 - 1; // take\n"
    "    np(1 - 1); // take\n"
    "    np(n); // take\n"
    "    return (short)0; // take\n"
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

// What the compiler's own test tables had and no corpus (#3201, #3202), with the default warnings. An ordered comparison
// of a pointer with an integer, by each operand that is a pointer once converted (a pointer, an array, a function), and
// the comparisons its test has nothing for: a null constant, two pointers (which the loop on the window leaves out).
// A cast of an array, a string, a function and a function's dereference to a narrower integer or an enumeration, and
// to a pointer's width. A call of a function whose earlier redeclaration has diagnose_if, with the declarations before
// and after it bare. An initializer whose declarator has a warning after its name (an attribute the reader ignores, one
// it doesn't know), an automatic object's asm label beside a register variable's, which names its register and is
// taken, and a brace that is its cast's type's. A `return` under a cleanup,
// by each scope that holds one. And a return of a sum of more operands than the address test's list once held.
static const char node_census_test_holes[] =
    "enum A { A0, A1 };\n"
    "struct L { int first; int list[4]; };\n"
    "int ga[8];\n"
    "int one(int);\n"
    "void f8(int x);\n"
    "void f8(int y) __attribute__((diagnose_if(y == 3, \"redeclared\", \"warning\")));\n"
    "void f8(int z);\n"
    "void rel(int* p);\n"
    "void vf(void);\n"
    "int at_file_scope __attribute__((nonnull)) = 5;\n"
    "int order(long a, int* p, int* e, struct L* q, int (*fp)(int))\n"
    "{\n"
    "    int x = 0;\n"
    "    enum A ea = A0;\n"
    "    x = a < p; // refuse: pointer-integer-order\n"
    "    x = p >= a; // refuse: pointer-integer-order\n"
    "    x = p <= 1; // refuse: pointer-integer-order\n"
    "    x = ga > a; // refuse: pointer-integer-order\n"
    "    x = q->list > a; // refuse: pointer-integer-order\n"
    "    x = one < a; // refuse: pointer-integer-order\n"
    "    x = a < one; // refuse: pointer-integer-order\n"
    "    x = a > ga; // refuse: pointer-integer-order\n"
    "    if (a < p) x = 1; // refuse: pointer-integer-order\n"
    "    x = p < 0; // take\n"
    "    x = p < e; // take\n"
    "    x = ga < p; // take\n"
    "    while (p < e) p++; // take\n"
    "    x = p < (int*)0; // take\n"
    "    x = a < x; // take\n"
    "    x = (int)ga; // refuse: pointer-cast-or-void-dereference\n"
    "    x = (char)q->list; // refuse: pointer-cast-or-void-dereference\n"
    "    x = (short)\"s\"; // refuse: pointer-cast-or-void-dereference\n"
    "    x = (short)one; // refuse: pointer-cast-or-void-dereference\n"
    "    x = (unsigned char)*fp; // refuse: pointer-cast-or-void-dereference\n"
    "    ea = (enum A)ga; // refuse: pointer-cast-or-void-dereference\n"
    "    a = (long)ga; // take\n"
    "    a = (long)one; // take\n"
    "    p = (int*)ga; // take\n"
    "    x = (*one)(x); // take\n"
    "    x = one(x); // take\n"
    "    (*vf)(); // take\n"
    "    f8(3); // refuse: callee\n"
    "    f8(2); // refuse: callee\n"
    "    return x + (int)ea; // take\n"
    "}\n"
    "void declared(int n)\n"
    "{\n"
    "    int a __attribute__((nonnull)) = n; // refuse: warning-event\n"
    "    int b __attribute__((unknown_attribute_of_no_compiler)) = n + 1; // refuse: warning-event\n"
    "    int d asm(\"d1\") = n + 2; // refuse: control\n"
    "    register long r asm(\"r10\") = n + 4; // take\n"
    "    static int labelled_static asm(\"ls1\") = 3;\n"
    "    enum A e = (enum { V1 })0; // take\n"
    "    int plain = n + 3; // take\n"
    "    f8(a + b + d + (int)e + plain + labelled_static + at_file_scope); // refuse: callee\n"
    "}\n"
    "int cleaned(int n)\n"
    "{\n"
    "    int c __attribute__((cleanup(rel))) = n;\n"
    "    if (n) return 1; // refuse: control\n"
    "    {\n"
    "        int inner __attribute__((cleanup(rel))) = n;\n"
    "        if (inner) return 2; // refuse: control\n"
    "    }\n"
    "    for (int i __attribute__((cleanup(rel))) = 0; i < n; i++)\n"
    "    {\n"
    "        if (i > 3) return i; // refuse: control\n"
    "    }\n"
    "    return c; // refuse: control\n"
    "}\n"
    "int before(int n)\n"
    "{\n"
    "    if (n) return 1; // take\n"
    "    {\n"
    "        int inner __attribute__((cleanup(rel))) = n;\n"
    "        n += inner; // take\n"
    "        if (n > 9) return 3; // refuse: control\n"
    "    }\n"
    "    return n; // take\n"
    "}\n"
    "struct L whole(struct L* q, int n)\n"
    "{\n"
    "    int c __attribute__((cleanup(rel))) = n;\n"
    "    return *q; // refuse: aggregate-value\n"
    "}\n"
    "long sum(long a)\n"
    "{\n"
    "    return a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + "
    "a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a; // take\n"
    "}\n";

// A segment's address space (#3205): a value of a type in one is the machine's to refuse, as any type it has no kind for,
// through a pointer, a member, a subscript and a name; an address space that is no segment's, and a pointer to a
// segment's object that nothing reads through, are taken.
static const char node_census_test_segments[] =
    "struct S { int a, b; };\n"
    "__seg_gs int count;\n"
    "int seg(__seg_gs int* p, __seg_fs struct S* s, int __attribute__((address_space(258)))* q, int __attribute__((address_space(1)))* flat, long v)\n"
    "{\n"
    "    int x = *p; // refuse: complex-bitint-atomic-vector-half\n"
    "    *p = (int)v; // refuse: complex-bitint-atomic-vector-half\n"
    "    s->b = (int)v; // refuse: complex-bitint-atomic-vector-half\n"
    "    x = p[1]; // refuse: complex-bitint-atomic-vector-half\n"
    "    x = count; // refuse: complex-bitint-atomic-vector-half\n"
    "    x = *q; // refuse: complex-bitint-atomic-vector-half\n"
    "    x = *(__seg_gs int*)v; // refuse: complex-bitint-atomic-vector-half\n"
    "    x = *flat; // take\n"
    "    p = p + 1; // take\n"
    "    v = (long)p; // take\n"
    "    return x; // take\n"
    "}\n";

// With every warning off (`-w`), the tests that a warning's policy asks refuse nothing: an ordered comparison of a
// pointer with an integer and a pointer's cast to a narrower integer are taken, in the census's code and in the
// machine's own form, and two pointers' comparison, which nothing sends, is not one the loop leaves out.
static const char node_census_test_silent[] =
    "int order(long a, int* p, int* e, char* s)\n"
    "{\n"
    "    int x = a < p; // take\n"
    "    x = (int)s; // take\n"
    "    if (p >= a) x = 2; // take\n"
    "    while (p < e) p++; // take\n"
    "    return x; // take\n"
    "}\n";

// Under the sign comparison's warning every comparison is sent to its tests whatever its operands, and two pointers'
// ordered comparison is not one the loop leaves out.
static const char node_census_test_signs[] =
    "int order(long a, int* p, int* e)\n"
    "{\n"
    "    int x = p < e; // take\n"
    "    while (p < e) p++; // take\n"
    "    x = a < p; // refuse: pointer-integer-order\n"
    "    return x; // take\n"
    "}\n";

// Two attributes whose warnings no test stands for (#3201): a unit that reads either has every expression refused, the
// function's that has the attribute and any other's.
static const char node_census_test_interrupt[] =
    "void g(void);\n"
    "__attribute__((interrupt)) void h(void* p) { g(); } // refuse: unmodeled-warning\n"
    "int w(int a, int b)\n"
    "{\n"
    "    a = a + b; // refuse: unmodeled-warning\n"
    "    return a; // refuse: unmodeled-warning\n"
    "}\n";

static const char node_census_test_noderef[] =
    "typedef int __attribute__((noderef)) Scalar;\n"
    "Scalar* p;\n"
    "void f(void) { (void)*p; } // refuse: unmodeled-warning\n"
    "int w(int a, int b)\n"
    "{\n"
    "    return a + b; // refuse: unmodeled-warning\n"
    "}\n";

// The objects' side at reader time (#3203, #3206, #3207): what T reads of an object that the reader settles after the
// bodies, each in a unit of its own (the answers are a unit's).
// - An asm label. One the asm pass keeps (memmove's; a tentative definition's redeclaration's; one spelled twice; a
//   register variable's): the builtin's call is a call, which T takes. One it ignores, on a declaration after the
//   definition (memcpy's, `object`'s): the builtin's call is computed, and T refuses it at both times.
static const char node_objects_test_labels[] =
    "typedef unsigned long size_t;\n"
    "void* memmove(void* destination, const void* source, size_t count) asm(\"moved\");\n"
    "void* memcpy(void* destination, const void* source, size_t count) { char* to = destination; const char* from = source; while (count--) *to++ = *from++; return destination; }\n"
    "void* memcpy(void* destination, const void* source, size_t count) asm(\"ignored_copy\");\n"
    "int object = 1;\n"
    "extern int object asm(\"ignored_object\");\n"
    "int tentative;\n"
    "extern int tentative asm(\"kept_tentative\");\n"
    "extern int twice asm(\"same\");\n"
    "extern int twice asm(\"same\");\n"
    "int twice = 3;\n"
    "int uses(char* to, const char* from, int n)\n"
    "{\n"
    "    register long kept asm(\"r10\") = n;\n"
    "    memcpy(to, from, 8);\n"
    "    memmove(to, from, 8);\n"
    "    return object + tentative + twice + (int)kept;\n"
    "}\n";

// - A rename pragma (with an asm label in the unit, without which the program reads no label at all: #3208). The
//   declarations it labels are chosen when the reader is done, so T refuses every library builtin's call of the unit,
//   the one before the pragma and memmove's too, and asks no label; the two objects it labels are counted apart.
static const char node_objects_test_renames[] =
    "typedef unsigned long size_t;\n"
    "void* memcpy(void* destination, const void* source, size_t count);\n"
    "void* memmove(void* destination, const void* source, size_t count);\n"
    "int plain(int n);\n"
    "extern int other asm(\"other_name\");\n"
    "int before(char* to, const char* from) { memcpy(to, from, 8); return plain(1); }\n"
    "#pragma redefine_extname memcpy renamed_copy\n"
    "int after(char* to, const char* from) { memcpy(to, from, 8); memmove(to, from, 8); return plain(2) + other; }\n"
    "extern int data;\n"
    "#pragma redefine_extname data renamed_data\n"
    "int read(void) { return data; }\n";

// - A weak pragma, which is no rename pragma: a labelled builtin's call is a call T takes, in a unit of its own (a
//   symbol pragma sends every name's line the long way).
static const char node_objects_test_pragmas[] =
    "typedef unsigned long size_t;\n"
    "void* memmove(void* destination, const void* source, size_t count) asm(\"moved\");\n"
    "extern int late;\n"
    "#pragma weak late\n"
    "int uses(char* to, const char* from) { memmove(to, from, 8); return &late != 0; }\n";

// - A function multiversioned by target strings, called: the program's own object for its versions, and its call's
//   candidates, made after the last body: T refuses every call of the unit at both times.
static const char node_objects_test_versions[] =
    "__attribute__((target(\"default\"))) int versioned(int n) { return n; }\n"
    "__attribute__((target(\"avx2\"))) int versioned(int n) { return n + 1; }\n"
    "int plain(int n);\n"
    "int calls(int n) { int a = versioned(n); int b = plain(n); return a + b; }\n"
    "int (*address(void))(int) { return versioned; }\n"
    "int unused(int n) { return n * 2; }\n";

// - The same declared and never called: the graph has no candidate at either time, and the unit's one call is refused
//   all the same, the answer being the unit's declarations'.
static const char node_objects_test_unversioned[] =
    "__attribute__((target(\"default\"))) int versioned(int n) { return n; }\n"
    "__attribute__((target(\"avx2\"))) int versioned(int n) { return n + 1; }\n"
    "int plain(int n);\n"
    "int calls(int n) { return plain(n); }\n";

// - An overloadable function: its declarations are the graph's candidates before the bodies, and at both times every
//   call of the unit is refused (at its callee's leaf: the plain function's before and after the overloaded one's
//   too; and for its callee, a call through a pointer) and every discard is left to the action.
static const char node_objects_test_overloads[] =
    "int __attribute__((overloadable)) chosen(int n);\n"
    "int __attribute__((overloadable)) chosen(long n);\n"
    "int plain(int n);\n"
    "int before(int n) { return plain(n); }\n"
    "int calls(int n) { return chosen(n); }\n"
    "int after(int n) { return plain(n) + n; }\n"
    "int through(int (*fp)(int), int n) { return fp(n); }\n"
    "int none(int n) { (void)n; return n + 1; }\n";

// - A weak name, which is the name's from a token on and not a declaration's: a function declared weak and then
//   defined; a block's weak declaration of a name whose other declaration is used before it (not weak) and after it
//   (weak: the declaration's line is made again); a weakref of a function and of an object; a plain weak
//   declaration, tested and called; and a name the unit does not note, used three times, whose line its second and
//   third use take without asking.
static const char node_objects_test_weak[] =
    "int declared(void) __attribute__((weak));\n"
    "int declared(void) { return 1; }\n"
    "int first(void) { return declared(); }\n"
    "static int hidden(void) { return 1; }\n"
    "int early(void) { return hidden(); }\n"
    "int second(void) { int hidden(void) __attribute__((weak)); return hidden(); }\n"
    "int late(void) { return hidden(); }\n"
    "static int missing(void) __attribute__((weakref(\"target\")));\n"
    "int third(void) { return missing(); }\n"
    "static int data __attribute__((weakref(\"external_data\")));\n"
    "int fourth(void) { return data; }\n"
    "extern int plain_weak(void) __attribute__((weak));\n"
    "int fifth(void) { return plain_weak ? plain_weak() : 0; }\n"
    "int plain(int n);\n"
    "int sixth(int n) { return plain(n) + plain(n + 1) + plain(n + 2); }\n";

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
            // (An expression an event refuses names the event's warning: one of its tokens' span's.)
            matched |= record->line == line && expected != NODE_CENSUS_TESTS && (record->tests >> expected & 1) != 0 &&
                       (expected != NODE_CENSUS_EVENT || record->event < CHECK_WARNING_ID_COUNT);
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
    // The ordered comparisons of two operands that are pointers once converted, which the loop's orders' rule left out.
    uint32_t orders;
    uint32_t asks;
    // The initializers' roots asked of the census's code for a null pointer constant that is no literal.
    uint32_t nulls;
    uint32_t values;
    uint32_t spared;
    uint32_t direct;
    uint32_t apart;
    // C on the window beside them (c_check_node_machine_window.c): the expressions it decides and those of them it
    // refuses for a template, each compared with C's on T's columns.
    uint32_t decided;
    uint32_t decided_refused;
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
                            (window ? BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS | BUSTER_NODE_READER_WINDOW_CONTEXTS : 0)),
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
                                                 shadow.own_root_nulls == windowed->nulls &&
                                                 shadow.own_result_casts == windowed->casts &&
                                                 shadow.own_quiet_casts == windowed->casts_left && shadow.own_quiet_casts_checked == windowed->casts_left &&
                                                 shadow.own_quiet_casts_wrong == 0 &&
                                                 shadow.own_quiet_orders == windowed->orders && shadow.own_quiet_orders_checked == windowed->orders &&
                                                 shadow.own_quiet_orders_wrong == 0 &&
                                                 shadow.own_values_checked == windowed->values && shadow.own_values_wrong == 0 &&
                                                 shadow.own_tree_checked == windowed->spared && shadow.own_tree_wrong == 0 && shadow.own_lists_past == 0 &&
                                                 shadow.own_direct == windowed->direct && shadow.own_direct_checked == windowed->direct && shadow.own_direct_wrong == 0 &&
                                                 shadow.reader_facts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES] == 0 &&
                                                 shadow.reader_facts[NODE_FACTS_COUNT_WINDOW_DIFFERENCES] == 0 &&
                                                 shadow.reader_facts[NODE_FACTS_COUNT_CONTEXTS_EXPRESSIONS] == windowed->expressions &&
                                                 shadow.reader_facts[NODE_FACTS_COUNT_CONTEXTS_DECIDED] == windowed->decided &&
                                                 shadow.reader_facts[NODE_FACTS_COUNT_CONTEXTS_REFUSED] == windowed->decided_refused &&
                                                 shadow.reader_facts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES] == 0));
    if (!valid || !same)
    {
        fprintf(stderr, "test-checker: node-tests %s%s: C on the window ran on %" PRIu64 " expressions, decided %" PRIu64 " (%u expected) and refused %" PRIu64
                " of them (%u); %" PRIu64 " differ from C on T's columns, in %" PRIu64 " records\n",
                name, window ? " (on the window)" : "", shadow.reader_facts[NODE_FACTS_COUNT_CONTEXTS_EXPRESSIONS], shadow.reader_facts[NODE_FACTS_COUNT_CONTEXTS_DECIDED],
                window ? windowed->decided : 0, shadow.reader_facts[NODE_FACTS_COUNT_CONTEXTS_REFUSED], window ? windowed->decided_refused : 0,
                shadow.reader_facts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES], shadow.reader_facts[NODE_FACTS_COUNT_CONTEXTS_RECORD_DIFFERENCES]);
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
        fprintf(stderr, "test-checker: node-tests %s%s: %" PRIu64 " initializers' roots were asked of the census's code for a null pointer constant (%u)\n", name,
                window ? " (on the window)" : "", shadow.own_root_nulls, window ? windowed->nulls : 0);
        fprintf(stderr, "test-checker: node-tests %s%s: the loop left %" PRIu64 " ordered comparisons of two pointers out (%u): %" PRIu64 " of them checked, %" PRIu64
                " with a test\n", name, window ? " (on the window)" : "", shadow.own_quiet_orders, window ? windowed->orders : 0, shadow.own_quiet_orders_checked,
                shadow.own_quiet_orders_wrong);
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
        const char* reader = NULL;
        for (const char* c = at; c + 2 < end && refuse == NULL; c += 1)
        {
            reader = reader == NULL && strncmp(c, "// reader: ", 11) == 0 ? c + 11 : reader;
            refuse = strncmp(c, "// refuse: ", 11) == 0 ? c + 11 : NULL;
        }
        // A line's `// reader: TEST` before its `// refuse: TEST` names the test that refuses it here, where the census
        // after the pipeline refuses it by a warning's event that comes after reader time: a name up to its space.
        const char* named = reader != NULL ? reader : refuse;
        uint32_t length = 0;
        while (named != NULL && named + length < end && (reader == NULL || named[length] != ' '))
        {
            length += 1;
        }
        const uint32_t expected = named != NULL ? node_census_test_named(named, length) : NODE_CENSUS_TESTS;
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

// The weak names' search (check_input_weak_name, c_check_input.c), eight records a step: lists of every length to 20,
// record k the name k + 1 from the token 10 * (k + 1) on, and a last record that makes the first name weak again
// later. A name is weak at its token and after, not the token before; a name of no record never; with no use asked,
// every record's name; and the name of a list's last record past each step's end.
static bool node_objects_test_weak_search(void)
{
    CheckInputWeakName records[21];
    bool held = true;
    for (uint32_t count = 0; count <= 20; count += 1)
    {
        for (uint32_t k = 0; k < count; k += 1)
        {
            records[k] = (CheckInputWeakName){ .name = k + 1, .token = 10 * (k + 1) };
        }
        records[count] = (CheckInputWeakName){ .name = 1, .token = 1000 };
        const CheckInput listed = { .weak_names = records, .weak_name_count = count };
        const CheckInput again = { .weak_names = records, .weak_name_count = count + 1 };
        for (uint32_t k = 0; k < count; k += 1)
        {
            held = held && check_input_weak_name(&listed, k + 1, 10 * (k + 1)) && check_input_weak_name(&listed, k + 1, 10 * (k + 1) + 5) &&
                   !check_input_weak_name(&listed, k + 1, 10 * (k + 1) - 1) && check_input_weak_name(&listed, k + 1, UINT32_MAX) &&
                   !check_input_weak_name(&listed, 10 * (k + 1), 10 * (k + 1));
        }
        held = held && !check_input_weak_name(&listed, count + 1, UINT32_MAX) && !check_input_weak_name(&listed, 0, UINT32_MAX) &&
               check_input_weak_name(&again, 1, 1000) && check_input_weak_name(&again, 1, UINT32_MAX) &&
               check_input_weak_name(&again, 1, 999) == (count != 0) && check_input_weak_name(&again, 1, 9) == false;
    }
    return held;
}

// The shadow's own comparisons of the objects' side, which no run of a fixture can make fail: an object's facts
// against the program's (node_shadow_object), the objects a rename pragma labelled (node_shadow_object_renamed), and
// a name's leaf by the names' pass's second run (node_facts_window_name_same). A row a case.
static bool node_objects_test_comparisons(void)
{
    const uint8_t declared = BUSTER_NODE_MACHINE_OBJECT_DECLARED;
    const uint8_t twice = BUSTER_NODE_MACHINE_OBJECT_TWICE;
    const uint8_t labelled = BUSTER_NODE_MACHINE_OBJECT_LABELLED;
    const uint32_t none = BUSTER_CHECK_NONE;
    static const struct
    {
        uint8_t facts;
        uint32_t reader_declaration;
        uint32_t declaration;
        bool twice;
        bool labelled;
        bool pragma;
        NodeShadowObject expected;
    } objects[] = {
        // A declared object: its declaration, its returns_twice and its label, each the program's or a difference.
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED, 7, 7, false, false, false, { .same = true } },
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED, 7, 9, false, false, false, { .same = false } },
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, false, false, false, { .same = true } },
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED, BUSTER_CHECK_NONE, 7, false, false, false, { .same = false } },
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED | BUSTER_NODE_MACHINE_OBJECT_TWICE, 7, 7, true, false, false, { .same = true } },
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED | BUSTER_NODE_MACHINE_OBJECT_TWICE, 7, 7, false, false, false, { .same = false } },
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED, 7, 7, true, false, false, { .same = false } },
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED | BUSTER_NODE_MACHINE_OBJECT_LABELLED, 7, 7, false, true, false, { .same = true } },
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED | BUSTER_NODE_MACHINE_OBJECT_LABELLED, 7, 7, false, false, false, { .same = false } },
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED, 7, 7, false, true, false, { .same = false } },
        // A label a rename pragma made, which reader time has not: counted, and no difference; its other facts still are.
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED, 7, 7, false, true, true, { .same = true, .renamed = true } },
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED, 7, 9, false, true, true, { .same = false, .renamed = true } },
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED, 7, 7, true, true, true, { .same = false, .renamed = true } },
        // A pragma that labelled the object changes nothing where reader time has the label, or the program has none.
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED | BUSTER_NODE_MACHINE_OBJECT_LABELLED, 7, 7, false, true, true, { .same = true } },
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED | BUSTER_NODE_MACHINE_OBJECT_LABELLED, 7, 7, false, false, true, { .same = false } },
        { BUSTER_NODE_MACHINE_OBJECT_DECLARED, 7, 7, false, false, true, { .same = true } },
        // The program's own object: reader time has nothing, or it differs.
        { 0, BUSTER_CHECK_NONE, 7, false, false, false, { .same = true, .own = true } },
        { 0, BUSTER_CHECK_NONE, 7, true, true, true, { .same = true, .own = true } },
        { 0, 7, 7, false, false, false, { .same = false, .own = true } },
        { BUSTER_NODE_MACHINE_OBJECT_TWICE, BUSTER_CHECK_NONE, 7, true, false, false, { .same = false, .own = true } },
        { BUSTER_NODE_MACHINE_OBJECT_LABELLED, BUSTER_CHECK_NONE, 7, false, true, false, { .same = false, .own = true } },
        // An object neither has: nothing to compare.
        { 0, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, false, false, false, { .same = true } },
    };
    bool held = (declared | twice | labelled) != 0 && none == BUSTER_CHECK_NONE;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(objects); i += 1)
    {
        const NodeShadowObject made = node_shadow_object(objects[i].facts, objects[i].reader_declaration, objects[i].declaration, objects[i].twice, objects[i].labelled,
                                                         objects[i].pragma);
        const bool row = made.same == objects[i].expected.same && made.own == objects[i].expected.own && made.renamed == objects[i].expected.renamed;
        if (!row)
        {
            fprintf(stderr, "test-checker: node-objects: the objects' comparison's row %u: same %d (%d expected), own %d (%d), renamed %d (%d)\n", i, made.same,
                    objects[i].expected.same, made.own, objects[i].expected.own, made.renamed, objects[i].expected.renamed);
        }
        held = held && row;
    }
    // The pragmas' records: the object of token 2 a rename pragma labelled; token 3's named by a weak pragma, token 4's
    // by a rename the reader did not take, token 5's by one with a warning, and one that labelled nothing.
    CheckInputSymbolPragma records[] = {
        { .kind = CHECK_SYMBOL_PRAGMA_WEAK, .supported = true, .declaration = 3 },
        { .kind = CHECK_SYMBOL_PRAGMA_REDEFINE, .supported = false, .declaration = 4 },
        { .kind = CHECK_SYMBOL_PRAGMA_REDEFINE, .supported = true, .warning = CHECK_SYMBOL_WARNING_INTERNAL, .declaration = 5 },
        { .kind = CHECK_SYMBOL_PRAGMA_REDEFINE, .supported = true, .declaration = BUSTER_CHECK_NONE },
        { .kind = CHECK_SYMBOL_PRAGMA_REDEFINE, .supported = true, .declaration = 2 },
    };
    CheckInputSymbolPragmas pragmas = { .records = records, .count = BUSTER_ARRAY_LENGTH(records) };
    uint32_t token_objects[] = { 0, 0, 12, 13, 14, 15, 16 };
    const CheckInput input = { .symbol_pragmas = &pragmas, .objects = token_objects, .token_count = BUSTER_ARRAY_LENGTH(token_objects) };
    const CheckInput without = { .objects = token_objects, .token_count = BUSTER_ARRAY_LENGTH(token_objects) };
    held = held && node_shadow_object_renamed(&input, 12) && !node_shadow_object_renamed(&input, 13) && !node_shadow_object_renamed(&input, 14) &&
           !node_shadow_object_renamed(&input, 15) && !node_shadow_object_renamed(&input, 16) && !node_shadow_object_renamed(&input, 0) &&
           !node_shadow_object_renamed(&without, 12);
    // The lines: 1 and 2 with the same words, 3 with others.
    uint64_t lines[16] = { 0 };
    lines[4] = lines[8] = 5;
    lines[7] = lines[11] = 9;
    lines[12] = 5;
    lines[15] = 10;
    const uint8_t name = BUSTER_NODE_FACTS_IDENTIFIER;
    const uint8_t literal = BUSTER_NODE_FACTS_LITERAL;
    held = held && node_facts_window_name_same(lines, name, name, 1, 1) && node_facts_window_name_same(lines, name, name, 1, 2) &&
           !node_facts_window_name_same(lines, name, name, 1, 3) && !node_facts_window_name_same(lines, name, literal, 1, 1) &&
           !node_facts_window_name_same(lines, name, name, 1, 0) && !node_facts_window_name_same(lines, name, name, 0, 2) &&
           node_facts_window_name_same(lines, literal, literal, 0, 0) && !node_facts_window_name_same(lines, literal, literal, 1, 2);
    return held;
}

// What the objects' side counts on a fixture, which no verdict shows: the declared objects the shadow compared, the
// program's own (which no declaration declares) and those a rename pragma labelled; the expressions T took at reader
// time and those it refused for their callee; the library builtins' calls it asked about there; and the declarations'
// lines the kernel made, with those made again for a declaration that had one and the uses that asked for a line the
// long way (a use of a declaration whose line is settled asks nothing: one load and one test).
typedef struct NodeObjectsCounted NodeObjectsCounted;
struct NodeObjectsCounted
{
    uint32_t objects;
    uint32_t own;
    uint32_t renamed;
    uint32_t taken;
    uint32_t callees;
    uint32_t library;
    uint32_t lines;
    uint32_t again;
    uint32_t asks;
};

// The whole shadow at reader time on fixture `text`, written to `directory` as `name` and read with `options`, as
// `measure-node-machine --shadow --window-rows --passes 0` runs it: nothing it compares differs from the pipeline's
// (node_shadow_differs: T's verdicts and records, the objects' facts, the library calls' answers, the kernel's records,
// the tests, C, X and the summaries), and what it counts is `counted`'s.
static bool node_objects_test_fixture(const char* directory, const char* name, const char* text, char** options, uint32_t option_count, NodeObjectsCounted counted)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    bool valid = parse_test_write(path, text, strlen(text));
    NodeShadow shadow = {
        .front = (uint8_t)(BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | BUSTER_NODE_FRONT_LEAN | BUSTER_NODE_FRONT_AFTER),
        .reader = (uint16_t)(BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS |
                             BUSTER_NODE_READER_WINDOW_CONTEXTS | BUSTER_NODE_READER_WINDOW_ROWS),
        .passes = 0,
        .repeat = 1,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    const bool same = valid && !node_shadow_differs(&shadow) && shadow.typed_compared != 0 && shadow.typed_unrun == 0;
    const bool counts = shadow.typed_objects == counted.objects && shadow.typed_objects_own == counted.own && shadow.typed_objects_renamed == counted.renamed &&
                        shadow.typed_refused[NODE_MACHINE_TAKEN] == counted.taken && shadow.typed_refused[NODE_MACHINE_REFUSED_CALLEE] == counted.callees &&
                        shadow.typed_library_calls == counted.library && shadow.reader_facts[NODE_FACTS_COUNT_DECLARATION_LINES] == counted.lines &&
                        shadow.reader_facts[NODE_FACTS_COUNT_LINE_VERSIONS] == counted.again && shadow.reader_facts[NODE_FACTS_COUNT_LINE_ASKS] == counted.asks;
    if (!same || !counts)
    {
        fprintf(stderr, "test-checker: node-objects %s: %s, %s: %" PRIu64 " objects compared (%u expected) with %" PRIu64 " differences, %" PRIu64
                " the program's own (%u), %" PRIu64 " a pragma labelled (%u); T took %" PRIu64 " expressions there (%u) and refused %" PRIu64
                " for their callee (%u), %" PRIu64 " differ from the pipeline's; it asked %" PRIu64 " library calls (%u), %" PRIu64 " differ; the kernel made %"
                PRIu64 " declarations' lines (%u), %" PRIu64 " of them again (%u), for %" PRIu64 " uses that asked the long way (%u), %" PRIu64 " records differ, %" PRIu64
                " names' leaves by the pass's second run\n",
                name, valid ? "read" : "NOT READ", same ? "nothing differs" : "SOMETHING DIFFERS", shadow.typed_objects, counted.objects, shadow.typed_object_differences,
                shadow.typed_objects_own, counted.own, shadow.typed_objects_renamed, counted.renamed, shadow.typed_refused[NODE_MACHINE_TAKEN], counted.taken,
                shadow.typed_refused[NODE_MACHINE_REFUSED_CALLEE], counted.callees, shadow.typed_differences, shadow.typed_library_calls, counted.library,
                shadow.typed_library_differences, shadow.reader_facts[NODE_FACTS_COUNT_DECLARATION_LINES], counted.lines,
                shadow.reader_facts[NODE_FACTS_COUNT_LINE_VERSIONS], counted.again, shadow.reader_facts[NODE_FACTS_COUNT_LINE_ASKS], counted.asks,
                shadow.reader_facts[NODE_FACTS_COUNT_DIFFERENCES],
                shadow.front_window_name_differences);
    }
    unlink(path);
    return same && counts;
}

// C on the window's fixture (#2850's step 2, c_check_node_machine_window.c; docs/one-core-path.md, "C on the window"):
// each of its loop's rules on both sides, and each kind of expression it lists.
// - Names: a parameter, a local, a global, a static local and a thread-local one (whose flags don't say it is a
//   global), read, addressed and stored to by name; a global array, an address constant.
// - Stores through a pointer, to a member and to an element; of an aggregate and to an atomic object (no template:
//   refused).
// - Folded constants over their operands; a long double's and a 128-bit integer's, which no template takes (refused),
//   beside a double's.
// - A member of a value and through a pointer; a pointer's sum, with an array's, a variable-length array's, an
//   incomplete array's and a function's decay for its pointer; an integer's with the pointer second and two pointers'
//   difference (refused); a stride its type's row doesn't hold, asked of the type table (an element past 4 GB), and
//   one that is no constant (a variable-length array's: refused).
// - Two numbers' operators of each class and a comparison of pointers; long double's, a 128-bit integer's, a _BitInt's
//   and a complex operand's (refused); truths, of a long double's wrapper and of a __float128 (no class a truth tests:
//   refused); wrappers, of long double too; a return's conversion, of an aggregate too (the first function's: T's
//   record of an aggregate has an address's class, so its wrapper passes).
// - What an operand's own class doesn't say, read from T's two conversions a record at a time: a 128-bit integer
//   beside a double or a float is converted to it (taken), two of them and a long double beside a double are not
//   (refused). A subscript whose first operand is the index has no template by its operands' own types.
// - A cast to an atomic type: an rvalue of an atomic type is read like any other, and the cast's operand's target is the
//   cast's own type, where T's conversion is the value type. A wrapper of a half-precision class, a subscript that
//   reads a long double, a complex value through a pointer and a vector's element have no template (refused).
// - A long double read for its address through a subscript, a dereference and a member (taken: the place asks for no
//   class), and read for its value through the member (refused); a complex object's name, a folded constant of a
//   _BitInt's type and a long double's negation, each under a cast to void that passes (refused for them alone).
// - Four sums of seventy operands: what is before one is more than two blocks from what is after, so a leaf's
//   consumer, a `+`'s and a `=`'s first operand (a name's, a pointer's, an array's that decays) and a place's consumer
//   are read by loads.
// - What it lists: a bit-field's read (its value isn't the columns' whole), a call, a logical operator that is a
//   value, a conditional, the updates, a comma, a division and a remainder by a constant, and a cast to a union.
// - What the masks carry from a block to the block before it: 64 statements of seven records each, whose first
//   operands are read for their addresses (a member's value, a store's destination), and 64 of five, `&*p`, whose
//   operand has no target. An odd count of records a statement (its discard is one) puts a block's end between every
//   two of a statement's records in turn.
// - What is not evaluated: a folded comparison's operand that is itself a constant no template takes (a 128-bit
//   integer's), with a constant below it; and a sum of seventy such constants, each with no template, which only the
//   range the comparison's operand takes out, over more than two blocks, keeps from refusing the expression.
// - A shift, whose two operands' conversions differ, and one by a 128-bit count (refused: the count's conversion is its
//   own); a pointer's sum with a 128-bit index; a condition of a __float128, whose root alone has no template.
// - Under a cast to void that passes: a long double's member and a long double's name, read (refused for them alone),
//   and a union's name (taken: only a cast to a union lists).
// - A condition's branches (a statement's CONDITION over `&&`, `||` and `!`):
//   - each operator under each, and a loop's three conditions (taken);
//   - tested values of each class, a long double's and a 128-bit integer's wrapper among them, a constant second, and
//     a `!` that is a folded constant under a branched operator (a constant's template);
//   - operands no truth tests or with no template of their own: a __float128 on each side and under a branched `!`
//     (its record has a template: refused for the operator's alone), a long double's name, an atomic's read, a
//     vector's element, a complex value and a _BitInt (refused), and a bit-field through a pointer and of an object
//     (listed for its value, the second with its object's address for a constant);
//   - a first operand that is an object's lvalue, whose truth is not known: a global's, a static's, an element's, a
//     thread's, an array's that decays, a local array's and a string's (taken);
//   - a first operand whose truth is known: an integer's, an enumerator's, an address's, a function's, a floating
//     constant's, a complex one's and an lvalue at an absolute address, which has no object; and an `||` an array's
//     decay folds, alone and under a branched `&&` (listed);
//   - two levels away: a CONDITION over a `!` over `&&` (listed), a `!` under a branched `!` and a `!` over an `&&`
//     that is a value, and plain truths beside them;
//   - `&&` and `||` as values: stored, under a comparison, a call, a cast and a switch, and with a conditional or a
//     comma for an operand (each taken, or listed for one of C's terms on its consumer's conversion or its operands);
//   - a CONDITION over the operator whose code is before `&&`'s (a truth's template);
//   - past two blocks: a tested value's consumer, a `!`'s and an `&&`'s, and a first operand that is not known, that
//     is, that is an object's lvalue, one at an absolute address and an address;
//   - six statements 64 times each, a block's end between every two of their records in turn: four taken (a global
//     first with a `!` second, `&&` under `||`, a `!` first, a `!` over a `!`) and two listed (a constant first, a `!`
//     over `&&`).
// - An operator's value and a comma (`valued`), each of C's terms for them and each row X writes for them: commas
//   discarded and not, taken, refused (a void one, after an aggregate's store) and listed (a comma's second operand
//   that is a comma, a folded one, one read for its address, after a bit-field's store); conditionals with each arm's
//   conversion, a condition of each kind, nested, with calls, and listed for a known condition, for one operand twice
//   and for what its consumer puts into its arms; `&&` and `||` as values with each operand's class, each operator
//   for an operand, runs of `!`s and of chained operators of each length; operands and consumers past two blocks;
//   and six statements 64 times each, a block's end between every two of their records in turn.
#define BUSTER_NODE_CONTEXTS_TEST_TEN "a + a + a + a + a + a + a + a + a + a"
#define BUSTER_NODE_CONTEXTS_TEST_ONES "1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1"
#define BUSTER_NODE_CONTEXTS_TEST_EIGHT(text) text text text text text text text text
#define BUSTER_NODE_CONTEXTS_TEST_BLOCK(text) BUSTER_NODE_CONTEXTS_TEST_EIGHT(BUSTER_NODE_CONTEXTS_TEST_EIGHT(text))
#define BUSTER_NODE_CONTEXTS_TEST_SUM                                                                                                                              \
    "(" BUSTER_NODE_CONTEXTS_TEST_TEN " + " BUSTER_NODE_CONTEXTS_TEST_TEN " + " BUSTER_NODE_CONTEXTS_TEST_TEN " + " BUSTER_NODE_CONTEXTS_TEST_TEN " + "             \
    BUSTER_NODE_CONTEXTS_TEST_TEN " + " BUSTER_NODE_CONTEXTS_TEST_TEN " + " BUSTER_NODE_CONTEXTS_TEST_TEN ")"
static const char node_contexts_test_source[] =
    "struct pair { int first; long second; unsigned bits : 3; };\n"
    "struct outer { struct pair inner; struct pair* link; int list[4]; };\n"
    "union both { int whole; float part; };\n"
    "struct wide { long double big; int small; };\n"
    "extern int global;\n"
    "static long file_scope;\n"
    "_Thread_local int thread_scope;\n"
    "_Atomic int shared;\n"
    "extern int table[4];\n"
    "extern int open_list[];\n"
    "_Complex double zed;\n"
    "typedef int four __attribute__((vector_size(16)));\n"
    "typedef int spaced __attribute__((aligned(16)));\n"
    "typedef float fourf __attribute__((ext_vector_type(4)));\n"
    "typedef union { int* ip; long* lp; long whole; } either __attribute__((transparent_union));\n"
    "enum shade { RED, GREEN };\n"
    "int callee(int a, long b);\n"
    "void fi(int);\n"
    "void fl(long);\n"
    "void fd(double);\n"
    "void fp(const char*);\n"
    "void fv(void*);\n"
    "void fst(struct pair);\n"
    "void fld(long double);\n"
    "void ffour(four);\n"
    "void fe(enum shade);\n"
    "void fb(_Bool);\n"
    "void ff(float);\n"
    "void fip(int*);\n"
    "void fcp(const int*);\n"
    "void ffn(void (*)(int));\n"
    "void fat(_Atomic int);\n"
    "void fvol(volatile int*);\n"
    "void fev(fourf);\n"
    "void fbit(_BitInt(24));\n"
    "void fz(_Complex double);\n"
    "void fcvv(const volatile void*);\n"
    "int* hop(int);\n"
    "void ft(either);\n"
    "four make_four(void);\n"
    "const int cfi(void);\n"
    "int vf(int, ...);\n"
    "int more(int, int, int, int, int, int);\n"
    "int printf(const char*, ...);\n"
    "extern void (*fptr)(int);\n"
    "__int128 big(int);\n"
    "void none(void);\n"
    "struct outer whole(struct outer* o)\n"
    "{\n"
    "    return *o;\n"
    "}\n"
    "int contexts(int a, long b, int* p, struct outer* o, struct outer v, int n, double d, char c, int (*q)[n], long double e, float f, char (*h)[4294967296],\n"
    "             _BitInt(24) bi, __int128 w, __float128 q128, union both un, long double* lds, _Complex double* zp, four vec,\n"
    "             struct wide* wp, four other, enum shade sh, fourf ev)\n"
    "{\n"
    "    int local = a;\n"
    "    long wide = a;\n"
    "    static int still;\n"
    "    volatile int vol = a;\n"
    "    register int reg = a;\n"
    "    register struct pair regs = v.inner;\n"
    "    register int* regp = p;\n"
    "    int* volatile vptr = p;\n"
    "    const int fixed = 3;\n"
    "    int arr[4];\n"
    "    spaced apart = a;\n"
    "    volatile struct pair vsp;\n"
    "    volatile int va[4];\n"
    "    int* const kept = p;\n"
    "    short shorts[2];\n"
    "    const struct outer* co = o;\n"
    "    local = a;\n"
    "    global = a;\n"
    "    still = a;\n"
    "    thread_scope = a;\n"
    "    file_scope = b;\n"
    "    a = global + still + thread_scope;\n"
    "    p = &local;\n"
    "    p = &global;\n"
    "    *p = a;\n"
    "    o->inner.first = a;\n"
    "    v.inner.second = b;\n"
    "    p[1] = a;\n"
    "    a = 1 + 2 * 3;\n"
    "    a = -1;\n"
    "    b = (long)4;\n"
    "    d = 1.5 + 2.0;\n"
    "    a = v.inner.first;\n"
    "    b = o->inner.second;\n"
    "    a = o->link->first;\n"
    "    a = o->inner.bits;\n"
    "    a = v.list[2];\n"
    "    p = p + 1;\n"
    "    p = 2 + p;\n"
    "    p = p - a;\n"
    "    b = p - o->list;\n"
    "    a = a * n + local / n - a % n;\n"
    "    a = a << 2 | a >> 1;\n"
    "    b = a + b;\n"
    "    d = a + d;\n"
    "    d = d * c;\n"
    "    a = a < b;\n"
    "    a = p == o->list;\n"
    "    a = !a;\n"
    "    a = -a + ~a;\n"
    "    a = p[a];\n"
    "    a = o->list[a];\n"
    "    (void)(local = a);\n"
    "    a = (int)(char)b;\n"
    "    d = (double)a;\n"
    "    wide = wide + local;\n"
    "    v.inner = o->inner;\n"
    "    p = q[1];\n"
    "    a = shared;\n"
    "    e = e + a;\n"
    "    f = f + a;\n"
    "    p = &v.list[1];\n"
    "    a = *p + *o->list;\n"
    "    a = \"abc\"[1];\n"
    "    p = (void*)0;\n"
    "    a = p != 0;\n"
    "    (void)a;\n"
    "    if (a) { local = 1; }\n"
    "    while (!p) { p = &local; }\n"
    "    if (a < n) { a = n; }\n"
    "    a = callee(a, b);\n"
    "    a = a && n;\n"
    "    a = a ? n : 1;\n"
    "    a++;\n"
    "    a += n;\n"
    "    a = (local, n);\n"
    "    a = a / 4;\n"
    "    a = a % 8;\n"
    "    a = b + " BUSTER_NODE_CONTEXTS_TEST_SUM ";\n"
    "    p = p + " BUSTER_NODE_CONTEXTS_TEST_SUM ";\n"
    "    *p = " BUSTER_NODE_CONTEXTS_TEST_SUM ";\n"
    "    q = q + 1;\n"
    "    h = h + 1;\n"
    "    p = v.list + 1;\n"
    "    p = 1 + v.list;\n"
    "    p = *q + 1;\n"
    "    p = open_list + a;\n"
    "    a = (callee + a) != 0;\n"
    "    p = table;\n"
    "    a = table[1];\n"
    "    a = !q128;\n"
    "    a = !(long double)b;\n"
    "    (void)2.5L;\n"
    "    (void)(__int128)5;\n"
    "    (void)2.5;\n"
    "    a = (int)(long double)b;\n"
    "    bi = bi + 1;\n"
    "    w = w + 1;\n"
    "    a = zed == d;\n"
    "    p = (int*)&shared;\n"
    "    shared = a;\n"
    "    d = 1.0 / 0.0;\n"
    "    un = (union both)a;\n"
    "    b = (_Atomic long)a + 1;\n"
    "    a = (int)(_Float16)d;\n"
    "    d = (__int128)b + d;\n"
    "    a = f < (__int128)b;\n"
    "    a = (__int128)b < (__int128)a;\n"
    "    a = (long double)b < d;\n"
    "    a = 1[p];\n"
    "    e = lds[1];\n"
    "    (void)*zp;\n"
    "    a = vec[1];\n"
    "    p = table + " BUSTER_NODE_CONTEXTS_TEST_SUM ";\n"
    "    lds = &lds[1];\n"
    "    lds = &*lds;\n"
    "    lds = &wp->big;\n"
    "    e = wp->big;\n"
    "    (void)zed;\n"
    "    (void)(_BitInt(24))5;\n"
    "    (void)-(long double)b;\n"
    "    (void)wp->big;\n"
    "    (void)e;\n"
    "    (void)un;\n"
    "    a = a << (__int128)b;\n"
    "    b = b << a;\n"
    "    a = (__int128)1 < 2;\n"
    "    a = (__int128)" BUSTER_NODE_CONTEXTS_TEST_ONES " + " BUSTER_NODE_CONTEXTS_TEST_ONES " + " BUSTER_NODE_CONTEXTS_TEST_ONES " + " BUSTER_NODE_CONTEXTS_TEST_ONES " + "
    BUSTER_NODE_CONTEXTS_TEST_ONES " + " BUSTER_NODE_CONTEXTS_TEST_ONES " + " BUSTER_NODE_CONTEXTS_TEST_ONES " < 2;\n"
    "    p = p + (__int128)a;\n"
    "    if (q128) { local = 2; }\n"
    // A condition's branches: `&&`, `||` and `!` under a statement's CONDITION, each under each.
    "    if (a && n) { local = 1; }\n"
    "    if (a || n) { local = 1; }\n"
    "    if (!a && n) { local = 1; }\n"
    "    if (a || !n) { local = 1; }\n"
    "    if (a && n && local) { local = 1; }\n"
    "    if (a || (n && local)) { local = 1; }\n"
    "    if ((a || n) && local) { local = 1; }\n"
    "    if (!(a && n) || local) { local = 1; }\n"
    "    if (!(a || n) && !(local && a)) { local = 1; }\n"
    "    while (a && n) { a = a - 1; }\n"
    "    do { a = a - 1; } while (a || n);\n"
    "    for (local = 0; local < a && n; local = local + 1) { }\n"
    // Tested values of each class; a long double's and a 128-bit integer's wrapper; a constant; a `!` that is a folded constant.
    "    if (b && d) { local = 1; }\n"
    "    if (p && c) { local = 1; }\n"
    "    if (f && sh) { local = 1; }\n"
    "    if (p && *p) { local = 1; }\n"
    "    if (o->link && o->link->first) { local = 1; }\n"
    "    if (a < n || b >= wide) { local = 1; }\n"
    "    if (callee(a, b) && a) { local = 1; }\n"
    "    if (a++ && n--) { local = 1; }\n"
    "    if ((long double)a && n) { local = 1; }\n"
    "    if (a && (__int128)b) { local = 1; }\n"
    "    if (a && !(long double)b) { local = 1; }\n"
    "    if (a && 1) { local = 1; }\n"
    "    if (a && !1) { local = 1; }\n"
    // Operands of a class no truth tests, and ones with no template of their own.
    "    if (e && a) { local = 1; }\n"
    "    if (a && q128) { local = 1; }\n"
    "    if (q128 && a) { local = 1; }\n"
    "    if (a && !q128) { local = 1; }\n"
    "    if (shared && a) { local = 1; }\n"
    "    if (vec[1] && a) { local = 1; }\n"
    "    if (a && !zed) { local = 1; }\n"
    "    if (bi && a) { local = 1; }\n"
    "    if (o->inner.bits && a) { local = 1; }\n"
    "    if (v.inner.bits && a) { local = 1; }\n"
    // A first operand that is an object's lvalue: its truth is not known.
    "    if (global && a) { local = 1; }\n"
    "    if (still && a) { local = 1; }\n"
    "    if (table[1] && a) { local = 1; }\n"
    "    if (file_scope || a) { local = 1; }\n"
    "    if (thread_scope && a) { local = 1; }\n"
    "    if (table && a) { local = 1; }\n"
    "    if (arr || a) { local = 1; }\n"
    "    if (\"s\" && a) { local = 1; }\n"
    // A first operand whose truth is known, or an operator a decay folds.
    "    if (1 && a) { local = 1; }\n"
    "    if (0 || a) { local = 1; }\n"
    "    if (RED && a) { local = 1; }\n"
    "    if (&global && a) { local = 1; }\n"
    "    if (callee && a) { local = 1; }\n"
    "    if (2.5 && a) { local = 1; }\n"
    "    if (*(int*)0 && a) { local = 1; }\n"
    "    if (0.0i || a) { local = 1; }\n"
    "    if (table || a) { local = 1; }\n"
    "    if (a && (table || n)) { local = 1; }\n"
    "    if (\"s\" || a) { local = 1; }\n"
    // Two levels away, and a plain truth beside them.
    "    if (!(a && n)) { local = 1; }\n"
    "    if (!!a && n) { local = 1; }\n"
    "    if (a && !!n) { local = 1; }\n"
    "    if (!!(a && n)) { local = 1; }\n"
    "    if (!a) { local = 1; }\n"
    "    if (!!a) { local = 1; }\n"
    // Values.
    "    a = a || n;\n"
    "    a = !(a && n);\n"
    "    a = !a && n;\n"
    "    a = (a && n) || local;\n"
    "    fi(a && n);\n"
    "    if ((a && n) == local) { local = 1; }\n"
    "    if (a ? n : local) { local = 1; }\n"
    "    if (a && (n ? local : a)) { local = 1; }\n"
    "    if ((a ? n : local) && a) { local = 1; }\n"
    "    if ((a, n) && local) { local = 1; }\n"
    "    if (a && (n, local)) { local = 1; }\n"
    "    if ((_Bool)(a && n)) { local = 1; }\n"
    "    if (a && (local = n && a)) { local = 1; }\n"
    "    switch (a && n) { default: local = 1; }\n"
    // The operator whose code is before `&&`'s.
    "    if (a ^ n) { local = 1; }\n"
    // Past two blocks.
    "    if (a && " BUSTER_NODE_CONTEXTS_TEST_SUM ") { local = 1; }\n"
    "    if (!a && " BUSTER_NODE_CONTEXTS_TEST_SUM ") { local = 1; }\n"
    "    if (global && " BUSTER_NODE_CONTEXTS_TEST_SUM ") { local = 1; }\n"
    "    if (1 && " BUSTER_NODE_CONTEXTS_TEST_SUM ") { local = 1; }\n"
    "    if ((a && n) || " BUSTER_NODE_CONTEXTS_TEST_SUM ") { local = 1; }\n"
    "    if (*(int*)0 && " BUSTER_NODE_CONTEXTS_TEST_SUM ") { local = 1; }\n"
    "    if (&global && " BUSTER_NODE_CONTEXTS_TEST_SUM ") { local = 1; }\n"
    // A call. Plain ones, each of the kernel's argument rule's cases: the parameter's type; an arithmetic conversion; an
    // integer constant's, a null pointer constant's and a floating constant's; a pointer to another pointer type; an
    // array's; two arguments; a call's value as an operand and as an argument; a call through a pointer, whose callee is
    // evaluated, and through one dereferenced; past the parameters, the promotions and an array; more than four
    // arguments; none; an integer constant to an enumeration.
    "    fi(a);\n"
    "    fi(c);\n"
    "    fd(a);\n"
    "    fl(1);\n"
    "    fp(0);\n"
    "    fd(2.5);\n"
    "    fv(p);\n"
    "    fp(\"text\");\n"
    "    callee(a, b);\n"
    "    a = callee(a, b) + callee(b, a);\n"
    "    fi(callee(a, b));\n"
    "    fptr(a);\n"
    "    (*fptr)(a);\n"
    "    printf(\"%d %f\", c, f);\n"
    "    printf(\"%p\", table);\n"
    "    a = more(a, a, a, a, a, a);\n"
    "    none();\n"
    "    fe(RED);\n"
    // The rule's other terms, one a call. To bool: an arithmetic value's, and a scalar's that is no number's. Between
    // floating types, with no constant and with one; an integer constant to a floating type. An lvalue the operand's
    // conversion reads with a flag of its own (a register's), one of a const object. A pointer of the parameter's
    // type, one of another type the parameter takes, an array's of each, a function's; a null pointer constant as an
    // integer's zero and as a pointer with its flag. An lvalue's arithmetic conversions, both ways; an integer's to an
    // enumeration, an enumerator's to an integer; a 128-bit integer's and a _BitInt's.
    "    fb(a);\n"
    "    fb(p);\n"
    "    fb(d);\n"
    "    ff(d);\n"
    "    ff(2.5f);\n"
    "    fd(1);\n"
    "    fd(f);\n"
    "    fi(reg);\n"
    "    fi(fixed);\n"
    "    fip(p);\n"
    "    fcp(p);\n"
    "    fip(arr);\n"
    "    fcp(arr);\n"
    "    fv(arr);\n"
    "    ffn(fi);\n"
    "    fv(0);\n"
    "    fip(0);\n"
    "    fip((void*)0);\n"
    "    fl(c);\n"
    "    fi(b);\n"
    "    fe(a);\n"
    "    fi(RED);\n"
    "    fi(w);\n"
    "    fi(bi);\n"
    // Past the parameters: a type that stays, a promotion, float to double, a pointer, an array's, an integer and a
    // floating constant.
    "    vf(a, a);\n"
    "    vf(a, c);\n"
    "    vf(a, f);\n"
    "    vf(a, d);\n"
    "    vf(a, p);\n"
    "    vf(a, arr);\n"
    "    vf(a, 1);\n"
    "    vf(a, 2.5);\n"
    // What the faults' first round found no call for. A long double's constant, both ways; an array with no bound and
    // one of a variable bound; a const pointer's value; a null pointer constant as a pointer with its flag to a
    // function's pointer (below, a void pointer with none, which that parameter doesn't take); two arguments of one
    // key past the parameters, and two of one type that differ in their standing; and a call deep in its expression,
    // which the rule makes.
    "    fld(2.5);\n"
    "    fd(2.5L);\n"
    "    fip(open_list);\n"
    "    fip(*q);\n"
    "    fip(kept);\n"
    "    ffn((void*)0);\n"
    "    vf(a, d, d);\n"
    "    vf(a, a, 1);\n"
    "    fi(a + (c * (a - (c + (a * (c - a))))));\n"
    "    fbit(fixed);\n"
    // And the second round: an address constant to an integer's type; an array whose pointer type is nowhere else; a
    // call that is not plain though each of its records has a template.
    "    fl((long)&still);\n"
    "    fv(shorts);\n"
    "    fld(d);\n"
    // The third: a const object's value past the parameters, and calls deep in their expression (each is over a
    // dereference of the one before, an operator whose code is after a call's), which the rule makes.
    "    vf(a, fixed);\n"
    "    fi(*hop(*hop(*hop(*hop(*hop(a))))));\n"
    // The fourth: a const struct's member array past the parameters, whose pointer is to a const element; and a
    // callee with an offset, which is evaluated and lists nothing.
    "    vf(a, co->list);\n"
    "    (fi + 1)(a);\n"
    // Calls C refuses: a long double's parameter, a 128-bit integer's result.
    "    fld(e);\n"
    "    a = big(a) < 2;\n"
    // Calls that list their expression: an argument whose conversion the kernel leaves to T (a struct by value, a
    // vector, an enumeration's lvalue), and a callee that is no name's leaf and has an object.
    "    fst(v.inner);\n"
    "    ffour(vec);\n"
    "    fe(sh);\n"
    "    (*fi)(a);\n"
    // And the other arguments the kernel keeps and doesn't convert: a volatile object's value, an enumeration's and an
    // atomic object's to an integer, and one of a type with a wrapper; a vector of the other kind, a volatile struct's
    // member, a void pointer to a function's pointer, a vector's rvalue with no flag its conversion drops, a volatile
    // array, a scalar to a vector; past the parameters an enumeration's, a struct's (an lvalue and an rvalue), a
    // vector's, a value of a qualified type, a volatile array's and a volatile object's; a vector with the one
    // inherited flag its conversion keeps of a constant (a GNU constant's), and an enumeration's rvalue past the
    // parameters. One it converts to a type that is no plain scalar's, which C refuses. An argument whose record is
    // not the kernel's. (An atomic type's rvalue is no argument: the compiler rejects it.)
    "    fi(vol);\n"
    "    fi(sh);\n"
    "    fi(shared);\n"
    "    fi(apart);\n"
    "    fev(ev);\n"
    "    fi(vsp.first);\n"
    "    ffn((void*)&still);\n"
    "    ffour(make_four());\n"
    "    vf(a, whole(o));\n"
    "    fvol(va);\n"
    "    vf(a, cfi());\n"
    "    fev(f);\n"
    "    vf(a, va);\n"
    "    vf(a, vol);\n"
    "    fcvv(va);\n"
    "    vf(a, sh);\n"
    "    vf(a, v.inner);\n"
    "    vf(a, vec);\n"
    "    vf(a, e);\n"
    "    ffour(vec + (int)1e100);\n"
    "    vf(a, (enum shade)a);\n"
    "    fi(o->inner.bits);\n"
    // Calls the kernel leaves to T, whose record its arguments' conversions change: a floating constant to an integer's
    // type (out of its range it folds with two inherited flags), a vector of either kind with a flag its conversion
    // drops, a complex number, a _BitInt's constant; one to an atomic parameter, which has no target; one to a
    // transparent union, whose member is the action's choice, of a pointer and of a number; a scalar with such a flag
    // to a vector, and a vector with one past the parameters; an integer constant zero of a type with no width here
    // to a pointer; an integer constant to a _BitInt; a number to a complex parameter; and a floating constant of a
    // type that is not float, double or long double, to a parameter and past them.
    "    fi(2.5);\n"
    "    fi(1e100);\n"
    "    ffour(vec = other);\n"
    "    fd(zed);\n"
    "    fat(a);\n"
    "    fev(ev = ev);\n"
    "    fi((_BitInt(24))5);\n"
    "    ft(p);\n"
    "    ft(b);\n"
    "    fev(f = d);\n"
    "    vf(a, vec = other);\n"
    "    fp((__int128)0);\n"
    "    fbit(5);\n"
    "    fz(d);\n"
    "    fd(1.5f16);\n"
    "    vf(a, 1.5f16);\n"
    // A call the kernel leaves, as an argument of a call of its batch: that one's record is made from T's record of
    // it, by the rule after it. Once, twice over, past the parameters, and of a call left for a vector's flag.
    "    fi(callee(1e100, b));\n"
    "    a = callee(callee(callee(1e100, b), b), b);\n"
    "    vf(a, callee(1e100, b));\n"
    "    fi(vf(a, vec = other));\n"
    // And beside an argument the first pass leaves (a call with a bit-field's value, which neither path makes): the
    // call is on the first pass's list and its other argument on the second's. The two lists are run in the window's
    // order, each argument's rule before the call's, which then makes the call from T's records of both.
    "    a = callee(callee(o->inner.bits, b), callee(1e100, b));\n"
    // An integer constant folded from a floating one (#3014): its record's payload is the floating value's, its integer
    // zero. As an argument its conversion is T's (the rule takes an integer's value from a payload that is the
    // integer's), and so is a null pointer made of it, whose value keeps the floating storage: an assignment's, and
    // what reads the assignment after.
    "    fl((int)0.5);\n"
    "    fi((int)1e-4900L);\n"
    "    vf(a, (int)0.5);\n"
    "    p = (int)0.5;\n"
    "    fip(p = (int)0.5);\n"
    "    a = *(p = (long)0.5);\n"
    // A null pointer constant that is no literal as an argument (#3012): its call is not quiet for the census's tests,
    // whatever the call's short path said; a literal's is.
    "    fip(RED);\n"
    "    fcp((char)0);\n"
    "    fv(1 - 1);\n"
    "    fp(0);\n"
    // An update (C's plan, node_machine_update_plan's, as the loop's masks). By name, which C doesn't evaluate: a
    // local's, a parameter's, a global's, a static's, a thread's and a file's.
    "    local++;\n"
    "    --a;\n"
    "    global--;\n"
    "    ++still;\n"
    "    thread_scope++;\n"
    "    file_scope -= b;\n"
    "    wide <<= 2;\n"
    // Through an address: a member's, a subscript's and a dereference's.
    "    v.inner.first++;\n"
    "    o->inner.second -= b;\n"
    "    o->link->first |= a;\n"
    "    arr[a] ^= n;\n"
    "    o->list[2]++;\n"
    "    (*p)--;\n"
    "    *p += a;\n"
    "    p[a] *= 2;\n"
    "    shorts[1] >>= 1;\n"
    "    un.whole++;\n"
    "    wp->small += a;\n"
    // A pointer's: its second operand to long, by name and through an address. One to a variable-length array has no
    // stride, and one to an element past 4 GB has one its type's row doesn't hold.
    "    p++;\n"
    "    p += a;\n"
    "    p -= b;\n"
    "    o->link++;\n"
    "    o->link += n;\n"
    "    q++;\n"
    "    q += a;\n"
    "    h++;\n"
    "    h += a;\n"
    // The classes: a char's and a short's computed as int, a float's with a double, a double's, an enumeration's, a
    // volatile one's and an aligned one's.
    "    c += 1;\n"
    "    c++;\n"
    "    f *= 2;\n"
    "    f += d;\n"
    "    d -= f;\n"
    "    d++;\n"
    "    a += d;\n"
    "    sh++;\n"
    "    sh |= a;\n"
    "    vol++;\n"
    "    va[1] |= 2;\n"
    "    vsp.first--;\n"
    "    apart++;\n"
    // What C's plan refuses, which the masks don't take (the expression is listed, unless its record's class is no
    // number's, which has no template): a long double's, a 128-bit integer's, a _BitInt's, a __float128's and a complex
    // destination; a computation or a second operand of such a type; an atomic, a vector's element and a vector; a
    // register variable's by name (as a second operand it is read like any other).
    "    e += 1;\n"
    "    a += e;\n"
    "    w++;\n"
    "    a |= w;\n"
    "    a <<= w;\n"
    "    bi++;\n"
    "    q128 += 1;\n"
    "    zed += 1;\n"
    "    shared++;\n"
    "    shared += a;\n"
    "    vec[1]++;\n"
    "    vec += other;\n"
    "    reg++;\n"
    "    reg += a;\n"
    "    a += reg;\n"
    // A register variable's use has its flag in parentheses too. A register struct's member and what a register
    // pointer points to are no names: C's plan takes them.
    "    (reg)--;\n"
    "    regp++;\n"
    "    regs.first++;\n"
    "    regs.second += b;\n"
    "    (*regp)++;\n"
    "    regp[1] -= a;\n"
    // A pointer with a qualifier: T's conversion of it, whose stride the loop reads, is its type without the qualifier.
    "    vptr++;\n"
    "    vptr -= a;\n"
    // A second operand that is a wrapper's record of a long double: it has a template, and the update's plan refuses
    // the computation's class.
    "    a += (long double)b;\n"
    // A pointer's second operand is of any class, which C converts to long: a wrapper's record of a 128-bit integer.
    "    p += (__int128)b;\n"
    // A bit-field's is a value the columns don't hold whole, which lists its expression.
    "    v.inner.bits++;\n"
    "    o->inner.bits |= 1;\n"
    // A division and a remainder: by a value, the update's; by an integer constant listed as the plain ones are, an
    // integer's alone (a floating one's has no power of two's shift).
    "    a /= n;\n"
    "    a %= n;\n"
    "    a /= 4;\n"
    "    a %= 8;\n"
    "    b /= 3;\n"
    "    d /= 2;\n"
    "    f /= 4;\n"
    // An update's value used, and one below another operator: its record is an operand like any other.
    "    a = local++;\n"
    "    a = ++local + n;\n"
    "    b = (wide += a);\n"
    "    a = arr[local++];\n"
    "    arr[local++] += arr[a--];\n"
    "    fi(a++);\n"
    "    fl(wide -= 1);\n"
    "    a = *p++;\n"
    "    *p++ = a;\n"
    "    a += local += n;\n"
    // A consumer past two blocks: an update's destination before a long second operand, by name (not evaluated),
    // through an address (evaluated for it) and a pointer's both ways, whose key the far lanes ask the rule for again.
    "    a += " BUSTER_NODE_CONTEXTS_TEST_SUM ";\n"
    "    o->inner.first += " BUSTER_NODE_CONTEXTS_TEST_SUM ";\n"
    "    p += " BUSTER_NODE_CONTEXTS_TEST_SUM ";\n"
    "    o->link += " BUSTER_NODE_CONTEXTS_TEST_SUM ";\n"
    // An update's five records, a block's end between every two in turn: by name, through an address, a pointer's
    // both ways, and a subscript's `++`.
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    local += -n;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    *p += a;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    p -= -n;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    o->link += n;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    arr[a]++;\n")
    // A consumer and an operand past two blocks: a call's callee and its first argument before a long second one.
    "    a = callee(a, " BUSTER_NODE_CONTEXTS_TEST_SUM ");\n"
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    v.inner.second = -b;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    (void)&*p;\n")
    // A call's five records, a block's end between every two in turn: a direct call's, and one through a pointer.
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    callee(a, b);\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    (void)fptr(a);\n")
    // A condition's records, a block's end between every two in turn: taken with a global first and a `!` second, with
    // `&&` under `||`, with a `!` first and with a `!` over a `!`; listed for a constant first and for a `!` over `&&`.
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    if (global && !n) { local = 1; }\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    if (a && n || local) local++;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    if (!a || n) { local = 1; }\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    if (1 && n) local++;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    if (!(a && n)) { local = 1; }\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    if (a && !!n) local++;\n")
    "    return a + c;\n"
    "}\n"
    // An operator's value and a comma (the comment above the fixture has the cases).
    "int valued(int a, long b, char c, short h, unsigned char u, _Bool t, float x, double d, int* p, struct pair* q, long double ld, __float128 f128, _BitInt(24) bits24)\n"
    "{\n"
    "    int local = 0;\n"
    "    long wide = 0;\n"
    "    char narrow = 0;\n"
    "    _Bool flag = 0;\n"
    "    double real = 0;\n"
    "    int* ptr = 0;\n"
    // Commas: discarded and not, nested either way, with a void operand on either side, of a pointer, under a
    // qualified void, as a conditional's arm of either kind, after a store through an address, an aggregate's and a
    // bit-field's, folded, in a for statement's clauses, and with two calls.
    "    a++, b++;\n"
    "    local = 1, wide = 2;\n"
    "    local = (a++, b);\n"
    "    local = (a, b, c);\n"
    "    wide = (a, b);\n"
    "    (void)(a, (b, c));\n"
    "    local = (a, (b, c));\n"
    "    (a, b), c;\n"
    "    (fi(a), a);\n"
    "    (a, fi(a));\n"
    "    ptr = (a, p);\n"
    "    (const void)(a, b);\n"
    "    local = a ? (b, c) : 0;\n"
    "    a ? (void)(b, c) : fi(a);\n"
    "    a ? (b, c) : fi(a);\n"
    "    (*p = 1, a);\n"
    "    (*q = *q, a);\n"
    "    (q->bits = 1, a);\n"
    "    local = (1, 2);\n"
    "    (void)(1, 2);\n"
    "    local = (a, *q).first;\n"
    "    for (a = 0, b = 1; a < 3; a++, b--) local += a;\n"
    "    local = (callee(a, b), callee(c, a));\n"
    "    real = (a, d);\n"
    "    (void)(a, ld);\n"
    // A comma that is a void conditional's condition: its target is T's conversion of it, no void.
    "    (a, b) ? fi(a) : fi(local);\n"
    // Conditionals: arms converted and not, folded, to bool after its end, of void's class with an arm of each
    // kind, discarded; a condition of each class, branched, under a `!` and known; nested either way and beside
    // one another; with calls; of pointers and of an aggregate; of one operand twice; a root's, taken and listed
    // for its target; a statement's condition's, tested and owned; and what its consumer puts into its arms.
    "    local = a ? b : c;\n"
    "    local = a ? c : b;\n"
    "    local = a ? 1 : 2;\n"
    "    wide = a ? 1 : 2;\n"
    "    wide = a ? 1 : b;\n"
    "    narrow = a ? c : c;\n"
    "    narrow = a ? c : 1;\n"
    "    flag = a ? b : c;\n"
    "    flag = a ? 1 : 0;\n"
    "    real = a ? x : d;\n"
    "    real = a ? 1 : 2;\n"
    "    real = a ? 1 : x;\n"
    "    a ? fi(a) : fi(local);\n"
    "    a ? (void)b : fi(a);\n"
    "    a ? b : fi(a);\n"
    "    (void)(a ? b : c);\n"
    "    local = c ? a : b;\n"
    "    local = t ? a : b;\n"
    "    local = p ? *p : 0;\n"
    "    local = x ? a : 1;\n"
    "    local = ld ? a : 1;\n"
    "    local = (a && b) ? c : h;\n"
    "    local = (a || b) ? c : h;\n"
    "    local = !(a && b) ? c : h;\n"
    "    local = !!(a || b) ? c : h;\n"
    "    local = !a ? b : c;\n"
    "    local = !!a ? b : c;\n"
    "    local = 1 ? a : b;\n"
    "    local = 0 ? a : b;\n"
    "    local = &global ? a : b;\n"
    "    local = a ? b ? 1 : 2 : 3;\n"
    "    local = a ? 1 : b ? 2 : 3;\n"
    "    local = (a ? 1 : 2) + (b ? 3 : 4);\n"
    "    local = (a ? 1 : 2) + (b ? 3 : 4) + (c ? 5 : 6);\n"
    "    local = (a ? b : c) ? (h ? 1 : 2) : (u ? 3 : 4);\n"
    "    local = a ? callee(a, b) : callee(c, a);\n"
    "    local = callee(a ? 1 : 2, b);\n"
    "    local = callee(a, a ? 1 : 2);\n"
    "    local = callee(a, a ? b : c);\n"
    "    ptr = a ? p : ptr;\n"
    "    ptr = a ? p : 0;\n"
    "    local = *(a ? p : ptr);\n"
    "    local = (a ? q : q)->first;\n"
    "    *q = a ? *q : *q;\n"
    "    local = a ?: 3;\n"
    "    int rooted = a ? 1 : 2;\n"
    "    char sunk_root = a ? 1 : 2;\n"
    "    long wide_root = a ? b : c;\n"
    "    if (a ? b : c) local = 1;\n"
    "    if (h && (a ? b : c)) local = 1;\n"
    "    if ((a ? b : c) || h) local = 2;\n"
    "    if (!(a ? b : c)) local = 3;\n"
    "    local = (char)(a ? 1 : 2);\n"
    "    wide = (a ? 1 : 2) + b;\n"
    "    p[a ? 1 : 2] = 3;\n"
    "    local = p[a ? b : c];\n"
    "    local += a ? 1 : 2;\n"
    "    wide += a ? 1 : 2;\n"
    "    local = a ? b++ : c--;\n"
    "    local = a ? q->bits : 1;\n"
    // An arm whose conversion's type is not the conditional's (a null pointer constant's); a condition and an operand
    // of a type no truth's entries test; a conditional of a type no template takes; what the tasks may sink, by each
    // term: a first operand's target and a second's, a consumer's conversion that is not its type, an arm of each
    // kind of constant on either side and none, and a target of another class of each kind; and an operator's value
    // that is not evaluated.
    "    ptr = a ? 0 : p;\n"
    "    local = f128 ? a : 1;\n"
    "    local = f128 && a;\n"
    "    local = a && f128;\n"
    "    fbit(a ? bits24 : bits24);\n"
    "    local = (a ? 1 : 2) && wide;\n"
    "    local = (a ? 1 : 2) < wide;\n"
    "    local = (a ? 1 : 2) < b ? 3 : c;\n"
    "    wide = a ? 1 : c;\n"
    "    wide = a ? c : 1;\n"
    "    wide = a ? c : h;\n"
    "    const char* text = a ? \"x\" : \"y\";\n"
    "    real = a ? 1 : c;\n"
    "    ptr = a ? p : (void*)0;\n"
    "    local = 0 && (a ? b : c);\n"
    "    local = 1 || (a && b);\n"
    "    local = a && h;\n"
    "    local = a || u;\n"
    // Past two blocks: a conditional's condition, whose consumer is far and whose own first operand is far; a
    // comma's first operand, whose consumer's key is a stored one; and a void conditional's arm that is a comma.
    "    local = a ? " BUSTER_NODE_CONTEXTS_TEST_SUM " : " BUSTER_NODE_CONTEXTS_TEST_SUM ";\n"
    "    (void)(a++, " BUSTER_NODE_CONTEXTS_TEST_SUM ");\n"
    "    wide = (b++, " BUSTER_NODE_CONTEXTS_TEST_SUM ");\n"
    "    a ? (b, c) : fi(" BUSTER_NODE_CONTEXTS_TEST_SUM ");\n"
    "    local = (a || " BUSTER_NODE_CONTEXTS_TEST_SUM ") && " BUSTER_NODE_CONTEXTS_TEST_SUM ";\n"
    "    if (!a && " BUSTER_NODE_CONTEXTS_TEST_SUM ") local = 4;\n"
    "    local = !a && " BUSTER_NODE_CONTEXTS_TEST_SUM ";\n"
    // `&&` and `||` as values: of each class on either side, narrow ones among them; with an operator of either
    // kind for an operand, on either side; under and over `!`s, by runs of each length; what its consumer puts into
    // its value; discarded; beside another; an argument; with a first operand whose truth is known; under a
    // comparison in a condition; and a root's.
    "    local = a && b;\n"
    "    local = a || b;\n"
    "    local = x && d;\n"
    "    local = p || q;\n"
    "    local = a && c;\n"
    "    local = c && a;\n"
    "    local = t || b;\n"
    "    local = (a && b) || wide;\n"
    "    local = (a || b) && wide;\n"
    "    local = a && (b || wide);\n"
    "    local = a || (b && wide);\n"
    "    local = !(a && b);\n"
    "    local = !!(a || b);\n"
    "    local = !(a && b) || wide;\n"
    "    local = wide || !(a && b);\n"
    "    local = !a && b;\n"
    "    local = a && !b;\n"
    "    local = a && !!(b && wide);\n"
    "    local = a && !!!(b && wide);\n"
    "    if (a && !!(b && wide)) local = 1;\n"
    "    if (a && !!!(b && wide)) local = 1;\n"
    "    if (a && (b || (wide && (local || a)))) local = 1;\n"
    "    if (a && (b || (wide && (local || (a && b))))) local = 1;\n"
    "    local = a && (b || (wide && (local || a)));\n"
    "    wide = a && b;\n"
    "    flag = a || b;\n"
    "    narrow = a && b;\n"
    "    real = a && b;\n"
    "    (void)(a || callee(a, b));\n"
    "    a && callee(a, b);\n"
    "    local = (a && b) + (wide || local);\n"
    "    local = (a && b) + (wide || local) + (a || wide);\n"
    "    local = callee(a && b, 1);\n"
    "    local = callee(a, a || b);\n"
    "    local = 1 && a;\n"
    "    local = 0 || a;\n"
    "    local = a ? b && wide : local || a;\n"
    "    local = (a && b) ? (wide || local) : (a && wide);\n"
    "    if ((a && b) == c) local = 1;\n"
    "    if ((a || b) + 1) local = 2;\n"
    "    int root_and = a && b;\n"
    "    long root_or = a || b;\n"
    "    _Bool root_flag = a && b;\n"
    // A block's end between every two records in turn: a `!` over an `&&` under a branched one (listed where the `!`
    // starts a block); a value's `||` with a `!` over an `&&` second (a run of three chained records); a discarded
    // comma's three operands; a conditional and an `||` beside it (two operators with labels: numbered); a void
    // conditional's arm that is a cast to void over a comma; and a run of three chained operators under a condition.
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    if (a && !(b && wide)) local = 1;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    local = wide || !(a && b);\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    (void)(a++, b--, wide);\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    local = (a ? local + a : local) + (wide || a);\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    a ? (void)(b++, wide) : fi(a);\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    if (a && (b || (wide && local))) local++;\n")
    "    return local + (int)wide + narrow + flag + (int)real + (ptr != 0) + rooted + sunk_root + (int)wide_root + root_and + (int)root_or + root_flag + text[0];\n"
    "}\n";

// C on the window's fixture for a unit with a type in a space of its own (TypeTable.extended: here `__seg_gs`'s and
// `__seg_fs`'s): the loop asks the space of the destination of each update its masks take, a record at a time, and
// lists the expression of one in a segment's, which C's plan refuses (check_lower_segment_override).
// - In the flat space, taken: by name, through an address, and a pointer's both ways. A pointer to an int in a
//   segment's space is itself in the flat one.
// - In a segment's space, listed: through a pointer to it, by name, and a member's through a pointer to a struct in
//   one.
// - An update's five records in each space in turn, a block's end between every two: every block has lanes of both.
static const char node_contexts_test_spaced_source[] =
    "struct pair { int first; long second; };\n"
    "extern int __seg_gs* segmented;\n"
    "extern int __seg_gs slot;\n"
    "extern struct pair __seg_fs* shared_pair;\n"
    "extern int global;\n"
    "int spaced(int a, long b, int* p, struct pair* o)\n"
    "{\n"
    "    int local = a;\n"
    "    local++;\n"
    "    global += a;\n"
    "    *p += a;\n"
    "    o->first--;\n"
    "    o->second += b;\n"
    "    p++;\n"
    "    p += a;\n"
    "    (*segmented)++;\n"
    "    *segmented += a;\n"
    "    slot++;\n"
    "    slot -= a;\n"
    "    shared_pair->first++;\n"
    "    shared_pair->second += b;\n"
    "    segmented++;\n"
    "    segmented += a;\n"
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    *segmented += a;\n    *p += a;\n")
    "    return local;\n"
    "}\n";

// What C on the window makes of a fixture: the expressions it runs on, those it decides and those of them it refuses
// for a template, the listed ones by their first record's reason, the records whose stride was read and those of them
// asked of the type table, the records whose rule read T's conversions, and the consumers and operands read by loads.
typedef struct NodeContextsExpected NodeContextsExpected;
struct NodeContextsExpected
{
    uint32_t expressions;
    uint32_t decided;
    uint32_t refused;
    uint32_t strides;
    uint32_t asked;
    uint32_t converted;
    uint32_t far;
    // The updates C asks its plan of, and those of them the loop's masks took: another lists its expression.
    uint32_t updates;
    uint32_t updates_taken;
    // The records the loop evaluates as branches, and those of them a statement's CONDITION.
    uint32_t branched;
    uint32_t branched_roots;
    // The records it gives an operator's value's two labels, those of them that list their expression for what their
    // consumer puts into their arms, and the commas it takes.
    uint32_t valued;
    uint32_t sunk;
    uint32_t commas;
    uint32_t listed[BUSTER_NODE_CONTEXTS_REASONS];
    // The kernel in place's calls: those it is to make, those it made, those of them with every argument's conversion
    // made and the plain ones among those, and those its rule left to T for an argument alone.
    uint32_t calls;
    uint32_t calls_made;
    uint32_t calls_converted;
    uint32_t calls_plain;
    uint32_t calls_left;
    // And of the calls with every argument's conversion made, those with an argument: the comparison's control's count.
    uint32_t calls_compared;
    // With the arguments' slots as one (BUSTER_NODE_READER_ONE_SLOT): the arguments' rule's answers (one an argument
    // whose key is not the last answered), and those of them in the timed run.
    uint32_t single_answers;
    uint32_t single_timed;
};

// C on the window on fixture `text` (written to `directory` as `name`, read with `options`): every expression it
// decides has C's verdict on T's columns and, taken, C's answers of each record; and its counts are `expected`'s.
static bool node_contexts_test_fixture(const char* directory, const char* name, const char* text, char** options, uint32_t option_count, const NodeContextsExpected* expected)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    bool valid = parse_test_write(path, text, strlen(text));
    NodeShadow shadow = {
        .verify = true,
        .front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | BUSTER_NODE_FRONT_LEAN,
        .reader = BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_VERIFY | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS |
                  BUSTER_NODE_READER_WINDOW_CONTEXTS,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    const uint64_t* counts = shadow.reader_facts;
    bool same = counts[NODE_FACTS_COUNT_CONTEXTS_EXPRESSIONS] == expected->expressions && counts[NODE_FACTS_COUNT_CONTEXTS_DECIDED] == expected->decided &&
                counts[NODE_FACTS_COUNT_CONTEXTS_REFUSED] == expected->refused && counts[NODE_FACTS_COUNT_CONTEXTS_STRIDES] == expected->strides &&
                counts[NODE_FACTS_COUNT_CONTEXTS_STRIDES_ASKED] == expected->asked && counts[NODE_FACTS_COUNT_CONTEXTS_CONVERTED] == expected->converted &&
                counts[NODE_FACTS_COUNT_CONTEXTS_FAR] == expected->far &&
                counts[NODE_FACTS_COUNT_CONTEXTS_UPDATES] == expected->updates && counts[NODE_FACTS_COUNT_CONTEXTS_UPDATES_TAKEN] == expected->updates_taken &&
                counts[NODE_FACTS_COUNT_CONTEXTS_BRANCHED] == expected->branched && counts[NODE_FACTS_COUNT_CONTEXTS_BRANCHED_ROOTS] == expected->branched_roots &&
                counts[NODE_FACTS_COUNT_CONTEXTS_VALUED] == expected->valued && counts[NODE_FACTS_COUNT_CONTEXTS_SUNK] == expected->sunk &&
                counts[NODE_FACTS_COUNT_CONTEXTS_COMMAS] == expected->commas &&
                counts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES] == 0 && counts[NODE_FACTS_COUNT_CONTEXTS_RECORD_DIFFERENCES] == 0 &&
                counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES] == 0 && counts[NODE_FACTS_COUNT_TESTS_DIFFERENCES] == 0 &&
                counts[NODE_FACTS_COUNT_WINDOW_CALLS] == expected->calls && counts[NODE_FACTS_COUNT_WINDOW_CALLS_MADE] == expected->calls_made &&
                counts[NODE_FACTS_COUNT_WINDOW_CALLS_CONVERTED] == expected->calls_converted && counts[NODE_FACTS_COUNT_WINDOW_CALLS_PLAIN] == expected->calls_plain &&
                counts[NODE_FACTS_COUNT_WINDOW_CALLS_ARGUMENT] == expected->calls_left;
    uint64_t listed = 0;
    for (uint32_t reason = 0; reason < BUSTER_NODE_CONTEXTS_REASONS; reason += 1)
    {
        same = same && shadow.contexts_listed[reason] == expected->listed[reason];
        listed += shadow.contexts_listed[reason];
    }
    same = same && counts[NODE_FACTS_COUNT_CONTEXTS_DECIDED] + listed == counts[NODE_FACTS_COUNT_CONTEXTS_EXPRESSIONS];
    if (!valid || !same)
    {
        fprintf(stderr, "test-checker: node-contexts %s: %s: C on the window ran on %" PRIu64 " expressions (%u expected), decided %" PRIu64 " (%u) and refused %" PRIu64
                " of them (%u), read %" PRIu64 " strides (%u), %" PRIu64 " of the type table (%u), %" PRIu64 " rules by T's conversions (%u), and %" PRIu64
                " consumers and operands by loads (%u); of the %" PRIu64 " updates C asks its plan of (%u) the masks took %" PRIu64 " (%u); %" PRIu64
                " records are evaluated as branches (%u), %" PRIu64 " of them a condition (%u); %" PRIu64 " are an operator's value (%u), %" PRIu64
                " of them listed for their target (%u), and %" PRIu64 " commas are taken (%u); %" PRIu64 " expressions differ from C on T's columns, in %" PRIu64
                " records; listed:",
                name, valid ? "read" : "NOT READ", counts[NODE_FACTS_COUNT_CONTEXTS_EXPRESSIONS], expected->expressions, counts[NODE_FACTS_COUNT_CONTEXTS_DECIDED],
                expected->decided, counts[NODE_FACTS_COUNT_CONTEXTS_REFUSED], expected->refused, counts[NODE_FACTS_COUNT_CONTEXTS_STRIDES], expected->strides,
                counts[NODE_FACTS_COUNT_CONTEXTS_STRIDES_ASKED], expected->asked, counts[NODE_FACTS_COUNT_CONTEXTS_CONVERTED], expected->converted,
                counts[NODE_FACTS_COUNT_CONTEXTS_FAR], expected->far, counts[NODE_FACTS_COUNT_CONTEXTS_UPDATES], expected->updates,
                counts[NODE_FACTS_COUNT_CONTEXTS_UPDATES_TAKEN], expected->updates_taken, counts[NODE_FACTS_COUNT_CONTEXTS_BRANCHED], expected->branched,
                counts[NODE_FACTS_COUNT_CONTEXTS_BRANCHED_ROOTS], expected->branched_roots, counts[NODE_FACTS_COUNT_CONTEXTS_VALUED], expected->valued,
                counts[NODE_FACTS_COUNT_CONTEXTS_SUNK], expected->sunk, counts[NODE_FACTS_COUNT_CONTEXTS_COMMAS], expected->commas,
                counts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES], counts[NODE_FACTS_COUNT_CONTEXTS_RECORD_DIFFERENCES]);
        for (uint32_t reason = 1; reason < BUSTER_NODE_CONTEXTS_REASONS; reason += 1)
        {
            fprintf(stderr, " %s=%" PRIu64 " (%u)", node_contexts_reason_names[reason], shadow.contexts_listed[reason], expected->listed[reason]);
        }
        fprintf(stderr, "; the kernel in place's calls: %" PRIu64 " to make (%u), %" PRIu64 " made (%u), %" PRIu64 " with every argument's conversion (%u), %" PRIu64
                " of them plain (%u), %" PRIu64 " left for an argument (%u), %" PRIu64 " records not T's\n",
                counts[NODE_FACTS_COUNT_WINDOW_CALLS], expected->calls, counts[NODE_FACTS_COUNT_WINDOW_CALLS_MADE], expected->calls_made,
                counts[NODE_FACTS_COUNT_WINDOW_CALLS_CONVERTED], expected->calls_converted, counts[NODE_FACTS_COUNT_WINDOW_CALLS_PLAIN], expected->calls_plain,
                counts[NODE_FACTS_COUNT_WINDOW_CALLS_ARGUMENT], expected->calls_left, counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES]);
    }
    // The comparison's control: with one of the reference's answers changed for each expression C takes (its root's
    // template, context, target or whether it is evaluated, or its verdict, in turn), every decided expression C takes
    // differs, most by a record and the rest by the verdict. And with T's conversion of each call's arguments
    // changed, every call whose arguments' conversions the kernel made, and that has an argument, differs from T's:
    // once over poison and once over decoys.
    NodeShadow faulted = {
        .verify = true,
        .front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | BUSTER_NODE_FRONT_LEAN,
        .reader = BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_VERIFY | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS |
                  BUSTER_NODE_READER_WINDOW_CONTEXTS | BUSTER_NODE_READER_WINDOW_CONTEXTS_FAULT,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus faulted_census = { .shadow = &faulted };
    const bool faulted_read = valid && node_census_file(&faulted_census, path, options, option_count) && faulted_census.files == 1;
    const uint64_t* faulted_counts = faulted.reader_facts;
    const uint64_t taken = (uint64_t)expected->decided - expected->refused;
    const bool controlled = faulted_read && faulted_counts[NODE_FACTS_COUNT_CONTEXTS_DECIDED] == expected->decided &&
                            faulted_counts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES] == taken && faulted_counts[NODE_FACTS_COUNT_CONTEXTS_RECORD_DIFFERENCES] != 0 &&
                            faulted_counts[NODE_FACTS_COUNT_CONTEXTS_RECORD_DIFFERENCES] < taken &&
                            faulted_counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES] == 2 * (uint64_t)expected->calls_compared;
    if (!controlled)
    {
        fprintf(stderr, "test-checker: node-contexts %s: %s: with one of the reference's answers changed for each expression C takes, the comparison counts %" PRIu64
                " of the %" PRIu64 " decided expressions (%" PRIu64 " expected: those C takes), %" PRIu64 " records; with T's conversion of each call's arguments "
                "changed, the kernel in place's comparison counts %" PRIu64 " records (%" PRIu64 " expected: twice the calls it converted that have an argument)\n",
                name, faulted_read ? "read" : "NOT READ", faulted_counts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES], faulted_counts[NODE_FACTS_COUNT_CONTEXTS_DECIDED], taken,
                faulted_counts[NODE_FACTS_COUNT_CONTEXTS_RECORD_DIFFERENCES], faulted_counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES],
                2 * (uint64_t)expected->calls_compared);
    }
    // The arguments' slots as one: a key's probe meets another key's answer at its slot wherever the last answer was
    // another's, and the kernel's records, the calls' counts and C's answers are the same. The rule's answers are
    // pinned, and those of the timed run: what the calls' loop takes of a slot is no more and no less than its own key's.
    NodeShadow single = {
        .verify = true,
        .front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | BUSTER_NODE_FRONT_LEAN,
        .reader = BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_VERIFY | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS |
                  BUSTER_NODE_READER_WINDOW_CONTEXTS | BUSTER_NODE_READER_ONE_SLOT,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus single_census = { .shadow = &single };
    const bool single_read = valid && node_census_file(&single_census, path, options, option_count) && single_census.files == 1 && single_census.failures == 0;
    const uint64_t* single_counts = single.reader_facts;
    const bool single_same = single_read && single_counts[NODE_FACTS_COUNT_CONTEXTS_EXPRESSIONS] == expected->expressions &&
                             single_counts[NODE_FACTS_COUNT_CONTEXTS_DECIDED] == expected->decided && single_counts[NODE_FACTS_COUNT_CONTEXTS_REFUSED] == expected->refused &&
                             single_counts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES] == 0 && single_counts[NODE_FACTS_COUNT_CONTEXTS_RECORD_DIFFERENCES] == 0 &&
                             single_counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES] == 0 && single_counts[NODE_FACTS_COUNT_TESTS_DIFFERENCES] == 0 &&
                             single_counts[NODE_FACTS_COUNT_WINDOW_CALLS] == expected->calls && single_counts[NODE_FACTS_COUNT_WINDOW_CALLS_MADE] == expected->calls_made &&
                             single_counts[NODE_FACTS_COUNT_WINDOW_CALLS_CONVERTED] == expected->calls_converted &&
                             single_counts[NODE_FACTS_COUNT_WINDOW_CALLS_PLAIN] == expected->calls_plain &&
                             single_counts[NODE_FACTS_COUNT_WINDOW_CALLS_ARGUMENT] == expected->calls_left &&
                             single_counts[NODE_FACTS_COUNT_ARGUMENT_ANSWERS] == expected->single_answers &&
                             single_counts[NODE_FACTS_COUNT_WINDOW_ANSWERS] == expected->single_timed;
    if (!single_same)
    {
        fprintf(stderr, "test-checker: node-contexts %s: %s: with the arguments' slots as one, C on the window decided %" PRIu64 " of %" PRIu64 " expressions (%u of %u "
                "expected) and refused %" PRIu64 " (%u); %" PRIu64 " expressions differ from C on T's columns, in %" PRIu64 " records; the kernel in place's calls: %" PRIu64
                " to make (%u), %" PRIu64 " made (%u), %" PRIu64 " with every argument's conversion (%u), %" PRIu64 " of them plain (%u), %" PRIu64
                " left for an argument (%u), the arguments' rule's answers %" PRIu64 " (%u; %" PRIu64 " with the slots apart), %" PRIu64
                " of them in the timed run (%u), %" PRIu64 " records not T's\n",
                name, single_read ? "read" : "NOT READ", single_counts[NODE_FACTS_COUNT_CONTEXTS_DECIDED], single_counts[NODE_FACTS_COUNT_CONTEXTS_EXPRESSIONS],
                expected->decided, expected->expressions, single_counts[NODE_FACTS_COUNT_CONTEXTS_REFUSED], expected->refused,
                single_counts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES], single_counts[NODE_FACTS_COUNT_CONTEXTS_RECORD_DIFFERENCES],
                single_counts[NODE_FACTS_COUNT_WINDOW_CALLS], expected->calls, single_counts[NODE_FACTS_COUNT_WINDOW_CALLS_MADE], expected->calls_made,
                single_counts[NODE_FACTS_COUNT_WINDOW_CALLS_CONVERTED], expected->calls_converted, single_counts[NODE_FACTS_COUNT_WINDOW_CALLS_PLAIN],
                expected->calls_plain, single_counts[NODE_FACTS_COUNT_WINDOW_CALLS_ARGUMENT], expected->calls_left,
                single_counts[NODE_FACTS_COUNT_ARGUMENT_ANSWERS], expected->single_answers, counts[NODE_FACTS_COUNT_ARGUMENT_ANSWERS],
                single_counts[NODE_FACTS_COUNT_WINDOW_ANSWERS], expected->single_timed, single_counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES]);
    }
    unlink(path);
    return valid && same && controlled && single_same;
}

// X's rows on the window's fixture (#2850's step 2, c_check_node_machine_rows.c; docs/one-core-path.md, "X's rows on
// the window"): a line a shape, and each kind of expression it lists.
// - Names, constants and stores: a local's, a global's and a pointer's, read and written.
// - A binary operator's kind, each; the two unary ones; a truth as a value and as a statement's condition, of a wide
//   class and of a narrow one.
// - A member with and without an offset, read for its value and for its address, through a pointer, of a local and
//   of a global; a pointer's sum, its difference, its subscript read and addressed.
// - An address constant: a local's and a global's, with and without an offset, an array's decay, a member's; one
//   converted to an integer; one with no object, converted too; and a discarded one, with a discarded integer.
// - Conversions: between classes, to bool from a wide class and from a narrow one, to void (a discarded value), and
//   between two types of one class (none).
// - An integer constant's folded: to another integer's class, an unsigned long's to and from, to bool; a floating
//   one's, which is not folded.
// - A bool's conversion to bool (a const one's read), an aggregate's read through a pointer to const and its
//   discard, and an update that is not evaluated (a sizeof's operand).
// - An update's two rows: by name and through an address, prefix and postfix, each operator, a narrow, a floating
//   and a pointer's destination, its value used, and an update below another.
// - A condition evaluated as branches: `&&`, `||` and `!` under an if, a while, a do and a for, nested in either
//   operand, and a tested value of each class's width, a member's, a call's, a comparison's and an update's.
// - What it lists: an integer constant converted to a floating class; a conversion to a long double, a return's
//   (its root's record's alone); a bool's update, and a postfix one through an address with no room for its first
//   row; and a tested value with a conversion of its own (a const bool's read).
// - 64 statements of an odd number of records each, with a member's row of three entries and a conversion to bool of
//   four: a step's eight records' entries straddle the streams' registers in turn.
static const char node_rows_test_source[] =
    "struct pair { int first; long second; char third; _Bool flag; };\n"
    "struct outer { struct pair inner; struct pair* link; int list[4]; };\n"
    "extern int global;\n"
    "extern long global_long;\n"
    "extern struct outer global_outer;\n"
    "extern int table[8];\n"
    "extern long double big;\n"
    "int callee(int a, long b);\n"
    "void nothing(void);\n"
    "int rows(int a, long b, char c, short h, unsigned char u, int* p, struct outer* o, double d, _Bool t, float f, const _Bool cb, const struct outer* co)\n"
    "{\n"
    "    int local = 0;\n"
    "    long wide = 0;\n"
    "    char narrow = 0;\n"
    "    _Bool flag = 0;\n"
    "    double value = 0;\n"
    "    float single = 0;\n"
    "    int list[4];\n"
    "    struct outer whole;\n"
    "    int* q = 0;\n"
    "    long* lp = 0;\n"
    "    unsigned long ul = 0;\n"
    "    unsigned short us = 0;\n"
    // A _BitInt's target is an initialized object's: of its value's class it writes no entry; of another, a CONVERT,
    // and a constant's is no fold. A complex, a long double's, a 128-bit integer's and a half-precision target's
    // conversions are refused.
    "    _BitInt(32) same_bits = a;\n"
    "    _BitInt(17) odd_bits = a;\n"
    "    _BitInt(8) narrow_bits = a;\n"
    "    _BitInt(32) constant_bits = 5;\n"
    "    unsigned _BitInt(64) wide_bits = 5;\n"
    "    unsigned _BitInt(64) converted_bits = a;\n"
    "    _Complex double complex_constant = 1;\n"
    "    _Complex double complex_double = d;\n"
    "    _Complex float complex_float = f;\n"
    "    long double extended_local = a;\n"
    "    __int128 huge = a;\n"
    "    _Float16 half = f;\n"
    "    local = a;\n"
    "    local = 7;\n"
    "    global = a;\n"
    "    local = global;\n"
    "    *p = a;\n"
    "    local = *p;\n"
    "    local = a + local;\n"
    "    local = a - local;\n"
    "    local = a * local;\n"
    "    local = a / local;\n"
    "    local = a % local;\n"
    "    local = a & local;\n"
    "    local = a | local;\n"
    "    local = a ^ local;\n"
    "    local = a << local;\n"
    "    local = a >> local;\n"
    "    local = a == local;\n"
    "    local = a != local;\n"
    "    local = a < local;\n"
    "    local = a <= local;\n"
    "    local = a > local;\n"
    "    local = a >= local;\n"
    "    local = u / u;\n"
    "    local = u >> a;\n"
    "    value = d * d;\n"
    "    local = d < d;\n"
    "    local = -a;\n"
    "    local = ~a;\n"
    "    value = -d;\n"
    "    local = !a;\n"
    "    local = !c;\n"
    "    local = !b;\n"
    "    local = !d;\n"
    "    local = !p;\n"
    "    if (a) local = 1;\n"
    "    if (c) local = 2;\n"
    "    if (b) local = 3;\n"
    "    if (p) local = 4;\n"
    "    if (d) local = 5;\n"
    "    if (h) local = 6;\n"
    "    if (t) local = 7;\n"
    "    while (u) u = 0;\n"
    "    local = o->inner.first;\n"
    "    wide = o->inner.second;\n"
    "    lp = &o->inner.second;\n"
    "    q = &o->inner.first;\n"
    "    local = o->link->first;\n"
    "    wide = o->link->second;\n"
    "    narrow = o->link->third;\n"
    "    local = whole.inner.first;\n"
    "    wide = whole.inner.second;\n"
    "    local = global_outer.list[1];\n"
    "    wide = global_outer.inner.second;\n"
    "    o->inner.second = b;\n"
    "    o->link->first = a;\n"
    "    whole.inner.second = b;\n"
    "    q = p + a;\n"
    "    q = p - a;\n"
    "    q = a + p;\n"
    "    local = p[a];\n"
    "    q = &p[a];\n"
    "    local = *(p + a);\n"
    "    local = list[a];\n"
    "    local = table[a];\n"
    "    q = &list[a];\n"
    "    p[a] = local;\n"
    "    q = &local;\n"
    "    q = &global;\n"
    "    q = &list[2];\n"
    "    q = &table[2];\n"
    "    q = list;\n"
    "    q = table;\n"
    "    lp = &whole.inner.second;\n"
    "    lp = &global_outer.inner.second;\n"
    "    q = &whole.inner.first;\n"
    "    wide = (long)&global;\n"
    "    wide = (long)&list[1];\n"
    "    wide = (long)&local;\n"
    "    wide = (long)&table[3];\n"
    "    local = (int)(long)&global;\n"
    "    q = (int*)16;\n"
    "    wide = (long)(int*)16;\n"
    "    local = *(int*)32;\n"
    "    local = c;\n"
    "    wide = a;\n"
    "    local = h;\n"
    "    local = u;\n"
    "    value = a;\n"
    "    local = d;\n"
    "    single = d;\n"
    "    value = f;\n"
    "    narrow = a;\n"
    "    wide = d;\n"
    "    u = b;\n"
    "    lp = (long*)p;\n"
    "    wide = (long)p;\n"
    "    p = (int*)b;\n"
    "    flag = a;\n"
    "    flag = c;\n"
    "    flag = b;\n"
    "    flag = d;\n"
    "    flag = p;\n"
    "    flag = h;\n"
    "    flag = u;\n"
    "    local = t;\n"
    "    flag = t;\n"
    "    flag = o->inner.flag;\n"
    "    wide = 1;\n"
    "    narrow = 2;\n"
    "    flag = 1;\n"
    "    flag = 0;\n"
    "    q = 0;\n"
    "    local = 'x';\n"
    "    u = 300;\n"
    "    wide = -1;\n"
    "    h = 70000;\n"
    "    b = 4000000000u;\n"
    "    value = 1;\n"
    "    single = 2;\n"
    "    d = 16777217;\n"
    "    f = 16777217;\n"
    "    big = a;\n"
    "    local = big;\n"
    "    callee(a, b);\n"
    "    local = callee(a, b);\n"
    "    wide = callee(c, a);\n"
    "    nothing();\n"
    "    (void)a;\n"
    "    (void)callee(a, 1);\n"
    "    (void)o->inner.second;\n"
    "    7;\n"
    "    (void)7;\n"
    "    &global;\n"
    "    a;\n"
    "    wide = (long)&((struct pair*)0)->second;\n"
    "    local = (int)&((struct pair*)0)->third;\n"
    "    lp = &((struct pair*)0)->second;\n"
    "    ul = 5;\n"
    "    narrow = 7ul;\n"
    "    ul = (unsigned long)&global;\n"
    "    local = 2.5;\n"
    "    flag = cb;\n"
    "    whole = *co;\n"
    "    (void)*o;\n"
    "    o->inner;\n"
    // A store of a constant has an integer constant's kind, its value's, and no fold: its own entry and its
    // conversion's.
    "    wide = local = 5;\n"
    "    o->inner.second = narrow = -1;\n"
    "    global_long = wide = local = 0;\n"
    "    flag = local = 3;\n"
    "    local = sizeof(a++);\n"
    "    wide = sizeof(o->inner.second) + sizeof(!a) + sizeof(&global) + sizeof((long)a);\n"
    "    wide = (long)sizeof(whole);\n"
    // An update's two rows. By name, a local's and a global's: each operator; `++` and `--`, prefix and postfix; a
    // narrow destination's, whose old value is converted and its result converted back; a floating one's; a
    // pointer's, its step's and a count's, added and subtracted.
    "    a++;\n"
    "    local += 2;\n"
    "    ++a;\n"
    "    a--;\n"
    "    --a;\n"
    "    local -= a;\n"
    "    local *= a;\n"
    "    local &= a;\n"
    "    local |= a;\n"
    "    local ^= a;\n"
    "    local <<= a;\n"
    "    local >>= a;\n"
    "    local /= a;\n"
    "    local %= a;\n"
    "    global++;\n"
    "    ++global;\n"
    "    global--;\n"
    "    global += a;\n"
    "    global_long -= b;\n"
    "    c++;\n"
    "    --c;\n"
    "    c += a;\n"
    "    h <<= 2;\n"
    "    u >>= 1;\n"
    "    u++;\n"
    "    ul += 5;\n"
    "    wide += a;\n"
    "    local += b;\n"
    "    value += d;\n"
    "    value++;\n"
    "    --single;\n"
    "    single += d;\n"
    "    local += d;\n"
    "    value -= a;\n"
    "    p++;\n"
    "    ++p;\n"
    "    p--;\n"
    "    --p;\n"
    "    p += a;\n"
    "    p -= a;\n"
    "    q += 2;\n"
    "    lp -= b;\n"
    // Through an address, prefix: its destination's own entries, then the address's DUP, the LOAD and the CONVERT.
    "    *p += a;\n"
    "    ++*p;\n"
    "    o->inner.second += b;\n"
    "    o->inner.first -= a;\n"
    "    --o->inner.third;\n"
    "    o->link->first *= a;\n"
    "    p[a] += 1;\n"
    "    list[a] |= 2;\n"
    "    whole.inner.second += 1;\n"
    "    whole.inner.first ^= a;\n"
    "    global_outer.inner.second -= b;\n"
    "    *lp <<= 1;\n"
    "    o->link->third += c;\n"
    // Through an address, postfix: the address and the old value kept in two temporaries, six entries, after the
    // destination's own: none (a dereference's, a member's at no offset), one (a subscript's, a local's and a
    // global's address) and a member's two; with the CONVERT where there is room.
    "    (*p)++;\n"
    "    o->inner.first++;\n"
    "    o->inner.second--;\n"
    "    p[a]++;\n"
    "    list[a]--;\n"
    "    whole.inner.first++;\n"
    "    global_outer.inner.first--;\n"
    "    (*lp)--;\n"
    "    o->link->first++;\n"
    "    (*(char*)p)++;\n"
    "    ((char*)p)[a]--;\n"
    // An update's value used: converted, and not; an update below another's, in an argument and in an index.
    "    local = a++;\n"
    "    local = ++a;\n"
    "    wide = local += 2;\n"
    "    local = (c += 1);\n"
    "    value = local++;\n"
    "    wide = (*p)++;\n"
    "    local = o->inner.second--;\n"
    "    local += a++;\n"
    "    p[a++] = 1;\n"
    "    list[local++ & 3] += 2;\n"
    "    callee(a++, b--);\n"
    "    *p++ = a;\n"
    "    local = *q--;\n"
    "    wide += local += a;\n"
    // A row of eight entries, an expression's last record's: a narrow destination's `++` (its CONST, the operation,
    // the CONVERT back and the store) whose value is converted to bool from its narrow class (four more).
    "    _Bool told = ++c;\n"
    // What it lists: a bool's update, whose result is tested; and a postfix update through an address whose
    // destination's own entries leave its first row no room (an address and its offset; a member's two and the
    // CONVERT).
    "    flag |= t;\n"
    "    whole.inner.second--;\n"
    "    global_outer.inner.second++;\n"
    "    o->inner.third++;\n"
    // An update that is not evaluated has the update's template and no row: a folded `&&`'s and `||`'s dead operand
    // (C lists the expression; the entries written for its records are pinned).
    "    local = 0 && a++;\n"
    "    local = 1 || --o->inner.first;\n"
    // A block's every lane: an update and its destination on either side of a block's end, and more than 16 updates
    // in a block (their list's second turn). By name with the CONVERT; through an address, postfix; and the two it
    // lists.
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    c++;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    (*p)++;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    o->inner.second += b;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    flag |= t;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    o->inner.third++;\n")
    "    if (a && local) local = 1;\n"
    "    if (!a) local = 2;\n"
    // A condition evaluated as branches. Each operand that is no `&&`, `||` or `!` is a value it tests: its test and
    // its branch are its conversion's part's row, by its class's width and by its sense, which comes from the
    // condition's root down. An `&&`'s first operand branches where it doesn't hold and an `||`'s where it does; a
    // second operand has its operator's sense, a `!`'s the other; a do's condition's is the other of an if's, a
    // while's and a for's.
    "    if (a || b) local = 3;\n"
    "    if (!a && b) local = 4;\n"
    "    if (a && !b) local = 5;\n"
    "    if (!a || !b) local = 6;\n"
    "    if ((a && b) || c) local = 7;\n"
    "    if (a && (b || c)) local = 8;\n"
    "    if ((a || b) && (c || h)) local = 9;\n"
    "    if ((a && b) || (c && h)) local = 10;\n"
    "    if (a && b && c && h) local = 11;\n"
    "    if (a || b || c || h) local = 12;\n"
    "    if (a || (b && (c || h))) local = 13;\n"
    "    if (a && (b || (c && !h))) local = 14;\n"
    "    if (!(a || b) && c) local = 15;\n"
    "    while (a && b) a--;\n"
    "    while (a || !b) b++;\n"
    "    do { a++; } while (a < 3 || b);\n"
    "    do { a++; } while (a < 6 && !b);\n"
    "    do { a++; } while ((a < 9 && b) || !c);\n"
    "    for (local = 0; local < 3 && a; local++) wide += local;\n"
    "    for (local = 0; a || local < 2; local++) a = 0;\n"
    // A tested value of each class: a narrow one's test has its CONVERT; a pointer's, a floating one's and a bool's;
    // a member's with its offset's entries and its LOAD; a call's, a comparison's; an update's, by name and through
    // an address, whose own row is before its test; and a constant's, folded where its operator's value is not
    // known.
    "    if (c && h) local = 16;\n"
    "    if (u || t) local = 17;\n"
    "    if (b && p) local = 18;\n"
    "    if (d || f) local = 19;\n"
    "    if (ul && lp) local = 20;\n"
    "    if (o->inner.first && o->link->third) local = 21;\n"
    "    if (o->inner.flag || whole.inner.second) local = 22;\n"
    "    if (p[a] && *lp) local = 23;\n"
    "    if (callee(a, b) && a < b) local = 24;\n"
    "    if (a == local || callee(c, a)) local = 25;\n"
    "    if (a++ && --b) local = 26;\n"
    "    if (c-- || (*p)++) local = 27;\n"
    "    if (o->inner.second-- && (c += 2)) local = 28;\n"
    "    if ((local = a) && (wide = b)) local = 29;\n"
    "    if (a + local && b - wide) local = 30;\n"
    "    if (a && 1) local = 31;\n"
    "    if (us && a) local = 36;\n"
    "    if (a || !c) local = 37;\n"
    "    if (!us || !u) local = 38;\n"
    // What it lists: a tested value with a conversion of its own has no room for its test. A const bool's read is
    // converted to bool, four entries: either operand's.
    "    if (cb && a) local = 32;\n"
    "    if (a || cb) local = 33;\n"
    "    if (!cb && co->inner.flag) local = 34;\n"
    // (One more, under a `do`: the fixture lists 67 expressions for an update's destination, and as many for this
    // made the two reasons' counts the same when a fault swapped the two.)
    "    do { local++; } while (local < 9 && cb);\n"
    // A bool's update that is a tested value lists its expression for its own row, the update's.
    "    if ((flag |= t) && a) local = 35;\n"
    // A block's every lane: a condition's records and their operands on either side of a block's end, and more than
    // 16 records evaluated as branches in a block (their list's second turn); with a narrow value's test of four
    // entries, a do's sense, and the one it lists.
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    if (a && c) local++;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    if ((a || !b) && (h || p)) local++;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    do { local++; } while (!a && (b || !c));\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    if (a && cb) local++;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    wide = o->inner.second;\n    flag = c;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    flag = o->link->third;\n")
    "    return local + (int)wide + narrow + flag + (int)value + (int)single + list[0] + *q + (int)*lp;\n"
    "}\n"
    "long returned(int a)\n"
    "{\n"
    "    return a;\n"
    "}\n"
    "_Bool tested(char c)\n"
    "{\n"
    "    return c;\n"
    "}\n"
    "long constant(void)\n"
    "{\n"
    "    return 3;\n"
    "}\n"
    "double floated(void)\n"
    "{\n"
    "    return 1;\n"
    "}\n"
    "unsigned long addressed(void)\n"
    "{\n"
    "    return (unsigned long)&global;\n"
    "}\n"
    "long double extended(int a)\n"
    "{\n"
    "    return a;\n"
    "}\n"
    "float single_of(long b)\n"
    "{\n"
    "    return b;\n"
    "}\n"
    // An aggregate's conversion to an aggregate is refused, and its value with no conversion is taken.
    "struct outer copied(const struct outer* co)\n"
    "{\n"
    "    return *co;\n"
    "}\n"
    "struct outer same(struct outer* o)\n"
    "{\n"
    "    return *o;\n"
    "}\n";

// The entries' values on the window, a line a source of a value (node_rows_window_values' lists and the numbering):
// - a constant's bits of each class, its own and a folded one's: a negative one's of a narrower class extended, a
//   wider one's cut to its target's, an unsigned one's not extended, and to bool; a floating one's of either class,
//   its zeros (whose lines say no floating value) and a negative zero;
// - an object's number: a local's, a parameter's and a global's read, store and update; an address constant's, with an
//   offset and without, and one of no object;
// - a member's offset; a pointer's stride of each element, an element past 4 GB (which its type's row doesn't hold)
//   and a pointer's update's, its step's 1 and its NEG;
// - an update's step of each class, a float's and a double's; a postfix update through an address's temporaries, by
//   each shape of its destination's own entries, and two and three of them in one expression;
// - a call's row: calls nested in arguments and in a callee, side by side, three deep, in a condition, and beside a
//   call that is not evaluated (a sizeof's operand);
// - a branch's label: the control's, an operator's own, two and three operators with a label, nested and side by
//   side, under each control and under `!`; with calls and with postfix updates as their tested values;
// - an expression of more than 64 records with its numbered records past the first 64, on both sides of them, and
//   with one alone; 64 statements each of calls, of labels and of temporaries, a block's end between every two of
//   their records in turn;
// - and a postfix update with no room for its first row, whose destination is its expression's first record, after a
//   statement whose tape is made: the mark of no room is no tested value's shape.
static const char node_rows_test_values_source[] =
    "struct pair { int first; long second; char third; _Bool flag; };\n"
    "struct outer { struct pair inner; struct pair* link; int list[4]; };\n"
    "struct far { char pad[4294967296]; int tail; long more; };\n"
    "extern int global;\n"
    "extern long global_long;\n"
    "extern double global_double;\n"
    "extern struct outer global_outer;\n"
    "extern struct far distant;\n"
    "extern int table[8];\n"
    "int callee(int a, long b);\n"
    "long wider(long a);\n"
    "int quiet(void);\n"
    "int (*picker(int which))(int, long);\n"
    "int values(int a, long b, char c, short h, unsigned char u, int* p, struct outer* o, double d, float f, long* lp, char* cp, char (*rows)[4294967296], unsigned short us,\n"
    "           unsigned long ul, _Bool t, unsigned int ui, signed char sc, struct far* fp)\n"
    "{\n"
    "    int local = 0;\n"
    "    long wide = 0;\n"
    "    char narrow = 0;\n"
    "    _Bool flag = 0;\n"
    "    double value = 0;\n"
    "    float single = 0;\n"
    "    int list[4];\n"
    "    struct outer whole;\n"
    "    int* q = 0;\n"
    // An integer constant of its own class, each class: its bits extended from its width by its sign.
    "    local = 7;\n"
    "    local = -1;\n"
    "    local = -2147483647 - 1;\n"
    "    ui = 5u;\n"
    "    ui = 4294967290u;\n"
    "    wide = 5l;\n"
    "    wide = -5l;\n"
    "    ul = 5ul;\n"
    "    ul = 18446744073709551600ul;\n"
    "    sc = (signed char)5;\n"
    "    sc = (signed char)-3;\n"
    "    u = (unsigned char)250;\n"
    "    h = (short)-7;\n"
    "    h = (short)7;\n"
    "    us = (unsigned short)65530;\n"
    // One before an entry of a narrower class: its bits are by its own entry's class, not the next one's.
    "    local = 300 + c;\n"
    "    local = 70000 + h;\n"
    "    wide = 4294967296l + a;\n"
    "    local = -200 + u;\n"
    // A folded one, its own class's bits and then its target's: a negative int's to a long and to an unsigned long,
    // an unsigned int's to a long (not extended), a long's to an int and to a char (cut), to each narrow class.
    "    wide = -1;\n"
    "    wide = 7;\n"
    "    ul = -1;\n"
    "    wide = 4294967295u;\n"
    "    ul = 4294967295u;\n"
    "    local = 4294967295l;\n"
    "    local = 2147483648l;\n"
    "    ui = -1;\n"
    "    ui = 4294967296l + 9;\n"
    "    sc = 200;\n"
    "    sc = -200;\n"
    "    u = 300;\n"
    "    u = -1;\n"
    "    h = 40000;\n"
    "    h = 70000;\n"
    "    us = 70000;\n"
    "    us = -1;\n"
    "    wide = (signed char)-3;\n"
    "    wide = (unsigned char)250;\n"
    "    ul = (short)-7;\n"
    "    local = (unsigned short)65530;\n"
    // To bool: whether it is not zero.
    "    flag = 0;\n"
    "    flag = 1;\n"
    "    flag = 2;\n"
    "    flag = 256;\n"
    "    flag = -1;\n"
    "    flag = 4294967296l;\n"
    "    flag = 0l;\n"
    // A floating constant of either class: its bits; a zero's, and a negative zero's.
    "    value = 2.5;\n"
    "    value = -2.5;\n"
    "    value = 0.1;\n"
    "    value = 1e300;\n"
    "    value = 0.0;\n"
    "    value = -0.0;\n"
    "    single = 1.5f;\n"
    "    single = -0.1f;\n"
    "    single = 0.0f;\n"
    "    single = -0.0f;\n"
    "    global_double = 6.0;\n"
    "    value = d + 0.5;\n"
    "    single = f * 3.0f;\n"
    // An object's number: a local's, a parameter's and a global's, read and stored.
    "    local = a;\n"
    "    a = local;\n"
    "    global = a;\n"
    "    local = global;\n"
    "    global_long = b;\n"
    "    b = global_long;\n"
    "    wide = local + global + a;\n"
    // An address constant's: an object's with no offset and with one, a local's and a global's; and no object's.
    "    q = &local;\n"
    "    q = &global;\n"
    "    q = &list[2];\n"
    "    q = &table[3];\n"
    "    lp = &whole.inner.second;\n"
    "    lp = &global_outer.inner.second;\n"
    "    q = list;\n"
    "    q = table;\n"
    "    q = (int*)16;\n"
    "    q = (int*)0;\n"
    "    cp = (char*)-1;\n"
    "    lp = &((struct pair*)0)->second;\n"
    "    wide = (long)&table[5];\n"
    "    wide = (long)&list[1];\n"
    "    local = (int)(long)&global;\n"
    // A member's offset in bytes, and its sum's 1.
    "    local = o->inner.first;\n"
    "    wide = o->inner.second;\n"
    "    narrow = o->link->third;\n"
    "    local = o->list[2];\n"
    "    o->inner.second = b;\n"
    "    o->link->third = c;\n"
    "    lp = &o->inner.second;\n"
    // And one past 4 GB, a pointer's member's and a global's: a value's upper half.
    "    local = fp->tail;\n"
    "    fp->more = b;\n"
    "    lp = &fp->more;\n"
    "    local = distant.tail;\n"
    "    lp = &distant.more;\n"
    "    fp->tail++;\n"
    // A pointer's stride: each element's, a difference's NEG before it, and an element past 4 GB.
    "    q = p + a;\n"
    "    q = p - a;\n"
    "    q = a + p;\n"
    "    local = p[a];\n"
    "    lp = lp + a;\n"
    "    wide = lp[a];\n"
    "    cp = cp + a;\n"
    "    narrow = cp[a];\n"
    "    o = o + a;\n"
    "    local = o[a].list[1];\n"
    "    cp = (char*)(rows + a);\n"
    "    cp = (char*)(rows - a);\n"
    "    cp = rows[a];\n"
    "    narrow = rows[a][b];\n"
    "    local = table[a];\n"
    "    local = list[a];\n"
    // An update by name: its read's and its store's object, a local's, a parameter's and a global's; its step, one
    // of the class it is computed in.
    "    a++;\n"
    "    --a;\n"
    "    local += 2;\n"
    "    local -= a;\n"
    "    global++;\n"
    "    --global;\n"
    "    global_long -= b;\n"
    "    b += global_long;\n"
    "    c++;\n"
    "    u--;\n"
    "    h += a;\n"
    "    wide++;\n"
    "    ul--;\n"
    "    value++;\n"
    "    --value;\n"
    "    single++;\n"
    "    --single;\n"
    "    value += d;\n"
    "    single -= d;\n"
    "    global_double++;\n"
    "    local = a++;\n"
    "    wide = global--;\n"
    "    value = single++;\n"
    // A pointer's update: its stride, its step's 1 and its NEG.
    "    p++;\n"
    "    ++p;\n"
    "    p--;\n"
    "    --p;\n"
    "    p += a;\n"
    "    p -= a;\n"
    "    lp++;\n"
    "    lp -= b;\n"
    "    cp++;\n"
    "    cp += a;\n"
    "    o++;\n"
    "    o -= a;\n"
    "    rows++;\n"
    "    rows--;\n"
    "    rows += a;\n"
    "    rows -= a;\n"
    "    q = p++;\n"
    // Through an address: an access's value is zero; a prefix one's step; a postfix one's two temporaries, after
    // each shape of its destination's own entries.
    "    *p += a;\n"
    "    ++*p;\n"
    "    --*lp;\n"
    "    o->inner.second += b;\n"
    "    --o->inner.third;\n"
    "    p[a] += 1;\n"
    "    ++p[a];\n"
    "    ++lp[a];\n"
    "    (*p)++;\n"
    "    (*lp)--;\n"
    "    o->inner.first++;\n"
    "    o->inner.second--;\n"
    "    p[a]++;\n"
    "    lp[a]--;\n"
    "    cp[a]++;\n"
    "    list[a]--;\n"
    "    whole.inner.first++;\n"
    "    global_outer.inner.first--;\n"
    "    o->link->first++;\n"
    "    local = (*p)++;\n"
    "    wide = o->inner.second--;\n"
    // Two and three postfix updates through an address in one expression: their temporaries are numbered in the
    // order of their destinations' records.
    "    local = (*p)++ + (*q)++;\n"
    "    local = (*q)-- - (*p)--;\n"
    "    wide = (*lp)-- + o->inner.second++ + p[a]++;\n"
    "    local = p[a]++ + list[a]-- + whole.inner.first++ + o->link->first--;\n"
    "    callee((*p)++, (*lp)--);\n"
    "    p[(*q)++] = (*p)--;\n"
    // A call's row: its preorder number among its expression's evaluated calls.
    "    callee(a, b);\n"
    "    local = callee(a, b);\n"
    "    local = callee(callee(a, b), b);\n"
    "    local = callee(a, wider(b));\n"
    "    local = callee(callee(a, b), wider(b));\n"
    "    local = callee(a, b) + callee(c, a);\n"
    "    local = callee(a, b) + callee(c, a) + callee(h, b);\n"
    "    local = callee(callee(callee(a, b), b), b);\n"
    "    wide = wider(wider(wider(b)) + wider(a));\n"
    "    wide = wider(callee(a, wider(b)) + callee(callee(c, a), wider(wider(b))));\n"
    "    local = picker(a)(a, b);\n"
    "    local = picker(a)(callee(a, b), b);\n"
    "    local = picker(callee(a, b))(callee(c, a), wider(b));\n"
    "    p[callee(a, b)] = callee(c, a);\n"
    "    local += callee(a, b) + callee(c, a);\n"
    // An expression with two calls that C leaves (a conditional's), between two it takes: each one's numbers are by
    // its own verdict, whatever the next one's and the one's before.
    "    local = callee(a, b) + callee(h, a);\n"
    "    local = a ? callee(a, b) + callee(c, a) : 4;\n"
    "    local = callee(h, b) + callee(c, a);\n"
    // A call with no argument is its expression's second record, and two records after the end of a condition
    // before it, which has no call: one such call, two and three.
    "    if (a) quiet();\n"
    "    while (b) local = quiet() + quiet();\n"
    "    if (c) local = quiet() + callee(a, b) + quiet();\n"
    "    if (h) return quiet();\n"
    // An expression with two calls that the rows list (a constant to a floating class): its numbers are not made.
    "    value = d + 2 + callee(a, b) + callee(c, a);\n"
    // A call that is not evaluated is no call of its expression's: one evaluated call beside it is its first.
    "    local = (int)sizeof(callee(a, b)) + callee(c, a);\n"
    "    local = (int)sizeof(callee(callee(a, b), b)) + (int)sizeof(wider(b));\n"
    "    local = callee(a, (long)sizeof(wider(b))) + callee(c, (long)sizeof(callee(a, b)));\n"
    // A branch's label: the control's, for a value no operator owns.
    "    if (a && b) local = 1;\n"
    "    if (a || b) local = 2;\n"
    "    while (a && !b) a--;\n"
    "    do { a++; } while (a < 3 || b);\n"
    "    for (local = 0; local < 3 && a; local++) wide += local;\n"
    // An operator's own: an `&&` branched where true and an `||` where false own their first operand's branch. One
    // such operator's label is its expression's first.
    "    if ((a || b) && c) local = 3;\n"
    "    if ((a && b) || c) local = 4;\n"
    "    if (a && (b || c)) local = 5;\n"
    "    do { a++; } while ((a < 6 && b) || c);\n"
    "    do { a++; } while (a < 9 && (b || c));\n"
    // Two and three with a label: side by side, nested, three deep, and under `!`.
    "    if ((a || b) && (c || h)) local = 6;\n"
    "    if ((a && b) || (c && h)) local = 7;\n"
    "    if ((a || b) && (c || h) && (u || t)) local = 8;\n"
    "    if (((a || b) && c) || h) local = 9;\n"
    "    if ((a || (b && (c || h))) && u) local = 10;\n"
    "    if ((a && (b || (c && h))) || (us && (ul || t))) local = 11;\n"
    "    if (!(a && b) && !(c && h)) local = 12;\n"
    "    if (!((a || b) && (c || h)) || u) local = 13;\n"
    "    while ((a || b) && (c || h)) a--;\n"
    "    do { a++; } while ((a < 12 && b) || (c && h) || (u && t));\n"
    "    for (local = 0; (local < 3 || a) && (b || c); local++) wide += local;\n"
    "    if ((a || b) && ((c || h) && (u || t)) && ((us || ul) && (d || f))) local = 14;\n"
    // An operator with no label of its own between a tested value and its owner, the expression's second operator
    // with a label: the owner comes down through it.
    "    if ((h || u) && ((a || b) || c)) local = 23;\n"
    "    do { a++; } while ((h && u) || ((a < 30 && b) && c));\n"
    "    if ((h || u) && (!a || c)) local = 24;\n"
    // With calls, and with postfix updates through an address, as their tested values: each numbered in its turn.
    "    if (callee(a, b) && callee(c, a)) local = 15;\n"
    "    if (callee(a, b) || (callee(c, a) && wider(b))) local = 16;\n"
    "    if ((a || callee(a, b)) && c) local = 17;\n"
    "    if ((callee(a, b) || wider(b)) && (callee(c, a) || wider(a))) local = 18;\n"
    "    if ((*p)++ && (*q)--) local = 19;\n"
    "    if (((*p)++ || b) && ((*lp)-- || callee(a, b))) local = 20;\n"
    "    if (callee(callee(a, b), b) && a) local = 21;\n"
    // More than 64 records: its numbered records past the first 64, on both sides of them, and one alone.
    "    local = " BUSTER_NODE_CONTEXTS_TEST_SUM " + callee(a, b) + callee(c, a);\n"
    "    local = callee(a, b) + " BUSTER_NODE_CONTEXTS_TEST_SUM " + callee(c, a);\n"
    "    local = callee(a, b) + callee(c, a) + " BUSTER_NODE_CONTEXTS_TEST_SUM ";\n"
    "    local = " BUSTER_NODE_CONTEXTS_TEST_SUM " + callee(c, a);\n"
    "    local = " BUSTER_NODE_CONTEXTS_TEST_SUM " + " BUSTER_NODE_CONTEXTS_TEST_SUM " + callee(a, b) + callee(c, a) + callee(h, b);\n"
    "    local = callee(a, b) + " BUSTER_NODE_CONTEXTS_TEST_SUM " + callee(c, a) + " BUSTER_NODE_CONTEXTS_TEST_SUM " + callee(h, b);\n"
    "    if ((a || b) && " BUSTER_NODE_CONTEXTS_TEST_SUM " && (c || h)) local = 22;\n"
    // A postfix update with no room for its first row lists its expression: its destination, the expression's first
    // record, has the mark of no room and no entry, after a statement whose tape is made.
    "    local = a;\n"
    "    whole.inner.second--;\n"
    "    wide = b;\n"
    "    global_outer.inner.second++;\n"
    "    local = callee(a, b);\n"
    "    o->inner.third++;\n"
    // A block's every lane: a block's end between every two records in turn, for each list and for the numbered.
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    local = callee(callee(a, b), b);\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    if ((a || b) && (c || h)) local++;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    local = (*p)++ + (*q)++;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    wide = o->inner.second + 300;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    q = p - a;\n    global += 2;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    value = 2.5;\n    lp = &global_outer.inner.second;\n    p++;\n")
    BUSTER_NODE_CONTEXTS_TEST_BLOCK("    if (a && b) flag = 3;\n")
    "    return local + (int)wide + narrow + flag + (int)value + (int)single + list[0] + *q + (int)*lp + whole.inner.first + (int)ul + ui + sc + us;\n"
    "}\n"
    "long returned(void)\n"
    "{\n"
    "    return -3;\n"
    "}\n"
    "double floated(void)\n"
    "{\n"
    "    return 2.5;\n"
    "}\n"
    "int* addressed(void)\n"
    "{\n"
    "    return &table[2];\n"
    "}\n"
    "_Bool told(void)\n"
    "{\n"
    "    return 5;\n"
    "}\n";

// A unit of one chunk with one update: the updates' sweep runs for a chunk that has any.
static const char node_rows_test_single_source[] =
    "int single(int a, int b)\n"
    "{\n"
    "    a++;\n"
    "    b = a;\n"
    "    return b;\n"
    "}\n";

// And one with one condition evaluated as branches and no update: the sweep runs for a chunk that has either.
static const char node_rows_test_branch_source[] =
    "int branch(int a, char b)\n"
    "{\n"
    "    if (a && b)\n"
    "    {\n"
    "        return 1;\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

// A summary's side facts' fixture (node_rows_window_summary), a line a case of each fact.
// `references`: no object (a local, an enumerator); a global once and twice in one expression; a static object beside
// it; an array read at a variable index, at a constant one, decayed and by an element's address; an address constant;
// a record's members, two of one object; a function called, with a global for an argument, beside another call; a
// function's name as a value and by its address; a call through a pointer, the pointer its reference; a function's
// static object; a static function; an array of pointers; a string literal subscripted, cast and dereferenced; a call
// with no value; and a condition's, a loop's, a `for` statement's clauses' and a `switch`'s.
// `facts`: a plain call; calls through a pointer, a member, a pointer dereferenced and a conditional; a function's name
// dereferenced and by its address, which are the function; an unknown in an argument, an arm, an operand, a condition
// and a comma; a call that stops, by the attribute and by the keyword; a conditional with one arm that stops, with
// both and with a condition that does; `&&` and `||` with an operand that stops on either side; an argument that
// stops, and one with an arm that does; an arm that stops beside one that is unknown, on either side; both beside an
// unknown; an unknown before a stop with no operator that selects; an `&&`'s second operand that is unknown over an
// argument that stops; an arm that stops past a block's records from its expression's first; a call the kernel does
// not place (a `sizeof`'s operand's, which the front leaves to the walk); and the library's names the flow leaves
// unknown.
// `truths`: a branch's condition whose second operand's truth is known, of each operator, under a `!` and under
// another operator, and an address constant's; a value's with one; conditions with none, an object's lvalue for a
// second operand among them;
// constants a statement of each kind tests; a folded comma for a condition, of each truth; and a `switch`'s selector.
// `passed`: casts of an address through integer types, with consumers that test their truth or use their value;
// casts of variables and constants, to void, and with effects; and a boolean cast of an address with an object.
// `lanes`: a statement of eleven records 64 times, with a plain call, a call through a pointer and four references;
// two conditionals with an arm that stops 64 times each, of fifteen records and of eleven, one unknown by its other
// arm with a string's reference and one with no fact, in chunks one after another (the reduction's bytes of one
// chunk's records are the next's to clear); and a call that stops 64 times, three records each: each is at every
// lane of a block. And two calls whose callee is before the block before, a plain one and one through a pointer.
static const char node_summary_test_source[] =
    "struct S { long m; int a[4]; int (*cb)(int); struct S* next; };\n"
    "enum E { E0, E1 = 5 };\n"
    "extern int g;\n"
    "static int sg;\n"
    "int ga[8];\n"
    "struct S gs;\n"
    "extern const char* names[4];\n"
    "int f(int);\n"
    "int h(int, int);\n"
    "void w(void);\n"
    "void stop(void) __attribute__((noreturn));\n"
    "_Noreturn void quit(int);\n"
    "void exit(int);\n"
    "void abort(void);\n"
    "int (*fp)(int);\n"
    "static int own(int x) { return x; }\n"
    "int references(struct S* p, int a, int* q)\n"
    "{\n"
    "    static int counter;\n"
    "    int local = a;\n"
    "    local = a + 1;\n"
    "    a = g;\n"
    "    a = g + g;\n"
    "    a = sg + g;\n"
    "    a = ga[a];\n"
    "    a = ga[1];\n"
    "    q = &g;\n"
    "    q = ga;\n"
    "    q = &ga[2];\n"
    "    a = gs.a[a] + (int)gs.m;\n"
    "    p = &gs;\n"
    "    p = gs.next;\n"
    "    a = f(a);\n"
    "    a = f(g) + h(sg, a);\n"
    "    fp = f;\n"
    "    fp = &f;\n"
    "    a = fp(a);\n"
    "    a = counter;\n"
    "    counter = a;\n"
    "    a = E1 + a;\n"
    "    a = own(a);\n"
    "    a = names[a] != 0;\n"
    "    a = \"abc\"[a];\n"
    "    q = (int*)\"abcd\";\n"
    "    a = *\"x\";\n"
    "    w();\n"
    "    if (g)\n"
    "    {\n"
    "        a = sg;\n"
    "    }\n"
    "    while (ga[a] != 0)\n"
    "    {\n"
    "        a += 1;\n"
    "    }\n"
    "    for (a = g; a < sg; a += 1)\n"
    "    {\n"
    "        local += ga[a];\n"
    "    }\n"
    "    switch (g)\n"
    "    {\n"
    "        case 1: a = 2;\n"
    "    }\n"
    "    return a + local;\n"
    "}\n"
    "int facts(struct S* p, int a, int c)\n"
    "{\n"
    "    a = f(a);\n"
    "    a = fp(a);\n"
    "    a = p->cb(a);\n"
    "    a = (*fp)(a);\n"
    "    a = (*f)(a);\n"
    "    a = (&f)(a);\n"
    "    a = (c ? f : fp)(a);\n"
    "    a = h(f(a), fp(a));\n"
    "    a = c ? f(a) : fp(a);\n"
    "    a = c && fp(a);\n"
    "    a = fp(a) || c;\n"
    "    a = (fp(a), c);\n"
    "    if (c)\n"
    "    {\n"
    "        stop();\n"
    "    }\n"
    "    if (c)\n"
    "    {\n"
    "        quit(1);\n"
    "    }\n"
    "    a = c ? (stop(), a) : 2;\n"
    "    a = c ? (stop(), a) : (quit(2), c);\n"
    "    a = (stop(), c) ? 1 : 2;\n"
    "    a = c && (stop(), a);\n"
    "    a = (stop(), c) && a;\n"
    "    a = c || (quit(1), a);\n"
    "    a = h((stop(), c), a);\n"
    "    a = h(a, c ? (stop(), a) : 2);\n"
    "    a = c ? (stop(), a) : fp(a);\n"
    "    a = (c ? (stop(), a) : (stop(), c)) + fp(a);\n"
    "    a = f((stop(), a));\n"
    "    a = f(a) + (c ? (stop(), a) : (quit(1), c));\n"
    "    a = fp(a) + f((stop(), a));\n"
    "    a = c ? fp(a) : (stop(), a);\n"
    "    a = c && fp((stop(), a));\n"
    "    a = (a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a) + (c ? (stop(), a) : 2);\n"
    "    a = (int)sizeof(fp(a)) + a;\n"
    "    if (c)\n"
    "    {\n"
    "        exit(1);\n"
    "    }\n"
    "    if (c)\n"
    "    {\n"
    "        abort();\n"
    "    }\n"
    "    return a;\n"
    "}\n"
    "int truths(int a, int c)\n"
    "{\n"
    "    if (a && 0)\n"
    "    {\n"
    "        c = 1;\n"
    "    }\n"
    "    if (a || 1)\n"
    "    {\n"
    "        c = 2;\n"
    "    }\n"
    "    if (a && c)\n"
    "    {\n"
    "        c = 3;\n"
    "    }\n"
    "    if (a && g)\n"
    "    {\n"
    "        c = 13;\n"
    "    }\n"
    "    if (a || &g)\n"
    "    {\n"
    "        c = 14;\n"
    "    }\n"
    "    c = a || sg;\n"
    "    while (a || 0)\n"
    "    {\n"
    "        a = 0;\n"
    "    }\n"
    "    if (1)\n"
    "    {\n"
    "        c = 4;\n"
    "    }\n"
    "    if (0)\n"
    "    {\n"
    "        c = 5;\n"
    "    }\n"
    "    while (1)\n"
    "    {\n"
    "        break;\n"
    "    }\n"
    "    do\n"
    "    {\n"
    "        c = 6;\n"
    "    } while (0);\n"
    "    if (!(a && 0))\n"
    "    {\n"
    "        c = 7;\n"
    "    }\n"
    "    c = a && 0;\n"
    "    c = a || 1;\n"
    "    c = a && 1;\n"
    "    if ((a, 1))\n"
    "    {\n"
    "        c = 8;\n"
    "    }\n"
    "    if ((a, 0))\n"
    "    {\n"
    "        c = 9;\n"
    "    }\n"
    "    switch (a)\n"
    "    {\n"
    "        case 1: c = 10;\n"
    "    }\n"
    "    if (c == 3)\n"
    "    {\n"
    "        c = 11;\n"
    "    }\n"
    "    if (a && (c || 1))\n"
    "    {\n"
    "        c = 12;\n"
    "    }\n"
    "    return c;\n"
    "}\n"
    "int passed(int a, int c, int* q)\n"
    "{\n"
    "    if ((char)(long)&g)\n"
    "    {\n"
    "        c = 1;\n"
    "    }\n"
    "    c = !(char)(long)&sg;\n"
    "    c = (int)(char)(long)ga;\n"
    "    c = a || (char)(long)&g;\n"
    "    c = (char)(long)&g;\n"
    "    c = (char)(long)&g + a;\n"
    "    if ((char)a)\n"
    "    {\n"
    "        c = 2;\n"
    "    }\n"
    "    if ((char*)&g)\n"
    "    {\n"
    "        c = 3;\n"
    "    }\n"
    "    if (&g != &sg)\n"
    "    {\n"
    "        c = 4;\n"
    "    }\n"
    "    (void)(void)0;\n"
    "    c = !(long)(q = ga);\n"
    "    c = (_Bool)&g;\n"
    "    c = (_Bool)(long)ga + a;\n"
    "    return c + (q != 0);\n"
    "}\n"
    "int lanes(int a, int c)\n"
    "{\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = f(-g) + fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), \"xy\"[a]) : -fp(sg);\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    a = c ? (stop(), a) : -sg;\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    stop();\n"
    "    a = h(a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a, c);\n"
    "    a = fp(a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a);\n"
    "    return a;\n"
    "}\n";

// And a unit with a function the return flow leaves unknown from one of its declarations on
// (check_program_return_unknowns): no call of such a unit's is a plain one, and each is asked a record at a time. A
// call of the function before that declaration has no fact and one after it is unknown; a function with the attribute
// at its first declaration is unknown at every call.
static const char node_summary_test_unknowns_source[] =
    "int f(int);\n"
    "void later(int);\n"
    "void marked(int) __attribute__((analyzer_noreturn));\n"
    "void stop(void) __attribute__((noreturn));\n"
    "int (*fp)(int);\n"
    "int before(int a, int c)\n"
    "{\n"
    "    later(a);\n"
    "    marked(a);\n"
    "    a = f(a);\n"
    "    a = fp(a);\n"
    "    if (c)\n"
    "    {\n"
    "        stop();\n"
    "    }\n"
    "    a = f(a) + f(c);\n"
    "    return a;\n"
    "}\n"
    "void later(int) __attribute__((analyzer_noreturn));\n"
    "int after(int a, int c)\n"
    "{\n"
    "    later(a);\n"
    "    marked(a);\n"
    "    a = f(a);\n"
    "    a = f(a) + (later(c), c);\n"
    "    a = c ? (stop(), a) : (later(a), c);\n"
    "    if (c)\n"
    "    {\n"
    "        stop();\n"
    "    }\n"
    "    return a;\n"
    "}\n";

// The rows' tables against what the rows' write makes of them (node_rows_window_write): a shape's entries are as many
// as its length, its first slots' but for an update's rows, whose entries have fixed slots; each a typed kind's
// number, or one of an update's own row's two marks in its one slot; a class's source is one the write resolves in
// that slot of that part (in a record's own part its own class's in any slot, its target's in the first, its first
// operand's conversion's in the first two; in its conversion's part its own class's in any slot, its target's in
// the first two, that operand's in the last). A postfix update's destination's two parts, by the shape of its own
// entries: those entries and the first row's six, and its CONVERT, in the eight slots, or the mark of no room for
// both. An operator's value's rows: a logical operator's own four entries, and a conditional's first arm's jump after
// each conversion the arms' table gives it a row for. And void's word, which the reads don't read.
static bool node_rows_test_tables(void)
{
    NodeRowsTables tables;
    node_rows_tables(&tables);
    // The shapes of a conversion's part: a conversion's four, an update's first row's CONVERT by name and its rows
    // through an address, a tested value's test and branch, and a conditional's first arm's jump.
    static const uint8_t conversions[] = {
        BUSTER_TYPED_DISCARD,
        BUSTER_NODE_ROWS_BOOL,
        BUSTER_NODE_ROWS_BOOL + 1,
        BUSTER_NODE_ROWS_CONVERT,
        BUSTER_NODE_ROWS_FIRST_CONVERT,
        BUSTER_NODE_ROWS_FIRST_ADDRESS,
        BUSTER_NODE_ROWS_FIRST_ADDRESS + 1,
        BUSTER_NODE_ROWS_KEPT_REST,
        BUSTER_NODE_ROWS_KEPT_REST + 1,
        BUSTER_NODE_ROWS_KEPT_REST + 2,
        BUSTER_NODE_ROWS_KEPT_REST + 3,
        BUSTER_NODE_ROWS_KEPT_REST + 4,
        BUSTER_NODE_ROWS_TESTED,
        BUSTER_NODE_ROWS_TESTED + 1,
        BUSTER_NODE_ROWS_TESTED + 2,
        BUSTER_NODE_ROWS_TESTED + 3,
        BUSTER_NODE_ROWS_ARM,
        BUSTER_NODE_ROWS_ARM + 1,
        BUSTER_NODE_ROWS_ARM + 2,
        BUSTER_NODE_ROWS_ARM + 3,
        BUSTER_NODE_ROWS_ARM + 4,
    };
    uint32_t wrong = 0;
    uint32_t rows = 0;
    for (uint32_t shape = 0; shape < 128; shape += 1)
    {
        bool conversion = false;
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(conversions); c += 1)
        {
            conversion = conversion || conversions[c] == shape;
        }
        const bool fixed = shape >= BUSTER_NODE_ROWS_UPDATE && shape < BUSTER_NODE_ROWS_TESTED;
        const bool own_row = shape >= BUSTER_NODE_ROWS_UPDATE && shape < BUSTER_NODE_ROWS_FIRST;
        uint32_t length = 0;
        bool ended = false;
        for (uint32_t slot = 0; slot < BUSTER_NODE_ROWS_SLOTS; slot += 1)
        {
            const uint8_t kind = tables.kinds[slot][shape];
            const uint8_t source = tables.sources[slot][shape];
            const bool entry = kind != 0;
            const bool operation = kind == BUSTER_NODE_ROWS_KIND_OPERATION && slot == 1 && own_row && shape < BUSTER_NODE_ROWS_UPDATE_POINTER;
            const bool store = kind == BUSTER_NODE_ROWS_KIND_STORE && slot == 3 && own_row;
            const bool resolved = source < BUSTER_TYPED_CLASSES || source == BUSTER_NODE_ROWS_CLASS_OWN ||
                                  (source == BUSTER_NODE_ROWS_CLASS_TARGET && (slot == 0 || (conversion && slot == 1))) ||
                                  (source == BUSTER_NODE_ROWS_CLASS_OPERAND && (conversion ? slot == BUSTER_NODE_ROWS_SLOTS - 1 : slot <= 1));
            wrong += entry && ended && !fixed;
            wrong += kind >= BUSTER_NODE_ROWS_TABLE && !operation && !store;
            wrong += !entry && source != 0;
            wrong += !resolved;
            length += entry;
            ended = ended || !entry;
        }
        wrong += tables.lengths[shape] != length;
        // An update's own row has its store, and one that is no pointer's its operation.
        wrong += own_row && (tables.kinds[3][shape] != BUSTER_NODE_ROWS_KIND_STORE ||
                             (tables.kinds[1][shape] == BUSTER_NODE_ROWS_KIND_OPERATION) != (shape < BUSTER_NODE_ROWS_UPDATE_POINTER));
        // An operator's evaluated as branches has no entry, nor has a conditional's own part, nor a shape past the
        // table's rows, the mark of a template whose rows are not made here among them.
        wrong += ((shape >= BUSTER_NODE_ROWS_BRANCHED && shape <= BUSTER_NODE_ROWS_HELD) || shape >= BUSTER_NODE_ROWS_SHAPES) && length != 0;
        wrong += shape != 0 && shape < BUSTER_NODE_ROWS_TABLE && (length != 1 || tables.kinds[0][shape] != shape || tables.sources[0][shape] != BUSTER_NODE_ROWS_CLASS_OWN);
        rows += shape >= BUSTER_NODE_ROWS_TABLE && length != 0;
    }
    // A postfix update's destination through its address: of the shapes of an address's own entries, five have room
    // for the first row's six (a member's two without the CONVERT alone), nine pairs in all.
    uint32_t kept = 0;
    for (uint32_t base = 0; base < 128; base += 1)
    {
        for (uint32_t converted = 0; converted < 2; converted += 1)
        {
            const uint8_t own = tables.kept_owns[converted][base];
            const uint8_t rest = tables.kept_conversions[converted][base];
            const bool made = own != BUSTER_NODE_ROWS_LATER;
            wrong += made != (rest != BUSTER_NODE_ROWS_LATER);
            if (made && rest != BUSTER_NODE_ROWS_LATER)
            {
                wrong += own < BUSTER_NODE_ROWS_KEPT || own >= BUSTER_NODE_ROWS_KEPT_REST || rest < BUSTER_NODE_ROWS_KEPT_REST || rest >= BUSTER_NODE_ROWS_TESTED;
                wrong += tables.lengths[own & 127] != BUSTER_NODE_ROWS_SLOTS || tables.lengths[own & 127] + tables.lengths[rest & 127] != tables.lengths[base] + 6 + converted;
                for (uint32_t slot = 0; slot < tables.lengths[base] && slot < BUSTER_NODE_ROWS_SLOTS; slot += 1)
                {
                    wrong += tables.kinds[slot][own & 127] != tables.kinds[slot][base] || tables.sources[slot][own & 127] != tables.sources[slot][base];
                }
                kept += 1;
            }
        }
    }
    wrong += kept != 9;
    // A tested value's rows: the test of its class (a narrow one's CONVERT first, its CONST then I32's; another's
    // CONST the record's own), NE, and last the branch of its sense.
    for (uint32_t narrow = 0; narrow < 2; narrow += 1)
    {
        for (uint32_t sense = 0; sense < 2; sense += 1)
        {
            const uint32_t shape = BUSTER_NODE_ROWS_TESTED + 2 * narrow + sense;
            const uint32_t length = 3 + narrow;
            wrong += tables.lengths[shape] != length;
            wrong += narrow != 0 && (tables.kinds[0][shape] != BUSTER_TYPED_CONVERT || tables.sources[0][shape] != BUSTER_TYPED_CLASS_I32);
            wrong += tables.kinds[narrow][shape] != BUSTER_TYPED_CONST || tables.sources[narrow][shape] != (narrow != 0 ? BUSTER_TYPED_CLASS_I32 : BUSTER_NODE_ROWS_CLASS_OWN);
            wrong += tables.kinds[narrow + 1][shape] != BUSTER_TYPED_NE || tables.sources[narrow + 1][shape] != BUSTER_TYPED_CLASS_I32;
            wrong += tables.kinds[narrow + 2][shape] != (sense != 0 ? BUSTER_TYPED_BRANCH_TRUE : BUSTER_TYPED_BRANCH_FALSE) || tables.sources[narrow + 2][shape] != BUSTER_TYPED_CLASS_I32;
        }
    }
    // A logical operator's value's own row: its second operand's conversion's class's test, its jump, of its own
    // class, and its value at its alternate.
    for (uint32_t either = 0; either < 2; either += 1)
    {
        const uint32_t shape = BUSTER_NODE_ROWS_LOGIC + either;
        wrong += tables.lengths[shape] != 4;
        wrong += tables.kinds[0][shape] != BUSTER_TYPED_CONST || tables.sources[0][shape] != BUSTER_NODE_ROWS_CLASS_OPERAND;
        wrong += tables.kinds[1][shape] != BUSTER_TYPED_NE || tables.sources[1][shape] != BUSTER_TYPED_CLASS_I32;
        wrong += tables.kinds[2][shape] != BUSTER_TYPED_JUMP_VALUE || tables.sources[2][shape] != BUSTER_NODE_ROWS_CLASS_OWN;
        wrong += tables.kinds[3][shape] != BUSTER_TYPED_CONST || tables.sources[3][shape] != BUSTER_TYPED_CLASS_I32;
    }
    // A conditional's first arm's part with its jump, by the arms' table: six rows of sixteen. A void conditional's
    // arm ends on a JUMP of void's class, after nothing or a DISCARD; another's on a JUMP_VALUE, of the arm's own
    // class where the reads read no target's and of its target's where they did, after nothing or a CONVERT.
    uint32_t arms = 0;
    for (uint32_t index = 0; index < 16; index += 1)
    {
        const uint32_t shape = tables.arms[index];
        const bool voided = (index & 8) != 0;
        const bool worded = (index & 4) != 0;
        const uint32_t conversion = index & 3;
        if (shape != 0)
        {
            const uint32_t length = tables.lengths[shape & 127];
            const uint8_t last_kind = tables.kinds[(length - 1) & 3][shape & 127];
            const uint8_t last_source = tables.sources[(length - 1) & 3][shape & 127];
            wrong += shape < BUSTER_NODE_ROWS_ARM || shape >= BUSTER_NODE_ROWS_SHAPES;
            wrong += length != (conversion == 0 ? 1u : 2u) || conversion == 3;
            wrong += last_kind != (voided ? BUSTER_TYPED_JUMP : BUSTER_TYPED_JUMP_VALUE);
            wrong += last_source != (voided ? BUSTER_TYPED_CLASS_VOID : worded || conversion == 1 ? BUSTER_NODE_ROWS_CLASS_TARGET : BUSTER_NODE_ROWS_CLASS_OWN);
            wrong += conversion == 1 && (voided || tables.kinds[0][shape & 127] != BUSTER_TYPED_CONVERT || tables.sources[0][shape & 127] != BUSTER_NODE_ROWS_CLASS_TARGET);
            wrong += conversion == 2 && (!voided || worded || tables.kinds[0][shape & 127] != BUSTER_TYPED_DISCARD || tables.sources[0][shape & 127] != BUSTER_NODE_ROWS_CLASS_OWN);
            wrong += voided && worded;
            arms += 1;
        }
    }
    wrong += arms != 6;
    // The order the write reads a block's records in is a permutation of them.
    uint64_t seen = 0;
    for (uint32_t i = 0; i < 64; i += 1)
    {
        wrong += tables.order[i] >= 64;
        seen |= UINT64_C(1) << (tables.order[i] & 63);
    }
    wrong += seen != ~UINT64_C(0);
    // A block's records' places, which the updates' sweep compresses into their list.
    for (uint32_t i = 0; i < 64; i += 1)
    {
        wrong += tables.places[i] != i;
    }
    // A template's own shape: one entry's kind for the seven a template alone gives; the mark for the two whose rows
    // are not made here, which C on the window makes of no record (a logical operator's with a condition whose truth
    // is known: no fixture reaches their bytes); and none for the others, whose cases the shapes' sweep makes, an
    // update's among them, which the updates' loop makes, an operator's evaluated as branches, which writes no
    // entry, and an operator's value's, whose own part the branches' loop writes.
    static const uint8_t template_shapes[][2] = {
        { CHECK_LOWER_TEMPLATE_CONSTANT, BUSTER_TYPED_CONST },
        { CHECK_LOWER_TEMPLATE_LOCAL, BUSTER_TYPED_LOCAL },
        { CHECK_LOWER_TEMPLATE_GLOBAL, BUSTER_TYPED_GLOBAL },
        { CHECK_LOWER_TEMPLATE_STORE_LOCAL, BUSTER_TYPED_STORE_LOCAL },
        { CHECK_LOWER_TEMPLATE_STORE_GLOBAL, BUSTER_TYPED_STORE_GLOBAL },
        { CHECK_LOWER_TEMPLATE_STORE, BUSTER_TYPED_STORE },
        { NODE_MACHINE_TEMPLATE_CALL, BUSTER_TYPED_CALL },
        { NODE_MACHINE_TEMPLATE_DECIDED, BUSTER_NODE_ROWS_LATER },
        { NODE_MACHINE_TEMPLATE_SECOND, BUSTER_NODE_ROWS_LATER },
    };
    for (uint32_t template = 0; template < 64; template += 1)
    {
        uint8_t shape = 0;
        for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(template_shapes); t += 1)
        {
            shape = template_shapes[t][0] == template ? template_shapes[t][1] : shape;
        }
        wrong += tables.templates[template] != shape;
    }
    // And void's class is void's and it has no fact: the reads read no word for a target that is void, whose bytes
    // the shapes' sweep takes for zeros.
    CheckTestTypes types = { 0 };
    const bool opened = check_test_types_open(&types);
    wrong += !opened || check_lower_type_class(&types.table, BUSTER_TYPE_VOID) != BUSTER_TYPED_CLASS_VOID || check_lower_type_facts(&types.table, BUSTER_TYPE_VOID) != 0;
    check_test_types_close(&types);
    // The values' tables, by a record's own shape: the lists the sweep puts its record on and whether a value of its
    // row's is a number in its expression; and where in its row its stride, its step and its temporaries are. Every
    // other own shape's bytes are zero: its values are, or are an update's, whose loop goes by its own list.
    static const uint8_t values[][3] = {
        { BUSTER_TYPED_CONST, BUSTER_NODE_ROWS_VALUE_CONSTANT, 0 },
        { BUSTER_TYPED_LOCAL, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_TYPED_GLOBAL, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        // (An address's one entry is no record's own shape: an address constant's rows are the table's, and C on the
        // window lists a leaf read for its address.)
        { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_TYPED_STORE_LOCAL, BUSTER_NODE_ROWS_VALUE_STORED, 0 },
        { BUSTER_TYPED_STORE_GLOBAL, BUSTER_NODE_ROWS_VALUE_STORED, 0 },
        { BUSTER_TYPED_CALL, BUSTER_NODE_ROWS_VALUE_NUMBERED, 0 },
        { BUSTER_NODE_ROWS_ADDRESS, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_NODE_ROWS_ADDRESS + 1, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_NODE_ROWS_ADDRESS + 2, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_NODE_ROWS_ADDRESS + 3, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_NODE_ROWS_ADDRESS + 4, BUSTER_NODE_ROWS_VALUE_FIRST | BUSTER_NODE_ROWS_VALUE_REST, 0 },
        { BUSTER_NODE_ROWS_ADDRESS + 5, BUSTER_NODE_ROWS_VALUE_FIRST | BUSTER_NODE_ROWS_VALUE_REST, 0 },
        { BUSTER_NODE_ROWS_ADDRESS + 6, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_NODE_ROWS_ADDRESS + 7, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_NODE_ROWS_ADDRESS + 8, BUSTER_NODE_ROWS_VALUE_FIRST | BUSTER_NODE_ROWS_VALUE_REST, 0 },
        { BUSTER_NODE_ROWS_ADDRESS + 9, BUSTER_NODE_ROWS_VALUE_FIRST | BUSTER_NODE_ROWS_VALUE_REST, 0 },
        { BUSTER_NODE_ROWS_MEMBER, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_NODE_ROWS_MEMBER + 1, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_NODE_ROWS_POINTER, BUSTER_NODE_ROWS_VALUE_STRIDE, 0 },
        { BUSTER_NODE_ROWS_POINTER + 1, BUSTER_NODE_ROWS_VALUE_STRIDE, 1 },
        { BUSTER_NODE_ROWS_POINTER + 2, BUSTER_NODE_ROWS_VALUE_STRIDE, 0 },
        { BUSTER_NODE_ROWS_FOLDED, BUSTER_NODE_ROWS_VALUE_CONSTANT, 0 },
        { BUSTER_NODE_ROWS_UPDATE_POINTER, BUSTER_NODE_ROWS_VALUE_STRIDE, 0 },
        { BUSTER_NODE_ROWS_UPDATE_POINTER + 1, BUSTER_NODE_ROWS_VALUE_STRIDE, 1 },
        { BUSTER_NODE_ROWS_UPDATE_POINTER + 2, BUSTER_NODE_ROWS_VALUE_STRIDE, BUSTER_NODE_ROWS_SLOT_STEP | 1 },
        { BUSTER_NODE_ROWS_UPDATE_POINTER + 3, BUSTER_NODE_ROWS_VALUE_STRIDE, BUSTER_NODE_ROWS_SLOT_STEP | 2 },
        { BUSTER_NODE_ROWS_FIRST, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_NODE_ROWS_FIRST + 1, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_NODE_ROWS_FIRST + 2, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_NODE_ROWS_FIRST + 3, BUSTER_NODE_ROWS_VALUE_FIRST, 0 },
        { BUSTER_NODE_ROWS_KEPT, BUSTER_NODE_ROWS_VALUE_NUMBERED, 0 },
        { BUSTER_NODE_ROWS_KEPT + 1, BUSTER_NODE_ROWS_VALUE_STRIDE | BUSTER_NODE_ROWS_VALUE_NUMBERED, 1u << BUSTER_NODE_ROWS_SLOT_KEPT_SHIFT },
        { BUSTER_NODE_ROWS_KEPT + 2, BUSTER_NODE_ROWS_VALUE_FIRST | BUSTER_NODE_ROWS_VALUE_NUMBERED, 1u << BUSTER_NODE_ROWS_SLOT_KEPT_SHIFT },
        { BUSTER_NODE_ROWS_KEPT + 3, BUSTER_NODE_ROWS_VALUE_FIRST | BUSTER_NODE_ROWS_VALUE_NUMBERED, 1u << BUSTER_NODE_ROWS_SLOT_KEPT_SHIFT },
        { BUSTER_NODE_ROWS_KEPT + 4, BUSTER_NODE_ROWS_VALUE_FIRST | BUSTER_NODE_ROWS_VALUE_NUMBERED, 2u << BUSTER_NODE_ROWS_SLOT_KEPT_SHIFT },
        { BUSTER_NODE_ROWS_BRANCHED + 1, BUSTER_NODE_ROWS_VALUE_LABELED, 0 },
        { BUSTER_NODE_ROWS_HELD, BUSTER_NODE_ROWS_VALUE_LABELED, 0 },
        { BUSTER_NODE_ROWS_LOGIC, BUSTER_NODE_ROWS_VALUE_REST | BUSTER_NODE_ROWS_VALUE_LABELED, 0 },
        { BUSTER_NODE_ROWS_LOGIC + 1, BUSTER_NODE_ROWS_VALUE_REST | BUSTER_NODE_ROWS_VALUE_LABELED, 0 },
    };
    uint32_t valued = 0;
    for (uint32_t shape = 0; shape < 128; shape += 1)
    {
        // A conversion's part's shape has bytes that no one reads: the tables are read by a record's own shape.
        bool conversion = false;
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(conversions); c += 1)
        {
            conversion = conversion || conversions[c] == shape;
        }
        uint8_t lists = 0;
        uint8_t slots = 0;
        for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(values); v += 1)
        {
            lists = values[v][0] == shape ? values[v][1] : lists;
            slots = values[v][0] == shape ? values[v][2] : slots;
        }
        valued += !conversion && (tables.value_owns[shape] != lists || tables.value_slots[shape] != slots);
    }
    // The table's rows: every shape from the table's first to an operator's evaluated as branches, a logical
    // operator's value's two and an arm's five.
    const uint32_t rows_expected = BUSTER_NODE_ROWS_BRANCHED - BUSTER_NODE_ROWS_TABLE + 2 + (BUSTER_NODE_ROWS_SHAPES - BUSTER_NODE_ROWS_ARM);
    const bool valid = wrong == 0 && valued == 0 && rows == rows_expected;
    if (!valid)
    {
        fprintf(stderr, "test-checker: node-rows: the tables have %u rows (%u expected) and %u entries, lengths or sources the rows' write does not make; %u shapes' values' bytes are not their rows'\n",
                rows, rows_expected, wrong, valued);
    }
    return valid;
}

// What X's rows on the window make of a fixture: the expressions they run on (those C takes there), those whose tape
// they make and its entries, and the listed ones by their first record's reason.
typedef struct NodeRowsExpected NodeRowsExpected;
struct NodeRowsExpected
{
    uint32_t expressions;
    uint32_t taken;
    uint32_t entries;
    uint32_t listed_update;
    uint32_t listed_branches;
    uint32_t listed_conversion;
    uint32_t listed_floating;
    // The entries the rows' sweep wrote for the fixture's records, and the records the reads' loops read for: a
    // target's word, a line or a table, and an update's line; and the operators evaluated as branches, a turn of the
    // branches' loop each (a statement's CONDITION has none).
    uint32_t written;
    uint32_t words;
    uint32_t lines;
    uint32_t updates;
    uint32_t branched;
    // The records on each of the values' lists (BUSTER_NODE_ROWS_LIST_*), the expressions whose numbers were
    // numbered and the records their labels' walks went over, and, of the control's tapes, those that differ in a
    // value alone: every fourth's turn, with an entry. And the chunks its records come in: the control changes one
    // of a chunk's first run's answers, in thirteen turns, and the second run's comparison counts every chunk.
    uint32_t lists[BUSTER_NODE_ROWS_LISTS];
    uint32_t numbered;
    uint32_t steps;
    uint32_t control_valued;
    uint32_t chunks;
};

// X's rows on the window on fixture `text` (written to `directory` as `name`, read with `options`): every tape they
// make is X's on T's columns, entry by entry (its kind, its class and its value), and their counts are `expected`'s;
// and with one of the reference's answers changed for each (its first entry's kind, its last's class, its length and
// an entry's value in turn), every one differs; and with one of a chunk's first run's answers changed (the same
// control), every chunk's second run differs from it.
static bool node_rows_test_fixture(const char* directory, const char* name, const char* text, char** options, uint32_t option_count, const NodeRowsExpected* expected)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    bool valid = parse_test_write(path, text, strlen(text));
    NodeShadow shadow = {
        .verify = true,
        .front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | BUSTER_NODE_FRONT_LEAN,
        .reader = BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_VERIFY | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS |
                  BUSTER_NODE_READER_WINDOW_CONTEXTS | BUSTER_NODE_READER_WINDOW_ROWS,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    const uint64_t* counts = shadow.reader_facts;
    const uint64_t listed = counts[NODE_FACTS_COUNT_ROWS_LISTED_UPDATE] + counts[NODE_FACTS_COUNT_ROWS_LISTED_BRANCHES] + counts[NODE_FACTS_COUNT_ROWS_LISTED_CONVERSION] +
                            counts[NODE_FACTS_COUNT_ROWS_LISTED_FLOATING];
    bool listed_same = counts[NODE_FACTS_COUNT_ROWS_NUMBERED] == expected->numbered && counts[NODE_FACTS_COUNT_ROWS_NUMBER_STEPS] == expected->steps &&
                       counts[NODE_FACTS_COUNT_ROWS_VALUE_DIFFERENCES] == 0;
    for (uint32_t list = 0; list < BUSTER_NODE_ROWS_LISTS; list += 1)
    {
        listed_same = listed_same && counts[NODE_FACTS_COUNT_ROWS_LIST + list] == expected->lists[list];
    }
    const bool same = listed_same && counts[NODE_FACTS_COUNT_ROWS_EXPRESSIONS] == expected->expressions && counts[NODE_FACTS_COUNT_ROWS_TAKEN] == expected->taken &&
                      counts[NODE_FACTS_COUNT_ROWS_ENTRIES] == expected->entries && counts[NODE_FACTS_COUNT_ROWS_LISTED_UPDATE] == expected->listed_update &&
                      counts[NODE_FACTS_COUNT_ROWS_LISTED_BRANCHES] == expected->listed_branches &&
                      counts[NODE_FACTS_COUNT_ROWS_LISTED_CONVERSION] == expected->listed_conversion &&
                      counts[NODE_FACTS_COUNT_ROWS_LISTED_FLOATING] == expected->listed_floating && counts[NODE_FACTS_COUNT_ROWS_WRITTEN] == expected->written &&
                      counts[NODE_FACTS_COUNT_ROWS_WORDS] == expected->words && counts[NODE_FACTS_COUNT_ROWS_LINES] == expected->lines &&
                      counts[NODE_FACTS_COUNT_ROWS_UPDATES] == expected->updates && counts[NODE_FACTS_COUNT_ROWS_BRANCHED] == expected->branched &&
                      counts[NODE_FACTS_COUNT_ROWS_DIFFERENCES] == 0 &&
                      counts[NODE_FACTS_COUNT_ROWS_UNKEPT] == 0 && counts[NODE_FACTS_COUNT_ROWS_AGAIN_DIFFERENCES] == 0 &&
                      counts[NODE_FACTS_COUNT_TESTS_CHUNKS] == expected->chunks &&
                      counts[NODE_FACTS_COUNT_ROWS_TAKEN] + listed == counts[NODE_FACTS_COUNT_ROWS_EXPRESSIONS] &&
                      counts[NODE_FACTS_COUNT_ROWS_EXPRESSIONS] == counts[NODE_FACTS_COUNT_CONTEXTS_DECIDED] - counts[NODE_FACTS_COUNT_CONTEXTS_REFUSED] &&
                      counts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES] == 0 && counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES] == 0 &&
                      counts[NODE_FACTS_COUNT_TESTS_DIFFERENCES] == 0 && shadow.own_values_wrong == 0;
    // And each tape's expression's side facts are the program's own walks' (node_shadow_summaries).
    const bool summarized = shadow.summary_compared == expected->taken && shadow.summary_reference_differences == 0 && shadow.summary_fact_differences == 0 &&
                            shadow.summary_truth_differences == 0 && shadow.summary_fold_differences == 0 && shadow.summary_selector_differences == 0 && shadow.summary_unkept == 0 &&
                            counts[NODE_FACTS_COUNT_SUMMARY_AGAIN_DIFFERENCES] == 0;
    if (!summarized)
    {
        fprintf(stderr, "test-checker: node-rows %s: the side facts of %" PRIu64 " of the %u expressions whose tape is made were compared with the program's walks: references differ for %"
                PRIu64 ", return facts for %" PRIu64 ", truths for %" PRIu64 ", folds for %" PRIu64 ", selector types for %" PRIu64 "; %" PRIu64 " were not kept, and %" PRIu64
                " chunks' second run made other facts\n",
                name, shadow.summary_compared, expected->taken, shadow.summary_reference_differences, shadow.summary_fact_differences, shadow.summary_truth_differences,
                shadow.summary_fold_differences, shadow.summary_selector_differences, shadow.summary_unkept, counts[NODE_FACTS_COUNT_SUMMARY_AGAIN_DIFFERENCES]);
    }
    if (!valid || !same)
    {
        fprintf(stderr, "test-checker: node-rows %s: %s: X's rows on the window ran on %" PRIu64 " expressions (%u expected; C takes %" PRIu64 " there) and made %" PRIu64
                " tapes (%u) of %" PRIu64 " entries (%u); listed: update=%" PRIu64 " (%u) branches=%" PRIu64 " (%u) refused-conversion=%" PRIu64
                " (%u) constant-to-a-floating-class=%" PRIu64 " (%u); entries written=%" PRIu64 " (%u), records read for a target's word=%" PRIu64
                " (%u), for a line or a table=%" PRIu64 " (%u) and for an update's line=%" PRIu64 " (%u), operators evaluated as branches=%" PRIu64 " (%u); %" PRIu64
                " tapes differ from X's on T's columns, the reference kept none for %" PRIu64
                ", %" PRIu64 " of the %" PRIu64 " chunks' (%u expected) second run wrote other entries, and the reader's verify finds %" PRIu64
                " bytes wrong (a record the kernel didn't place has none, its type's column's among them)\n",
                name, valid ? "read" : "NOT READ", counts[NODE_FACTS_COUNT_ROWS_EXPRESSIONS], expected->expressions,
                counts[NODE_FACTS_COUNT_CONTEXTS_DECIDED] - counts[NODE_FACTS_COUNT_CONTEXTS_REFUSED], counts[NODE_FACTS_COUNT_ROWS_TAKEN], expected->taken,
                counts[NODE_FACTS_COUNT_ROWS_ENTRIES], expected->entries, counts[NODE_FACTS_COUNT_ROWS_LISTED_UPDATE], expected->listed_update,
                counts[NODE_FACTS_COUNT_ROWS_LISTED_BRANCHES], expected->listed_branches, counts[NODE_FACTS_COUNT_ROWS_LISTED_CONVERSION], expected->listed_conversion,
                counts[NODE_FACTS_COUNT_ROWS_LISTED_FLOATING], expected->listed_floating, counts[NODE_FACTS_COUNT_ROWS_WRITTEN], expected->written,
                counts[NODE_FACTS_COUNT_ROWS_WORDS], expected->words, counts[NODE_FACTS_COUNT_ROWS_LINES], expected->lines, counts[NODE_FACTS_COUNT_ROWS_UPDATES],
                expected->updates, counts[NODE_FACTS_COUNT_ROWS_BRANCHED], expected->branched, counts[NODE_FACTS_COUNT_ROWS_DIFFERENCES], counts[NODE_FACTS_COUNT_ROWS_UNKEPT],
                counts[NODE_FACTS_COUNT_ROWS_AGAIN_DIFFERENCES], counts[NODE_FACTS_COUNT_TESTS_CHUNKS], expected->chunks, shadow.own_values_wrong);
        fprintf(stderr, "test-checker: node-rows %s: the values: %" PRIu64 " tapes differ in a value alone; records on the lists: its record's value=%" PRIu64 " (%u) a store by name's destination's=%"
                PRIu64 " (%u) a constant's bits=%" PRIu64 " (%u) a stride=%" PRIu64 " (%u) the control's label=%" PRIu64 " (%u) the rest=%" PRIu64
                " (%u) a number of its expression's after the first=%" PRIu64 " (%u); expressions numbered=%" PRIu64 " (%u), the records their labels' walks went over=%" PRIu64
                " (%u)\n",
                name, counts[NODE_FACTS_COUNT_ROWS_VALUE_DIFFERENCES], counts[NODE_FACTS_COUNT_ROWS_LIST + BUSTER_NODE_ROWS_LIST_FIRST], expected->lists[BUSTER_NODE_ROWS_LIST_FIRST],
                counts[NODE_FACTS_COUNT_ROWS_LIST + BUSTER_NODE_ROWS_LIST_STORED], expected->lists[BUSTER_NODE_ROWS_LIST_STORED],
                counts[NODE_FACTS_COUNT_ROWS_LIST + BUSTER_NODE_ROWS_LIST_CONSTANT], expected->lists[BUSTER_NODE_ROWS_LIST_CONSTANT],
                counts[NODE_FACTS_COUNT_ROWS_LIST + BUSTER_NODE_ROWS_LIST_STRIDE], expected->lists[BUSTER_NODE_ROWS_LIST_STRIDE],
                counts[NODE_FACTS_COUNT_ROWS_LIST + BUSTER_NODE_ROWS_LIST_CONTROLLED], expected->lists[BUSTER_NODE_ROWS_LIST_CONTROLLED],
                counts[NODE_FACTS_COUNT_ROWS_LIST + BUSTER_NODE_ROWS_LIST_REST], expected->lists[BUSTER_NODE_ROWS_LIST_REST],
                counts[NODE_FACTS_COUNT_ROWS_LIST + BUSTER_NODE_ROWS_LIST_SECOND], expected->lists[BUSTER_NODE_ROWS_LIST_SECOND], counts[NODE_FACTS_COUNT_ROWS_NUMBERED],
                expected->numbered, counts[NODE_FACTS_COUNT_ROWS_NUMBER_STEPS], expected->steps);
    }
    // The comparisons' control: with one of the reference's answers changed for each tape made, every one differs;
    // and with one of a chunk's first run's answers changed, every chunk's second run does.
    NodeShadow faulted = {
        .verify = true,
        .front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | BUSTER_NODE_FRONT_LEAN,
        .reader = BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_VERIFY | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS |
                  BUSTER_NODE_READER_WINDOW_CONTEXTS | BUSTER_NODE_READER_WINDOW_ROWS | BUSTER_NODE_READER_WINDOW_ROWS_FAULT,
        .passes = 1,
        .repeat = 1,
    };
    NodeCensus faulted_census = { .shadow = &faulted };
    const bool faulted_read = valid && node_census_file(&faulted_census, path, options, option_count) && faulted_census.files == 1;
    const uint64_t* faulted_counts = faulted.reader_facts;
    const bool controlled = faulted_read && faulted_counts[NODE_FACTS_COUNT_ROWS_TAKEN] == expected->taken && faulted_counts[NODE_FACTS_COUNT_ROWS_DIFFERENCES] == expected->taken &&
                            faulted_counts[NODE_FACTS_COUNT_ROWS_VALUE_DIFFERENCES] == expected->control_valued && expected->taken >= 3 &&
                            faulted_counts[NODE_FACTS_COUNT_ROWS_AGAIN_DIFFERENCES] == expected->chunks && faulted_counts[NODE_FACTS_COUNT_TESTS_CHUNKS] == expected->chunks;
    if (!controlled)
    {
        fprintf(stderr, "test-checker: node-rows %s: %s: with one of the reference's answers changed for each tape, the comparison counts %" PRIu64 " of the %" PRIu64
                " tapes made (%u expected: every one), %" PRIu64 " of them for a value alone (%u expected); and with one of a chunk's first run's answers changed, %" PRIu64
                " of the %" PRIu64 " chunks' second run differs (%u expected: every one)\n",
                name, faulted_read ? "read" : "NOT READ", faulted_counts[NODE_FACTS_COUNT_ROWS_DIFFERENCES], faulted_counts[NODE_FACTS_COUNT_ROWS_TAKEN], expected->taken,
                faulted_counts[NODE_FACTS_COUNT_ROWS_VALUE_DIFFERENCES], expected->control_valued, faulted_counts[NODE_FACTS_COUNT_ROWS_AGAIN_DIFFERENCES],
                faulted_counts[NODE_FACTS_COUNT_TESTS_CHUNKS], expected->chunks);
    }
    unlink(path);
    return valid && same && summarized && controlled;
}

// What a summary's side facts make of a fixture: the expressions whose facts are kept (those whose tape X makes), the
// references the sweep gives them (an object once a leaf) and the program's own walk makes of them (a set an
// expression), the records listed for a reference and the calls asked a record at a time, the expressions with an
// unknown and with a stop for a return fact and those the reduction ran on, the operators whose turn lists their
// expression for a second operand whose truth is known, the branches' conditions among the kept and those of them
// whose truth the program knows, and the kept roots whose constant the controls fold. And with the control: the roots
// reader time typed, whose truth and fold are then asked whether kept or not, and those of them the two comparisons
// count: the conditions an operand of which decides, and the constants the controls do not fold. And its chunks.
typedef struct NodeSummaryExpected NodeSummaryExpected;
struct NodeSummaryExpected
{
    uint32_t taken;
    uint32_t references;
    uint32_t program_references;
    uint32_t listed;
    uint32_t suspects;
    uint32_t unknowns;
    uint32_t stops;
    uint32_t reduced;
    uint32_t decided;
    uint32_t conditions;
    uint32_t truths;
    uint32_t folds;
    uint32_t control_asked;
    uint32_t control_truths;
    uint32_t control_folds;
    uint32_t chunks;
};

// Six of the fixture's nine selectors are enums, from two different definitions. An explicit cast to int,
// unary promotion and an integer by itself account for the other three. Every one must reach the oracle.
static bool node_summary_test_selectors(char** options, uint32_t option_count, bool fault)
{
    const uint8_t front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | BUSTER_NODE_FRONT_LEAN;
    const uint16_t reader = BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_VERIFY | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS |
                            BUSTER_NODE_READER_WINDOW_CONTEXTS | BUSTER_NODE_READER_WINDOW_ROWS | (fault ? BUSTER_NODE_READER_WINDOW_SUMMARY_FAULT : 0);
    NodeShadow shadow = { .verify = true, .front = front, .reader = reader, .passes = 1, .repeat = 1 };
    NodeCensus census = { .shadow = &shadow };
    const bool read = node_census_file(&census, "tests/node-machine/switch-selectors.c", options, option_count) && census.files == 1 && census.failures == 0;
    const bool valid = read && shadow.summary_selectors == 9 && shadow.summary_enum_selectors == 6 && shadow.summary_selector_differences == (fault ? 9u : 0u);
    if (!valid)
    {
        fprintf(stderr, "test-checker: switch selectors%s: %" PRIu64 " compared, %" PRIu64 " enums, %" PRIu64 " differences (expected 9, 6, %u)\n",
                fault ? " with changed types" : "", shadow.summary_selectors, shadow.summary_enum_selectors, shadow.summary_selector_differences, fault ? 9u : 0u);
    }
    NodeShadow selector_only = { .summary_selector_differences = shadow.summary_selector_differences };
    return valid && node_shadow_differs(&selector_only) == fault;
}

// A summary's side facts on fixture `text` (written to `directory` as `name`, read with `options`): each kept
// expression's references and return fact are the program's own walks', a kept condition's truth is its value's and a
// kept constant folds, and the counts are `expected`'s. With the shadow's switch nothing is kept and X's tapes are the
// same. And with the control, where reader time keeps each expression's facts with one changed (a reference more, a
// reference's object, the fact's stop and its unknown in turn) and one of a chunk's first run's is changed: every
// expression's references or fact differ, by its turn; every chunk's second run differs; and the truths and the folds
// asked of every root count the fixture's lines the window lists for them.
static bool node_summary_test_fixture(const char* directory, const char* name, const char* text, char** options, uint32_t option_count, const NodeSummaryExpected* expected)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    bool valid = parse_test_write(path, text, strlen(text));
    const uint8_t front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | BUSTER_NODE_FRONT_LEAN;
    const uint16_t reader = BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_VERIFY | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS |
                            BUSTER_NODE_READER_WINDOW_CONTEXTS | BUSTER_NODE_READER_WINDOW_ROWS;
    NodeShadow shadow = { .verify = true, .front = front, .reader = reader, .passes = 1, .repeat = 1 };
    NodeCensus census = { .shadow = &shadow };
    fflush(stdout);
    valid = valid && node_census_file(&census, path, options, option_count) && census.files == 1 && census.failures == 0;
    const uint64_t* counts = shadow.reader_facts;
    const bool exact = shadow.summary_reference_differences == 0 && shadow.summary_fact_differences == 0 && shadow.summary_truth_differences == 0 &&
                       shadow.summary_fold_differences == 0 && shadow.summary_selector_differences == 0 && shadow.summary_unkept == 0 && counts[NODE_FACTS_COUNT_SUMMARY_AGAIN_DIFFERENCES] == 0 &&
                       counts[NODE_FACTS_COUNT_ROWS_DIFFERENCES] == 0 && counts[NODE_FACTS_COUNT_ROWS_AGAIN_DIFFERENCES] == 0 &&
                       counts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES] == 0 && counts[NODE_FACTS_COUNT_TESTS_DIFFERENCES] == 0;
    const bool same = counts[NODE_FACTS_COUNT_ROWS_TAKEN] == expected->taken && counts[NODE_FACTS_COUNT_SUMMARY_EXPRESSIONS] == expected->taken &&
                      shadow.summary_compared == expected->taken && shadow.summary_asked == expected->taken &&
                      counts[NODE_FACTS_COUNT_SUMMARY_REFERENCES] == expected->references && shadow.summary_references == expected->program_references &&
                      counts[NODE_FACTS_COUNT_SUMMARY_LISTED] == expected->listed && counts[NODE_FACTS_COUNT_SUMMARY_SUSPECTS] == expected->suspects &&
                      counts[NODE_FACTS_COUNT_SUMMARY_UNKNOWN] == expected->unknowns && shadow.summary_unknowns == expected->unknowns &&
                      counts[NODE_FACTS_COUNT_SUMMARY_STOPS] == expected->stops && shadow.summary_stops == expected->stops &&
                      counts[NODE_FACTS_COUNT_SUMMARY_REDUCED] == expected->reduced && counts[NODE_FACTS_COUNT_SUMMARY_DECIDED] == expected->decided &&
                      shadow.summary_conditions == expected->conditions && shadow.summary_truths == expected->truths && shadow.summary_folds == expected->folds &&
                      counts[NODE_FACTS_COUNT_TESTS_CHUNKS] == expected->chunks;
    if (!valid || !exact || !same)
    {
        fprintf(stderr, "test-checker: node-summary %s: %s: the side facts are kept for %" PRIu64 " expressions (%u expected; X makes %" PRIu64 " tapes, and %" PRIu64
                " were compared) with %" PRIu64 " references (%u), where the program's walk makes %" PRIu64 " (%u); records listed for a reference=%" PRIu64
                " (%u) calls asked a record at a time=%" PRIu64 " (%u); with an unknown=%" PRIu64 " (%u; the program's %" PRIu64 ") with a stop=%" PRIu64
                " (%u; the program's %" PRIu64 ") reduced=%" PRIu64 " (%u); operators whose turn lists for a known second operand=%" PRIu64
                " (%u); branches' conditions=%" PRIu64 " (%u) with a truth=%" PRIu64 " (%u); constants folded=%" PRIu64 " (%u); chunks=%" PRIu64
                " (%u). Differences: references=%" PRIu64 " facts=%" PRIu64 " truths=%" PRIu64
                " folds=%" PRIu64 " not kept=%" PRIu64 " second runs=%" PRIu64 "; X's tapes=%" PRIu64 "\n",
                name, valid ? "read" : "NOT READ", counts[NODE_FACTS_COUNT_SUMMARY_EXPRESSIONS], expected->taken, counts[NODE_FACTS_COUNT_ROWS_TAKEN], shadow.summary_compared,
                counts[NODE_FACTS_COUNT_SUMMARY_REFERENCES], expected->references, shadow.summary_references, expected->program_references,
                counts[NODE_FACTS_COUNT_SUMMARY_LISTED], expected->listed, counts[NODE_FACTS_COUNT_SUMMARY_SUSPECTS], expected->suspects,
                counts[NODE_FACTS_COUNT_SUMMARY_UNKNOWN], expected->unknowns, shadow.summary_unknowns, counts[NODE_FACTS_COUNT_SUMMARY_STOPS], expected->stops,
                shadow.summary_stops, counts[NODE_FACTS_COUNT_SUMMARY_REDUCED], expected->reduced, counts[NODE_FACTS_COUNT_SUMMARY_DECIDED], expected->decided,
                shadow.summary_conditions, expected->conditions, shadow.summary_truths, expected->truths, shadow.summary_folds, expected->folds,
                counts[NODE_FACTS_COUNT_TESTS_CHUNKS], expected->chunks, shadow.summary_reference_differences,
                shadow.summary_fact_differences, shadow.summary_truth_differences, shadow.summary_fold_differences, shadow.summary_unkept,
                counts[NODE_FACTS_COUNT_SUMMARY_AGAIN_DIFFERENCES], counts[NODE_FACTS_COUNT_ROWS_DIFFERENCES]);
    }
    // The switch: no facts, and the same tapes.
    NodeShadow off = { .verify = true, .front = front, .reader = (uint16_t)(reader | BUSTER_NODE_READER_WINDOW_SUMMARY_OFF), .passes = 1, .repeat = 1 };
    NodeCensus off_census = { .shadow = &off };
    const bool off_read = valid && node_census_file(&off_census, path, options, option_count) && off_census.files == 1;
    const uint64_t* off_counts = off.reader_facts;
    const bool switched = off_read && off_counts[NODE_FACTS_COUNT_ROWS_TAKEN] == expected->taken && off_counts[NODE_FACTS_COUNT_ROWS_DIFFERENCES] == 0 &&
                          off_counts[NODE_FACTS_COUNT_ROWS_ENTRIES] == counts[NODE_FACTS_COUNT_ROWS_ENTRIES] && off_counts[NODE_FACTS_COUNT_SUMMARY_EXPRESSIONS] == 0 &&
                          off_counts[NODE_FACTS_COUNT_SUMMARY_LISTED] == 0 && off_counts[NODE_FACTS_COUNT_SUMMARY_DECIDED] == expected->decided && off.summary_compared == 0 &&
                          off.summary_asked == 0;
    if (!switched)
    {
        fprintf(stderr, "test-checker: node-summary %s: %s: with the switch X makes %" PRIu64 " tapes (%u expected) of %" PRIu64 " entries (%" PRIu64
                "), %" PRIu64 " differ, and the side facts are kept for %" PRIu64 " expressions and compared for %" PRIu64 " (none expected)\n",
                name, off_read ? "read" : "NOT READ", off_counts[NODE_FACTS_COUNT_ROWS_TAKEN], expected->taken, off_counts[NODE_FACTS_COUNT_ROWS_ENTRIES],
                counts[NODE_FACTS_COUNT_ROWS_ENTRIES], off_counts[NODE_FACTS_COUNT_ROWS_DIFFERENCES], off_counts[NODE_FACTS_COUNT_SUMMARY_EXPRESSIONS], off.summary_compared);
    }
    // The control: a kept expression's turn is its place among the kept ones, and the first two turns change its
    // references and the other two its fact.
    NodeShadow faulted = { .verify = true, .front = front, .reader = (uint16_t)(reader | BUSTER_NODE_READER_WINDOW_SUMMARY_FAULT), .passes = 1, .repeat = 1 };
    NodeCensus faulted_census = { .shadow = &faulted };
    const bool faulted_read = valid && node_census_file(&faulted_census, path, options, option_count) && faulted_census.files == 1;
    const uint64_t* faulted_counts = faulted.reader_facts;
    const uint32_t referenced = (expected->taken + 3) / 4 + (expected->taken + 2) / 4;
    const bool controlled = faulted_read && faulted.summary_compared == expected->taken && faulted.summary_reference_differences == referenced &&
                            faulted.summary_fact_differences == expected->taken - referenced && expected->taken >= 4 &&
                            faulted.summary_asked == expected->control_asked && faulted.summary_truth_differences == expected->control_truths &&
                            faulted.summary_fold_differences == expected->control_folds &&
                            faulted_counts[NODE_FACTS_COUNT_SUMMARY_AGAIN_DIFFERENCES] == expected->chunks && faulted_counts[NODE_FACTS_COUNT_TESTS_CHUNKS] == expected->chunks;
    if (!controlled)
    {
        fprintf(stderr, "test-checker: node-summary %s: %s: with one of each kept expression's facts changed, the comparison counts %" PRIu64 " of the %" PRIu64
                " for their references (%u expected) and %" PRIu64 " for their fact (%u expected: every one, by its turn); with the truths and the folds asked of the %"
                PRIu64 " roots typed (%u expected), %" PRIu64 " truths differ (%u) and %" PRIu64 " folds (%u); and with one of a chunk's first run's facts changed, %"
                PRIu64 " of the %" PRIu64 " chunks' second run differs (%u expected: every one)\n",
                name, faulted_read ? "read" : "NOT READ", faulted.summary_reference_differences, faulted.summary_compared, referenced, faulted.summary_fact_differences,
                expected->taken - referenced, faulted.summary_asked, expected->control_asked, faulted.summary_truth_differences, expected->control_truths,
                faulted.summary_fold_differences, expected->control_folds, faulted_counts[NODE_FACTS_COUNT_SUMMARY_AGAIN_DIFFERENCES],
                faulted_counts[NODE_FACTS_COUNT_TESTS_CHUNKS], expected->chunks);
    }
    unlink(path);
    return valid && exact && same && switched && controlled;
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
// and on none of the 64 the chunks' ends cut, whose records are not the window's, and none differs. And with C on the
// window beside them ("C on the window"), whose consumers' distances the kept values' records keep: it decides the
// expressions with no conditional among those (four of the seven statements' updates, and the first one's condition),
// as C does on T's columns.
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
        .reader = BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS | BUSTER_NODE_READER_WINDOW_CONTEXTS,
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
    const uint64_t contexts_expressions = placed;
    const uint64_t contexts_decided = 8259;
    // Its expressions are short: none has a consumer or an operand out of a block's reach, placed or not.
    const uint64_t contexts_far = 0;
    const bool contexts = read && tested.reader_facts[NODE_FACTS_COUNT_CONTEXTS_EXPRESSIONS] == contexts_expressions &&
                          tested.reader_facts[NODE_FACTS_COUNT_CONTEXTS_DECIDED] == contexts_decided && tested.reader_facts[NODE_FACTS_COUNT_CONTEXTS_REFUSED] == 0 &&
                          tested.reader_facts[NODE_FACTS_COUNT_CONTEXTS_FAR] == contexts_far && tested.reader_facts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES] == 0;
    if (!contexts)
    {
        fprintf(stderr, "test-checker: node-facts window, no room for the waiting values' records, with C on the window: %s; it ran on %" PRIu64 " expressions (expected %"
                PRIu64 "), decided %" PRIu64 " (expected %" PRIu64 ") and refused %" PRIu64 " of them (expected none), read %" PRIu64
                " consumers and operands by loads (expected %" PRIu64 "), and %" PRIu64 " differ from C on T's columns\n",
                read ? "read" : "NOT READ", tested.reader_facts[NODE_FACTS_COUNT_CONTEXTS_EXPRESSIONS], contexts_expressions,
                tested.reader_facts[NODE_FACTS_COUNT_CONTEXTS_DECIDED], contexts_decided, tested.reader_facts[NODE_FACTS_COUNT_CONTEXTS_REFUSED],
                tested.reader_facts[NODE_FACTS_COUNT_CONTEXTS_FAR], contexts_far, tested.reader_facts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES]);
    }
    free(text);
    unlink(path);
    return held && tests && contexts;
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

// Compile the source independently both ways. Change the retained first row after checking and lower it again:
// only a lowering that actually splices the reader's tape can emit the changed value.
static void node_window_test_compile(CheckTest* test)
{
    char file[PATH_MAX];
    snprintf(file, sizeof(file), "%s/checker/node-window.c", check_test_fixture_root);
    char* options[] = { "-nostdinc", "-Wall", "-Wextra" };
    static LowerSource plain;
    static LowerSource window;
    const bool opened_plain = lower_source_open(&plain, file, options, BUSTER_ARRAY_LENGTH(options), false);
    const bool opened_window = lower_source_open(&window, file, options, BUSTER_ARRAY_LENGTH(options), true);
    const bool prepared = opened_plain && opened_window && plain.prepared && window.prepared;
    check_test_expect(test, prepared, "node-window prepares its mixed ordinary and taken returns");
    if (prepared)
    {
        const CheckInput* reference = &plain.path.input;
        CheckInput* input = &window.path.input;
        check_test_expect(test, reference->window_count == 0 && input->window_count == 9 && input->window_nodes == 28,
                          "node-window admits nine returns on baked and runtime-initialized transition tables");
        check_test_expect(test, reference->graph.count == input->graph.count + input->window_nodes - input->window_count,
                          "node-window replaces 28 ordinary nodes by nine return summaries");
        bool same = true;
        uint32_t reads = 0;
        for (uint32_t i = 0; same && i < input->window_count; i += 1)
        {
            const CheckInputWindowEntry* row = input->window_entries + i;
            const uint32_t node = row->node;
            const uint32_t returned = node < input->graph.count ? input->graph.tokens[node] : BUSTER_CHECK_NONE;
            const uint32_t other = returned < reference->token_count ? reference->token_nodes[returned] : BUSTER_CHECK_NONE;
            if (other >= plain.path.stream.count)
            {
                same = false;
                break;
            }
            const CheckValue value = check_stream_value(&window.path.stream, node);
            const CheckValue regular = check_stream_value(&plain.path.stream, other);
            same = input->graph.ops[node] == CHECK_OP_VALUE && input->graph.members[node] == i + 1 &&
                   check_test_value_equal(&value, &regular) && row->class == BUSTER_TYPED_CLASS_I32;
            if (row->read_offset != 0)
            {
                const uint32_t token = returned + row->read_offset;
                same &= row->kind == BUSTER_TYPED_LOCAL && token < input->token_count && input->declarations[token] + 1 == row->value;
                reads += 1;
            }
            else
            {
                same &= row->kind == BUSTER_TYPED_CONST && row->value == (uint64_t)(int64_t)(int32_t)value.integer;
            }
        }
        check_test_expect(test, same && reads == 2, "node-window publishes each complete ordinary checked value and its native row, including two local read events");
        CheckProgramLowered original = { 0 };
        CheckProgramLowered first = { 0 };
        bool lowered = check_program_lower(&original, &plain.selected.program, true, &plain.path.output_arena, &plain.path.scratch) &&
                       check_program_lower(&first, &window.selected.program, true, &window.path.output_arena, &window.path.scratch) && original.count == first.count;
        for (uint32_t b = 0; lowered && b < first.count; b += 1)
        {
            lowered = check_test_program_function(original.functions + b, first.functions + b);
        }
        check_test_expect(test, lowered, "node-window mixed returns lower to the ordinary functions' native tapes");
        bool replayed = false;
        if (lowered && input->window_count != 0)
        {
            input->window_entries[0].value = 314159;
            CheckProgramLowered changed = { 0 };
            if (check_program_lower(&changed, &window.selected.program, true, &window.path.output_arena, &window.path.scratch) && changed.count == first.count)
            {
                for (uint32_t b = 0; b < changed.count; b += 1)
                {
                    const Tier0Function* function = changed.functions + b;
                    for (uint32_t e = 0; e < function->count; e += 1)
                    {
                        replayed |= function->kinds[e] == BUSTER_TYPED_CONST && function->values[e] == 314159;
                    }
                }
            }
        }
        check_test_expect(test, replayed, "node-window lowering reads the retained native row independently of the checked summary");
    }
    lower_source_close(&window);
    lower_source_close(&plain);
}

static __attribute__((noinline)) void check_test_node_census(CheckTest* test)
{
    node_window_test_compile(test);
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
    check_test_expect(test, ready && node_census_test_fixture(directory, "holes.c", node_census_test_holes, quiet, BUSTER_ARRAY_LENGTH(quiet), &system),
                      "node-census refuses a pointer's ordered comparison with an integer, a converted pointer's cast, a redeclared condition's call, a declarator's "
                      "warning's initializer and a cleanup's return, and takes what stands beside each");
    check_test_expect(test, ready && node_census_test_fixture(directory, "segments.c", node_census_test_segments, quiet, BUSTER_ARRAY_LENGTH(quiet), &system),
                      "node-census refuses a value of a type in a segment's address space, and takes a pointer to one and another space's value");
    char* signs[] = { "-nostdinc", "-Wsign-compare" };
    check_test_expect(test, ready && node_census_test_fixture(directory, "signs.c", node_census_test_signs, signs, BUSTER_ARRAY_LENGTH(signs), &system),
                      "node-census takes two pointers' ordered comparison under the sign comparison's warning, and refuses a pointer's with an integer");
    char* silent[] = { "-nostdinc", "-w" };
    check_test_expect(test, ready && node_census_test_fixture(directory, "silent.c", node_census_test_silent, silent, BUSTER_ARRAY_LENGTH(silent), &system),
                      "node-census refuses nothing for a pointer's comparison or cast with every warning off");
    check_test_expect(test, ready && node_census_test_fixture(directory, "interrupt.c", node_census_test_interrupt, quiet, BUSTER_ARRAY_LENGTH(quiet), &system),
                      "node-census refuses every expression of a unit that reads x86's interrupt attribute");
    check_test_expect(test, ready && node_census_test_fixture(directory, "noderef.c", node_census_test_noderef, quiet, BUSTER_ARRAY_LENGTH(quiet), &system),
                      "node-census refuses every expression of a unit that spells noderef");
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
                    "-Wno-pointer-to-int-cast",
                    "-Wno-non-literal-null-conversion" };
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
        .sent = 22,
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
        .decided = 41,
        .decided_refused = 1,
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
        .decided = 23,
    };
    static const NodeTestsWindowed windowed_set = {
        .expressions = 13,
        .operators = 8,
        .quiet = 2,
        .values = 8,
        .spared = 5,
        .direct = 2,
        .decided = 12,
        .decided_refused = 2,
    };
    static const NodeTestsWindowed windowed_quiet = {
        .expressions = 7,
        .operators = 3,
        .sent = 2,
        .quiet = 1,
        .values = 7,
        .spared = 5,
        .direct = 2,
        .decided = 7,
    };
    static const NodeTestsWindowed windowed_atomic = {
        .expressions = 3,
        .operators = 3,
        .sent = 3,
        .quiet = 1,
        .values = 1,
        .decided = 3,
    };
    // (Three of its operators are dereferences of a function, `(*k0)(a, b)` and `(**k0)(a, b)`: a function's type's
    // byte has the void pointee's bit, so each is listed and sent to the census's code, which finds no pointer to void.)
    static const NodeTestsWindowed windowed_defaults = {
        .expressions = 475,
        .trees = 29,
        .operators = 130,
        .sent = 55,
        .walks = 12,
        .far = 37,
        .quiet = 77,
        .calls = 179,
        .calls_checked = 179,
        .casts = 163,
        .casts_left = 30,
        .asks = 9,
        .nulls = 8,
        .values = 253,
        .spared = 263,
        .direct = 211,
        .apart = 22,
        .decided = 446,
        .decided_refused = 10,
    };
    // (The loop leaves three ordered comparisons of two pointers out: `p < e`, `ga < p` and the `while`'s.)
    static const NodeTestsWindowed windowed_holes = {
        .expressions = 65,
        .operators = 25,
        .sent = 13,
        .quiet = 11,
        .calls = 3,
        .calls_checked = 3,
        .casts = 1,
        .casts_left = 3,
        .orders = 3,
        .values = 27,
        .spared = 43,
        .direct = 26,
        .apart = 8,
        .decided = 63,
    };
    static const NodeTestsWindowed windowed_segments = {
        .expressions = 11,
        .operators = 1,
        .quiet = 2,
        .casts_left = 1,
        .values = 2,
        .spared = 9,
        .direct = 9,
        .decided = 11,
    };
    static const NodeTestsWindowed windowed_interrupt = {
        .expressions = 3,
        .calls = 1,
        .calls_checked = 1,
        .casts = 1,
        .asks = 1,
        .spared = 1,
        .direct = 3,
        .decided = 3,
    };
    static const NodeTestsWindowed windowed_noderef = {
        .expressions = 2,
        .direct = 2,
        .decided = 2,
    };
    static const NodeTestsWindowed windowed_enums = {
        .expressions = 5,
        .operators = 2,
        .quiet = 1,
        .values = 4,
        .spared = 4,
        .direct = 2,
        .decided = 5,
    };
    static const NodeTestsWindowed windowed_off = {
        .expressions = 18,
        .trees = 5,
        .operators = 3,
        .quiet = 3,
        .calls = 1,
        .calls_checked = 1,
        .casts = 1,
        .asks = 1,
        .values = 12,
        .spared = 7,
        .direct = 7,
        .apart = 1,
        .decided = 18,
    };
    static const NodeTestsWindowed windowed_whole = {
        .expressions = 2,
        .spared = 1,
        .direct = 2,
        .decided = 2,
    };
    static const NodeTestsWindowed windowed_tagged = {
        .expressions = 4,
        .operators = 3,
        .values = 2,
        .decided = 4,
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
        .decided = 10,
    };
    static const NodeTestsWindowed windowed_system = {
        .expressions = 3,
        .operators = 1,
        .quiet = 1,
        .values = 2,
        .spared = 1,
        .direct = 2,
        .decided = 3,
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
                                                             BUSTER_NODE_FRONT_AFTER, 198, 28, windowed_defaults),
                      "node-tests: at reader time the tests refuse what warns without an option, with the default warnings alone");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "holes.c", node_census_test_holes, quiet, BUSTER_ARRAY_LENGTH(quiet),
                                                             BUSTER_NODE_FRONT_AFTER, 28, 13, windowed_holes),
                      "node-tests: at reader time the tests refuse what the compiler's own tests' programs had and no corpus, and leave a loop's pointers' comparison out");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "segments.c", node_census_test_segments, quiet, BUSTER_ARRAY_LENGTH(quiet),
                                                             BUSTER_NODE_FRONT_AFTER, 7, 1, windowed_segments),
                      "node-tests: at reader time a value of a type in a segment's address space is refused by its type's byte");
    static const NodeTestsWindowed windowed_signs = {
        .expressions = 5,
        .operators = 3,
        .sent = 3,
        .casts_left = 1,
        .spared = 4,
        .direct = 2,
        .apart = 1,
        .decided = 5,
    };
    check_test_expect(test, ready && node_tests_test_fixture(directory, "signs.c", node_census_test_signs, signs, BUSTER_ARRAY_LENGTH(signs),
                                                             BUSTER_NODE_FRONT_AFTER, 1, 1, windowed_signs),
                      "node-tests: at reader time, under the sign comparison's warning, the loop leaves no pointers' comparison out");
    static const NodeTestsWindowed windowed_silent = {
        .expressions = 7,
        .quiet = 1,
        .values = 2,
        .spared = 5,
        .direct = 6,
        .apart = 2,
        .decided = 7,
    };
    check_test_expect(test, ready && node_tests_test_fixture(directory, "silent.c", node_census_test_silent, silent, BUSTER_ARRAY_LENGTH(silent),
                                                             BUSTER_NODE_FRONT_AFTER, 0, 1, windowed_silent),
                      "node-tests: at reader time, with every warning off, a pointer's comparison and its cast are taken");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "interrupt.c", node_census_test_interrupt, quiet, BUSTER_ARRAY_LENGTH(quiet),
                                                             BUSTER_NODE_FRONT_AFTER, 3, 0, windowed_interrupt),
                      "node-tests: at reader time a unit that reads x86's interrupt attribute has every expression refused");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "noderef.c", node_census_test_noderef, quiet, BUSTER_ARRAY_LENGTH(quiet),
                                                             BUSTER_NODE_FRONT_AFTER, 2, 0, windowed_noderef),
                      "node-tests: at reader time a unit that spells noderef has every expression refused");
    static const NodeObjectsCounted counted_labels = {
        .objects = 6,
        .taken = 10,
        .callees = 1,
        .library = 2,
        .lines = 14,
        .asks = 28,
    };
    static const NodeObjectsCounted counted_renames = {
        .objects = 8,
        .renamed = 2,
        .taken = 3,
        .callees = 3,
        .lines = 9,
        .asks = 38,
    };
    static const NodeObjectsCounted counted_versions = {
        .objects = 6,
        .own = 1,
        .taken = 5,
        .callees = 2,
        .lines = 8,
        .asks = 14,
    };
    static const NodeObjectsCounted counted_unversioned = {
        .objects = 4,
        .own = 1,
        .taken = 2,
        .callees = 1,
        .lines = 4,
        .asks = 6,
    };
    static const NodeObjectsCounted counted_weak = {
        .objects = 14,
        .taken = 10,
        .lines = 15,
        .again = 7,
        .asks = 48,
    };
    check_test_expect(test, ready && node_objects_test_fixture(directory, "labels.c", node_objects_test_labels, quiet, BUSTER_ARRAY_LENGTH(quiet), counted_labels),
                      "node-objects: at reader time an asm label the asm pass will ignore is none, and one it keeps makes a builtin's call a call");
    check_test_expect(test, ready && node_objects_test_fixture(directory, "renames.c", node_objects_test_renames, quiet, BUSTER_ARRAY_LENGTH(quiet), counted_renames),
                      "node-objects: in a unit with a rename pragma T refuses every library builtin's call at both times, and the pragma's labels are counted apart");
    check_test_expect(test, ready && node_objects_test_fixture(directory, "versions.c", node_objects_test_versions, quiet, BUSTER_ARRAY_LENGTH(quiet), counted_versions),
                      "node-objects: a function multiversioned by target strings has the program's own object, and its unit's calls are refused at both times");
    check_test_expect(test, ready && node_objects_test_fixture(directory, "unversioned.c", node_objects_test_unversioned, quiet, BUSTER_ARRAY_LENGTH(quiet),
                                                               counted_unversioned),
                      "node-objects: a unit that declares a multiversioned function and never calls it has its calls refused at both times too");
    check_test_expect(test, node_objects_test_weak_search(), "node-objects: a weak name is found from its token on, in lists of every length to 20");
    check_test_expect(test, node_objects_test_comparisons(), "node-objects: the shadow's comparisons of an object's facts, of a pragma's label and of a name's line, a row a case");
    static const NodeObjectsCounted counted_pragmas = {
        .objects = 3,
        .taken = 2,
        .library = 1,
        .lines = 4,
        .asks = 20,
    };
    check_test_expect(test, ready && node_objects_test_fixture(directory, "pragmas.c", node_objects_test_pragmas, quiet, BUSTER_ARRAY_LENGTH(quiet), counted_pragmas),
                      "node-objects: a weak pragma is no rename pragma, and a labelled builtin's call beside it is a call T takes");
    static const NodeObjectsCounted counted_overloads = {
        .objects = 8,
        .taken = 2,
        .callees = 1,
        .lines = 8,
        .asks = 10,
    };
    check_test_expect(test, ready && node_objects_test_fixture(directory, "overloads.c", node_objects_test_overloads, quiet, BUSTER_ARRAY_LENGTH(quiet), counted_overloads),
                      "node-objects: a unit that declares an overloadable function has every call refused at both times");
    check_test_expect(test, ready && node_objects_test_fixture(directory, "weak.c", node_objects_test_weak, quiet, BUSTER_ARRAY_LENGTH(quiet), counted_weak),
                      "node-objects: the kernel's value of a name is weak from the token the reader notes it at, whichever declaration the use binds");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "enums.c", node_census_test_enums, enums, BUSTER_ARRAY_LENGTH(enums),
                                                             BUSTER_NODE_FRONT_AFTER, 1, 1, windowed_enums),
                      "node-tests: at reader time an enumeration's constant is tested where no mix of enumerations is asked about");
    check_test_expect(test, ready && node_tests_test_fixture(directory, "off.c", node_census_test_off, off, BUSTER_ARRAY_LENGTH(off), BUSTER_NODE_FRONT_AFTER, 1, 3, windowed_off),
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
    // C on the window: its own fixture's counts, each rule on both sides.
    static const NodeContextsExpected contexts_expected = {
        .expressions = 2536,
        .decided = 2270,
        .refused = 61,
        .strides = 241,
        .asked = 7,
        .converted = 7,
        .far = 71,
        .updates = 866,
        .updates_taken = 854,
        .branched = 1722,
        .branched_roots = 523,
        .valued = 470,
        .sunk = 18,
        .commas = 224,
        .listed = {
            [BUSTER_NODE_CONTEXTS_REASON_CALL] = 43,
            [BUSTER_NODE_CONTEXTS_REASON_LOGICAL] = 183,
            [BUSTER_NODE_CONTEXTS_REASON_UPDATE] = 15,
            [BUSTER_NODE_CONTEXTS_REASON_COMMA] = 5,
            [BUSTER_NODE_CONTEXTS_REASON_DIVISION] = 2,
            [BUSTER_NODE_CONTEXTS_REASON_WIDE] = 16,
            [BUSTER_NODE_CONTEXTS_REASON_OPERATOR] = 2,
        },
        .calls = 362,
        .calls_made = 336,
        .calls_converted = 312,
        .calls_plain = 303,
        .calls_left = 21,
        .calls_compared = 309,
        .single_answers = 2543,
        .single_timed = 363,
    };
    check_test_expect(test, ready && node_contexts_test_fixture(directory, "contexts.c", node_contexts_test_source, quiet, BUSTER_ARRAY_LENGTH(quiet), &contexts_expected),
                      "node-contexts: C on the window decides each expression of its rules as C does on T's columns, and lists the kinds it has no case for");
    static const NodeContextsExpected spaced_expected = {
        .expressions = 145,
        .decided = 75,
        .strides = 4,
        .updates = 143,
        .updates_taken = 73,
        .listed = {
            [BUSTER_NODE_CONTEXTS_REASON_UPDATE] = 70,
        },
    };
    check_test_expect(test, ready && node_contexts_test_fixture(directory, "spaced.c", node_contexts_test_spaced_source, quiet, BUSTER_ARRAY_LENGTH(quiet), &spaced_expected),
                      "node-contexts: in a unit with a type in a space of its own, C on the window lists an update whose destination is in a segment's and takes the others");
    // X's rows on the window: its tables; its own fixture's counts, a line a shape; and C's fixture's, each of C's
    // rules' records.
    check_test_expect(test, node_rows_test_tables(), "node-rows: each shape's row has its length's entries, and no class's source where the rows' write resolves none");
    static const NodeRowsExpected rows_expected = {
        .expressions = 1379,
        .taken = 1165,
        .entries = 9509,
        .listed_update = 133,
        .listed_branches = 69,
        .listed_conversion = 10,
        .listed_floating = 2,
        .written = 11147,
        .words = 405,
        .lines = 625,
        .updates = 681,
        .branched = 714,
        .lists = {
            [BUSTER_NODE_ROWS_LIST_FIRST] = 2318,
            [BUSTER_NODE_ROWS_LIST_STORED] = 380,
            [BUSTER_NODE_ROWS_LIST_CONSTANT] = 115,
            [BUSTER_NODE_ROWS_LIST_STRIDE] = 31,
            [BUSTER_NODE_ROWS_LIST_CONTROLLED] = 521,
            [BUSTER_NODE_ROWS_LIST_REST] = 13,
            [BUSTER_NODE_ROWS_LIST_SECOND] = 68,
        },
        .numbered = 68,
        .steps = 473,
        .control_valued = 291,
        .chunks = 9,
    };
    check_test_expect(test, ready && node_rows_test_fixture(directory, "rows.c", node_rows_test_source, quiet, BUSTER_ARRAY_LENGTH(quiet), &rows_expected),
                      "node-rows: X's rows on the window make each shape's entries' kinds and classes as X does on T's columns, and list what they don't make");
    static const NodeRowsExpected rows_contexts_expected = {
        .expressions = 2209,
        .taken = 2188,
        .entries = 20946,
        .listed_update = 0,
        .listed_branches = 5,
        .listed_conversion = 10,
        .listed_floating = 6,
        .written = 23351,
        .words = 536,
        .lines = 501,
        .updates = 854,
        .branched = 1667,
        .lists = {
            [BUSTER_NODE_ROWS_LIST_FIRST] = 6396,
            [BUSTER_NODE_ROWS_LIST_STORED] = 693,
            [BUSTER_NODE_ROWS_LIST_CONSTANT] = 595,
            [BUSTER_NODE_ROWS_LIST_STRIDE] = 237,
            [BUSTER_NODE_ROWS_LIST_CONTROLLED] = 1105,
            [BUSTER_NODE_ROWS_LIST_REST] = 339,
            [BUSTER_NODE_ROWS_LIST_SECOND] = 239,
        },
        .numbered = 223,
        .steps = 1895,
        .control_valued = 547,
        .chunks = 23,
    };
    check_test_expect(test, ready && node_rows_test_fixture(directory, "rows-contexts.c", node_contexts_test_source, quiet, BUSTER_ARRAY_LENGTH(quiet), &rows_contexts_expected),
                      "node-rows: on C's fixture, X's rows on the window make the tape of each expression C takes there, or list it");
    static const NodeRowsExpected rows_single_expected = {
        .expressions = 3,
        .taken = 3,
        .entries = 10,
        .listed_update = 0,
        .listed_branches = 0,
        .listed_conversion = 0,
        .listed_floating = 0,
        .written = 10,
        .words = 0,
        .lines = 0,
        .updates = 1,
        .branched = 0,
        .lists = {
            [BUSTER_NODE_ROWS_LIST_FIRST] = 3,
            [BUSTER_NODE_ROWS_LIST_STORED] = 1,
            [BUSTER_NODE_ROWS_LIST_CONSTANT] = 0,
            [BUSTER_NODE_ROWS_LIST_STRIDE] = 0,
            [BUSTER_NODE_ROWS_LIST_CONTROLLED] = 0,
            [BUSTER_NODE_ROWS_LIST_REST] = 0,
            [BUSTER_NODE_ROWS_LIST_SECOND] = 0,
        },
        .numbered = 0,
        .steps = 0,
        .control_valued = 0,
        .chunks = 1,
    };
    check_test_expect(test, ready && node_rows_test_fixture(directory, "rows-single.c", node_rows_test_single_source, quiet, BUSTER_ARRAY_LENGTH(quiet), &rows_single_expected),
                      "node-rows: in a chunk with one update, X's rows on the window make its two rows");
    static const NodeRowsExpected rows_branch_expected = {
        .expressions = 3,
        .taken = 3,
        .entries = 11,
        .listed_update = 0,
        .listed_branches = 0,
        .listed_conversion = 0,
        .listed_floating = 0,
        .written = 11,
        .words = 0,
        .lines = 0,
        .updates = 0,
        .branched = 1,
        .lists = {
            [BUSTER_NODE_ROWS_LIST_FIRST] = 2,
            [BUSTER_NODE_ROWS_LIST_STORED] = 0,
            [BUSTER_NODE_ROWS_LIST_CONSTANT] = 2,
            [BUSTER_NODE_ROWS_LIST_STRIDE] = 0,
            [BUSTER_NODE_ROWS_LIST_CONTROLLED] = 2,
            [BUSTER_NODE_ROWS_LIST_REST] = 0,
            [BUSTER_NODE_ROWS_LIST_SECOND] = 0,
        },
        .numbered = 0,
        .steps = 0,
        .control_valued = 0,
        .chunks = 1,
    };
    check_test_expect(test, ready && node_rows_test_fixture(directory, "rows-branch.c", node_rows_test_branch_source, quiet, BUSTER_ARRAY_LENGTH(quiet), &rows_branch_expected),
                      "node-rows: in a chunk with one condition evaluated as branches and no update, X's rows on the window make its tested values' tests");
    static const NodeRowsExpected rows_values_expected = {
        .expressions = 1064,
        .taken = 1058,
        .entries = 9877,
        .listed_update = 3,
        .listed_branches = 0,
        .listed_conversion = 0,
        .listed_floating = 3,
        .written = 9923,
        .words = 350,
        .lines = 259,
        .updates = 416,
        .branched = 356,
        .lists = {
            [BUSTER_NODE_ROWS_LIST_FIRST] = 2322,
            [BUSTER_NODE_ROWS_LIST_STORED] = 618,
            [BUSTER_NODE_ROWS_LIST_CONSTANT] = 218,
            [BUSTER_NODE_ROWS_LIST_STRIDE] = 178,
            [BUSTER_NODE_ROWS_LIST_CONTROLLED] = 326,
            [BUSTER_NODE_ROWS_LIST_REST] = 220,
            [BUSTER_NODE_ROWS_LIST_SECOND] = 281,
        },
        .numbered = 244,
        .steps = 724,
        .control_valued = 264,
        .chunks = 10,
    };
    check_test_expect(test, ready && node_rows_test_fixture(directory, "rows-values.c", node_rows_test_values_source, quiet, BUSTER_ARRAY_LENGTH(quiet), &rows_values_expected),
                      "node-rows: X's rows on the window make each entry's value as X does on T's columns: a line's field, a stride, a fixed number or its number in its expression");
    // A summary's side facts on the window: its own fixture's, and a unit's with a function the return flow leaves
    // unknown from a declaration on.
    check_test_expect(test, node_summary_test_selectors(quiet, BUSTER_ARRAY_LENGTH(quiet), false) && node_summary_test_selectors(loud, BUSTER_ARRAY_LENGTH(loud), true),
                      "node-summary: a switch retains its selector type before promotion and the comparison catches every changed type");
    static const NodeSummaryExpected summary_expected = {
        .taken = 375,
        .references = 804,
        .program_references = 801,
        .listed = 808,
        .suspects = 362,
        .unknowns = 146,
        .stops = 74,
        .reduced = 141,
        .decided = 9,
        .conditions = 16,
        .truths = 3,
        .folds = 5,
        .control_asked = 389,
        .control_truths = 4,
        .control_folds = 2,
        .chunks = 6,
    };
    check_test_expect(test, ready && node_summary_test_fixture(directory, "summaries.c", node_summary_test_source, quiet, BUSTER_ARRAY_LENGTH(quiet), &summary_expected),
                      "node-summary: an expression's references and return fact on the window are the program's own walks', a kept condition's truth is its value's and a kept constant folds");
    // The ordinary checker folds a strong pointer comparison in these required constant contexts. In particular,
    // only the selected string in the conditional initializer is referenced alongside the condition's string.
    char pointer_path[PATH_MAX + 128];
    snprintf(pointer_path, sizeof(pointer_path), "%s/checker/sources/pointer-constant-equality.c", check_test_fixture_root);
    NodeShadow pointer_shadow = {
        .verify = true, .front = BUSTER_NODE_FRONT_AFTER | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | BUSTER_NODE_FRONT_LEAN,
        .reader = BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_VERIFY | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW |
                  BUSTER_NODE_READER_WINDOW_TESTS | BUSTER_NODE_READER_WINDOW_CONTEXTS | BUSTER_NODE_READER_WINDOW_ROWS,
        .repeat = 1,
    };
    NodeCensus pointer_census = { .shadow = &pointer_shadow };
    check_test_expect(test, ready && node_census_file(&pointer_census, pointer_path, quiet, BUSTER_ARRAY_LENGTH(quiet)) && pointer_census.files == 1 &&
                                pointer_census.failures == 0 && pointer_shadow.typed_expressions != 0 && !node_shadow_differs(&pointer_shadow),
                      "node-summary: a static pointer comparison in a conditional initializer agrees with the ordinary reference walk");
    static const NodeSummaryExpected summary_unknowns_expected = {
        .taken = 16,
        .references = 15,
        .program_references = 14,
        .listed = 15,
        .suspects = 15,
        .unknowns = 6,
        .stops = 2,
        .reduced = 1,
        .decided = 0,
        .conditions = 2,
        .truths = 0,
        .folds = 0,
        .control_asked = 16,
        .control_truths = 0,
        .control_folds = 0,
        .chunks = 1,
    };
    check_test_expect(test, ready && node_summary_test_fixture(directory, "summaries-unknowns.c", node_summary_test_unknowns_source, quiet, BUSTER_ARRAY_LENGTH(quiet),
                                                               &summary_unknowns_expected),
                      "node-summary: in a unit with a function unknown from a declaration on, each call is asked a record at a time, and one after that declaration is unknown");
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
    // A hardware-independent check of the two denominators. Two runs over two different-sized chunks
    // contribute both deltas but count each chunk and its records once.
    NodeWindowCounter counter = { .descriptor = -1 };
    const uint64_t first[] = { 80, 100, 107 };
    const uint64_t second[] = { 160, 170, 173 };
    for (uint32_t size = 3; size <= 9; size += 6)
    {
        node_window_collect(&counter, 0, first, 2, size);
        node_window_collect(&counter, 1, second, 2, size);
    }
    check_test_expect(test, !counter.failed && counter.chunks == 2 && counter.records == 12 && counter.sums[0][0] == 40 && counter.sums[0][1] == 14 &&
                      counter.sums[1][0] == 20 && counter.sums[1][1] == 6, "window counter sums each sweep over both sizes and each run");
    node_window_collect(&counter, 0, (const uint64_t[]){ 20, 19 }, 1, 3);
    check_test_expect(test, counter.failed && node_window_select(&counter, "rows", "cycles") && !node_window_select(&counter, "rows", "missing") &&
                      !node_window_select(&counter, "missing", "cycles"), "window counter rejects a reversed interval and unknown event or family");

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
