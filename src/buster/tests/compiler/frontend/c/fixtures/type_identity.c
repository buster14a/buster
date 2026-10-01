static int selected_calls;
static int unselected_calls;

static int selected(void)
{
    selected_calls += 1;
    return 17;
}

static int unselected(void)
{
    unselected_calls += 1;
    return 99;
}

/* Build: -std=gnu17 (for __builtin_types_compatible_p / typeof). Exits 0 when every check holds. */

static int failures;
#define CHECK(name, got, want) do { int g_ = (got); if (g_ != (want)) { failures += 1; } } while (0)

static void take_int_pointer(int *p) { (void)p; }

static const char *g_cp;
static volatile int g_vi;
static const int g_ci;
#define SEL_CONST_PTR(x) _Generic((x), char *: 1, default: 2)
#define SEL_VOLATILE(x) _Generic((x), int: 1, default: 2)
#define SEL_CONST_TOP(x) _Generic((x), const int: 1, default: 2)

enum { E_CONST_PTR = SEL_CONST_PTR(g_cp), E_VOLATILE = SEL_VOLATILE(g_vi), E_CONST_TOP = SEL_CONST_TOP(g_ci) };
static int s_const_ptr = SEL_CONST_PTR(g_cp);
static int s_volatile = SEL_VOLATILE(g_vi);
static int s_const_top = SEL_CONST_TOP(g_ci);
static char a_const_ptr[SEL_CONST_PTR(g_cp)];
static char a_volatile[SEL_VOLATILE(g_vi)];

static int identity_family(void)
{
    char buffer[2] = "m";
    const char *cp = "c";
    char *mp = buffer;
    const int ci = 1;
    volatile int vi = 2;
    _Atomic int ai = 3;
    int i = 4;
    int *p = &i;
    int *restrict rp = &i;
    volatile int *vp = &vi;
    const int ca[3] = {1, 2, 3};
    int ma[3] = {4, 5, 6};
    const char **cpp = &cp;
    char *const *mpc = &mp;

    CHECK("P1", _Generic(cp, char *: 1, default: 2), 2);
    CHECK("P2", _Generic(mp, const char *: 1, default: 2), 2);
    CHECK("P3", _Generic("lit", const char *: 1, default: 2), 2);
    CHECK("P4", _Generic(ca, int *: 1, default: 2), 2);
    CHECK("P5", _Generic(ma, const int *: 1, default: 2), 2);
    CHECK("P6", _Generic(cpp, char **: 1, default: 2), 2);
    CHECK("P7", _Generic(mpc, char **: 1, default: 2), 2);
    CHECK("P8", _Generic(cp, char *: 1, const char *: 2), 2);
    CHECK("P9", _Generic(mp, char *: 1, const char *: 2), 1);
    CHECK("L1", _Generic(ci, const int: 1, default: 2), 2);
    CHECK("L2", _Generic(vi, int: 1, default: 2), 1);
    CHECK("L3", _Generic(ai, int: 1, default: 2), 1);
    CHECK("L4", _Generic(+vi, int: 1, default: 2), 1);
    CHECK("L5", _Generic(vi, int: 1, volatile int: 2, default: 3), 1);
    CHECK("F1", _Generic(take_int_pointer, void (*)(int *): 1, default: 2), 1);
    CHECK("F2", _Generic(&take_int_pointer, void (*)(int *): 1, default: 2), 1);
    CHECK("C1", _Generic(1 ? cp : mp, char *: 1, default: 2), 2);
    CHECK("C2", _Generic(1 ? p : vp, int *: 1, default: 2), 2);
    CHECK("C3", _Generic(1 ? vp : p, volatile int *: 1, default: 2), 1);
    CHECK("C4", _Generic(0 ? (mp) : (void *)1, const void *: 1, default: 2), 2);
    CHECK("C5", _Generic(0 ? (cp) : (void *)1, const void *: 1, default: 2), 1);
    CHECK("K1", s_const_ptr, E_CONST_PTR);
    CHECK("K2", s_volatile, E_VOLATILE);
    CHECK("K3", s_const_top, E_CONST_TOP);
    CHECK("K4", (int)sizeof a_const_ptr, E_CONST_PTR);
    CHECK("K5", (int)sizeof a_volatile, E_VOLATILE);
    CHECK("K6", SEL_CONST_PTR(cp), E_CONST_PTR);
    CHECK("K7", SEL_VOLATILE(vi), E_VOLATILE);
    {
        int label = 0;
        switch (2) { case SEL_CONST_PTR(g_cp): label = 2; break; default: label = 1; break; }
        CHECK("K8", label, E_CONST_PTR);
    }
    CHECK("K9", E_CONST_PTR * 100 + E_VOLATILE * 10 + E_CONST_TOP, 212);
    CHECK("B1", __builtin_types_compatible_p(const char *, char *), 0);
    CHECK("B2", __builtin_types_compatible_p(volatile int *, int *), 0);
    CHECK("B3", __builtin_types_compatible_p(char **, const char **), 0);
    CHECK("B4", __builtin_types_compatible_p(typeof(vp), typeof(p)), 0);
    CHECK("B5", __builtin_types_compatible_p(typeof(cp), char *), 0);
    CHECK("N1", _Generic(ci, int: 1, default: 2), 1);
    CHECK("N2", _Generic(rp, int *: 1, default: 2), 1);
    CHECK("N3", _Generic(vp, int *: 1, default: 2), 2);
    CHECK("N4", _Generic((const int)i, int: 1, default: 2), 1);
    CHECK("N5", _Generic((void (*)(const char *))0, void (*)(char *): 1, default: 2), 2);
    CHECK("N6", _Generic(0 ? (cp) : (void *)1, const void *: 1, default: 2), 1);
    CHECK("N7", __builtin_types_compatible_p(const int, int), 1);
    CHECK("N8", __builtin_types_compatible_p(int *restrict, int *), 1);
    CHECK("N9", __builtin_types_compatible_p(volatile int, int), 1);
    CHECK("N10", E_CONST_PTR * 100 + E_VOLATILE * 10 + E_CONST_TOP, 212);
    return failures != 0;
}


static char *g_mp;
static const int g_ca[3];
static int g_ma[3];
static const char **g_cpp;
static char *const *g_mpc;
static _Atomic int g_ai;
static int *g_p;
static volatile int *g_vp;

/* C17 conversion/compatibility expectations, repeated across all folds. */
#define ID_P1 (_Generic(g_cp, char *: 1, default: 2))
enum { E_P1 = ID_P1 };
_Static_assert(E_P1 == 2, "enumerator P1");
_Static_assert(ID_P1 == 2, "identity P1");
static int S_P1 = ID_P1;
static char A_P1[ID_P1 + 1];
static int parity_P1(void)
{
    int failed = S_P1 != 2 || sizeof A_P1 != 3 || ID_P1 != 2;
    switch (2) { case ID_P1: break; default: failed = 1; break; }
    return failed;
}

#define ID_P2 (_Generic(g_mp, const char *: 1, default: 2))
enum { E_P2 = ID_P2 };
_Static_assert(E_P2 == 2, "enumerator P2");
_Static_assert(ID_P2 == 2, "identity P2");
static int S_P2 = ID_P2;
static char A_P2[ID_P2 + 1];
static int parity_P2(void)
{
    int failed = S_P2 != 2 || sizeof A_P2 != 3 || ID_P2 != 2;
    switch (2) { case ID_P2: break; default: failed = 1; break; }
    return failed;
}

#define ID_P3 (_Generic("lit", const char *: 1, default: 2))
enum { E_P3 = ID_P3 };
_Static_assert(E_P3 == 2, "enumerator P3");
_Static_assert(ID_P3 == 2, "identity P3");
static int S_P3 = ID_P3;
static char A_P3[ID_P3 + 1];
static int parity_P3(void)
{
    int failed = S_P3 != 2 || sizeof A_P3 != 3 || ID_P3 != 2;
    switch (2) { case ID_P3: break; default: failed = 1; break; }
    return failed;
}

#define ID_P4 (_Generic(g_ca, int *: 1, default: 2))
enum { E_P4 = ID_P4 };
_Static_assert(E_P4 == 2, "enumerator P4");
_Static_assert(ID_P4 == 2, "identity P4");
static int S_P4 = ID_P4;
static char A_P4[ID_P4 + 1];
static int parity_P4(void)
{
    int failed = S_P4 != 2 || sizeof A_P4 != 3 || ID_P4 != 2;
    switch (2) { case ID_P4: break; default: failed = 1; break; }
    return failed;
}

#define ID_P5 (_Generic(g_ma, const int *: 1, default: 2))
enum { E_P5 = ID_P5 };
_Static_assert(E_P5 == 2, "enumerator P5");
_Static_assert(ID_P5 == 2, "identity P5");
static int S_P5 = ID_P5;
static char A_P5[ID_P5 + 1];
static int parity_P5(void)
{
    int failed = S_P5 != 2 || sizeof A_P5 != 3 || ID_P5 != 2;
    switch (2) { case ID_P5: break; default: failed = 1; break; }
    return failed;
}

#define ID_P6 (_Generic(g_cpp, char **: 1, default: 2))
enum { E_P6 = ID_P6 };
_Static_assert(E_P6 == 2, "enumerator P6");
_Static_assert(ID_P6 == 2, "identity P6");
static int S_P6 = ID_P6;
static char A_P6[ID_P6 + 1];
static int parity_P6(void)
{
    int failed = S_P6 != 2 || sizeof A_P6 != 3 || ID_P6 != 2;
    switch (2) { case ID_P6: break; default: failed = 1; break; }
    return failed;
}

#define ID_P7 (_Generic(g_mpc, char **: 1, default: 2))
enum { E_P7 = ID_P7 };
_Static_assert(E_P7 == 2, "enumerator P7");
_Static_assert(ID_P7 == 2, "identity P7");
static int S_P7 = ID_P7;
static char A_P7[ID_P7 + 1];
static int parity_P7(void)
{
    int failed = S_P7 != 2 || sizeof A_P7 != 3 || ID_P7 != 2;
    switch (2) { case ID_P7: break; default: failed = 1; break; }
    return failed;
}

#define ID_P8 (_Generic(g_cp, char *: 1, const char *: 2))
enum { E_P8 = ID_P8 };
_Static_assert(E_P8 == 2, "enumerator P8");
_Static_assert(ID_P8 == 2, "identity P8");
static int S_P8 = ID_P8;
static char A_P8[ID_P8 + 1];
static int parity_P8(void)
{
    int failed = S_P8 != 2 || sizeof A_P8 != 3 || ID_P8 != 2;
    switch (2) { case ID_P8: break; default: failed = 1; break; }
    return failed;
}

#define ID_P9 (_Generic(g_mp, char *: 1, const char *: 2))
enum { E_P9 = ID_P9 };
_Static_assert(E_P9 == 1, "enumerator P9");
_Static_assert(ID_P9 == 1, "identity P9");
static int S_P9 = ID_P9;
static char A_P9[ID_P9 + 1];
static int parity_P9(void)
{
    int failed = S_P9 != 1 || sizeof A_P9 != 2 || ID_P9 != 1;
    switch (1) { case ID_P9: break; default: failed = 1; break; }
    return failed;
}

#define ID_L1 (SEL_CONST_TOP(g_ci))
enum { E_L1 = ID_L1 };
_Static_assert(E_L1 == 2, "enumerator L1");
_Static_assert(ID_L1 == 2, "identity L1");
static int S_L1 = ID_L1;
static char A_L1[ID_L1 + 1];
static int parity_L1(void)
{
    int failed = S_L1 != 2 || sizeof A_L1 != 3 || ID_L1 != 2;
    switch (2) { case ID_L1: break; default: failed = 1; break; }
    return failed;
}

#define ID_L2 (SEL_VOLATILE(g_vi))
enum { E_L2 = ID_L2 };
_Static_assert(E_L2 == 1, "enumerator L2");
_Static_assert(ID_L2 == 1, "identity L2");
static int S_L2 = ID_L2;
static char A_L2[ID_L2 + 1];
static int parity_L2(void)
{
    int failed = S_L2 != 1 || sizeof A_L2 != 2 || ID_L2 != 1;
    switch (1) { case ID_L2: break; default: failed = 1; break; }
    return failed;
}

#define ID_L3 (_Generic(g_ai, int: 1, default: 2))
enum { E_L3 = ID_L3 };
_Static_assert(E_L3 == 1, "enumerator L3");
_Static_assert(ID_L3 == 1, "identity L3");
static int S_L3 = ID_L3;
static char A_L3[ID_L3 + 1];
static int parity_L3(void)
{
    int failed = S_L3 != 1 || sizeof A_L3 != 2 || ID_L3 != 1;
    switch (1) { case ID_L3: break; default: failed = 1; break; }
    return failed;
}

#define ID_L4 (SEL_VOLATILE(+g_vi))
enum { E_L4 = ID_L4 };
_Static_assert(E_L4 == 1, "enumerator L4");
_Static_assert(ID_L4 == 1, "identity L4");
static int S_L4 = ID_L4;
static char A_L4[ID_L4 + 1];
static int parity_L4(void)
{
    int failed = S_L4 != 1 || sizeof A_L4 != 2 || ID_L4 != 1;
    switch (1) { case ID_L4: break; default: failed = 1; break; }
    return failed;
}

#define ID_L5 (_Generic(g_vi, int: 1, volatile int: 2, default: 3))
enum { E_L5 = ID_L5 };
_Static_assert(E_L5 == 1, "enumerator L5");
_Static_assert(ID_L5 == 1, "identity L5");
static int S_L5 = ID_L5;
static char A_L5[ID_L5 + 1];
static int parity_L5(void)
{
    int failed = S_L5 != 1 || sizeof A_L5 != 2 || ID_L5 != 1;
    switch (1) { case ID_L5: break; default: failed = 1; break; }
    return failed;
}

#define ID_F1 (_Generic(take_int_pointer, void (*)(int *): 1, default: 2))
enum { E_F1 = ID_F1 };
_Static_assert(E_F1 == 1, "enumerator F1");
_Static_assert(ID_F1 == 1, "identity F1");
static int S_F1 = ID_F1;
static char A_F1[ID_F1 + 1];
static int parity_F1(void)
{
    int failed = S_F1 != 1 || sizeof A_F1 != 2 || ID_F1 != 1;
    switch (1) { case ID_F1: break; default: failed = 1; break; }
    return failed;
}

#define ID_F2 (_Generic(&take_int_pointer, void (*)(int *): 1, default: 2))
enum { E_F2 = ID_F2 };
_Static_assert(E_F2 == 1, "enumerator F2");
_Static_assert(ID_F2 == 1, "identity F2");
static int S_F2 = ID_F2;
static char A_F2[ID_F2 + 1];
static int parity_F2(void)
{
    int failed = S_F2 != 1 || sizeof A_F2 != 2 || ID_F2 != 1;
    switch (1) { case ID_F2: break; default: failed = 1; break; }
    return failed;
}

#define ID_C1 (_Generic(1 ? g_cp : g_mp, char *: 1, default: 2))
enum { E_C1 = ID_C1 };
_Static_assert(E_C1 == 2, "enumerator C1");
_Static_assert(ID_C1 == 2, "identity C1");
static int S_C1 = ID_C1;
static char A_C1[ID_C1 + 1];
static int parity_C1(void)
{
    int failed = S_C1 != 2 || sizeof A_C1 != 3 || ID_C1 != 2;
    switch (2) { case ID_C1: break; default: failed = 1; break; }
    return failed;
}

#define ID_C2 (_Generic(1 ? g_p : g_vp, int *: 1, default: 2))
enum { E_C2 = ID_C2 };
_Static_assert(E_C2 == 2, "enumerator C2");
_Static_assert(ID_C2 == 2, "identity C2");
static int S_C2 = ID_C2;
static char A_C2[ID_C2 + 1];
static int parity_C2(void)
{
    int failed = S_C2 != 2 || sizeof A_C2 != 3 || ID_C2 != 2;
    switch (2) { case ID_C2: break; default: failed = 1; break; }
    return failed;
}

#define ID_C3 (_Generic(1 ? g_vp : g_p, volatile int *: 1, default: 2))
enum { E_C3 = ID_C3 };
_Static_assert(E_C3 == 1, "enumerator C3");
_Static_assert(ID_C3 == 1, "identity C3");
static int S_C3 = ID_C3;
static char A_C3[ID_C3 + 1];
static int parity_C3(void)
{
    int failed = S_C3 != 1 || sizeof A_C3 != 2 || ID_C3 != 1;
    switch (1) { case ID_C3: break; default: failed = 1; break; }
    return failed;
}

#define ID_C4 (_Generic(0 ? g_mp : (void *)1, const void *: 1, default: 2))
enum { E_C4 = ID_C4 };
_Static_assert(E_C4 == 2, "enumerator C4");
_Static_assert(ID_C4 == 2, "identity C4");
static int S_C4 = ID_C4;
static char A_C4[ID_C4 + 1];
static int parity_C4(void)
{
    int failed = S_C4 != 2 || sizeof A_C4 != 3 || ID_C4 != 2;
    switch (2) { case ID_C4: break; default: failed = 1; break; }
    return failed;
}

#define ID_C5 (_Generic(0 ? g_cp : (void *)1, const void *: 1, default: 2))
enum { E_C5 = ID_C5 };
_Static_assert(E_C5 == 1, "enumerator C5");
_Static_assert(ID_C5 == 1, "identity C5");
static int S_C5 = ID_C5;
static char A_C5[ID_C5 + 1];
static int parity_C5(void)
{
    int failed = S_C5 != 1 || sizeof A_C5 != 2 || ID_C5 != 1;
    switch (1) { case ID_C5: break; default: failed = 1; break; }
    return failed;
}

#define ID_B1 (__builtin_types_compatible_p(const char *, char *))
enum { E_B1 = ID_B1 };
_Static_assert(E_B1 == 0, "enumerator B1");
_Static_assert(ID_B1 == 0, "identity B1");
static int S_B1 = ID_B1;
static char A_B1[ID_B1 + 1];
static int parity_B1(void)
{
    int failed = S_B1 != 0 || sizeof A_B1 != 1 || ID_B1 != 0;
    switch (0) { case ID_B1: break; default: failed = 1; break; }
    return failed;
}

#define ID_B2 (__builtin_types_compatible_p(volatile int *, int *))
enum { E_B2 = ID_B2 };
_Static_assert(E_B2 == 0, "enumerator B2");
_Static_assert(ID_B2 == 0, "identity B2");
static int S_B2 = ID_B2;
static char A_B2[ID_B2 + 1];
static int parity_B2(void)
{
    int failed = S_B2 != 0 || sizeof A_B2 != 1 || ID_B2 != 0;
    switch (0) { case ID_B2: break; default: failed = 1; break; }
    return failed;
}

#define ID_B3 (__builtin_types_compatible_p(char **, const char **))
enum { E_B3 = ID_B3 };
_Static_assert(E_B3 == 0, "enumerator B3");
_Static_assert(ID_B3 == 0, "identity B3");
static int S_B3 = ID_B3;
static char A_B3[ID_B3 + 1];
static int parity_B3(void)
{
    int failed = S_B3 != 0 || sizeof A_B3 != 1 || ID_B3 != 0;
    switch (0) { case ID_B3: break; default: failed = 1; break; }
    return failed;
}

#define ID_B4 (__builtin_types_compatible_p(typeof(g_vp), typeof(g_p)))
enum { E_B4 = ID_B4 };
_Static_assert(E_B4 == 0, "enumerator B4");
_Static_assert(ID_B4 == 0, "identity B4");
static int S_B4 = ID_B4;
static char A_B4[ID_B4 + 1];
static int parity_B4(void)
{
    int failed = S_B4 != 0 || sizeof A_B4 != 1 || ID_B4 != 0;
    switch (0) { case ID_B4: break; default: failed = 1; break; }
    return failed;
}

#define ID_B5 (__builtin_types_compatible_p(typeof(g_cp), char *))
enum { E_B5 = ID_B5 };
_Static_assert(E_B5 == 0, "enumerator B5");
_Static_assert(ID_B5 == 0, "identity B5");
static int S_B5 = ID_B5;
static char A_B5[ID_B5 + 1];
static int parity_B5(void)
{
    int failed = S_B5 != 0 || sizeof A_B5 != 1 || ID_B5 != 0;
    switch (0) { case ID_B5: break; default: failed = 1; break; }
    return failed;
}

static int identity_parity(void)
{
    return parity_P1() || parity_P2() || parity_P3() || parity_P4() || parity_P5() || parity_P6() || parity_P7() || parity_P8() || parity_P9() || parity_L1() || parity_L2() || parity_L3() || parity_L4() || parity_L5() || parity_F1() || parity_F2() || parity_C1() || parity_C2() || parity_C3() || parity_C4() || parity_C5() || parity_B1() || parity_B2() || parity_B3() || parity_B4() || parity_B5();
}

static volatile int qualified_result(void)
{
    return 2;
}

static int identity_conversions(void)
{
    volatile int v = 3;
    volatile int *pointer = &v;
    struct { volatile int m; _Atomic int a; } fields = {0};
    int failed = _Generic((v = 5), int: 0, default: 1);
    failed |= _Generic((v += 1), int: 0, default: 1);
    failed |= _Generic(++v, int: 0, default: 1);
    failed |= _Generic(fields.m, int: 0, default: 1);
    failed |= _Generic(fields.a, int: 0, default: 1);
    failed |= _Generic(*pointer, int: 0, default: 1);
    failed |= _Generic((0, v), int: 0, default: 1);
    failed |= _Generic(1 ? v : v, int: 0, default: 1);
    failed |= _Generic((volatile int)0, int: 0, default: 1);
    failed |= _Generic(({ v; }), int: 0, default: 1);
    failed |= _Generic(qualified_result(), int: 0, default: 1);
    failed |= v != 3;
    failed |= _Generic(&g_ca, const int (*)[3]: 0, default: 1);
    failed |= !__builtin_types_compatible_p(int (*)[3], int (*)[3]);
    failed |= __builtin_types_compatible_p(const int (*)[3], int (*)[3]);
    failed |= !__builtin_types_compatible_p(void (*)(const int), void (*)(int));
    return failed;
}

int main(void)
{
    int control = 0;
    typeof(_Generic(control, default: (char)0)) typed = 0;
    failures += sizeof typed != 1 || sizeof(_Generic(control, default: (char)0)) != 1;
    double floating = 2.0;
    int* pointer = &control;
    int first = _Generic(control++, int: selected(), default: unselected());
    int second = _Generic(floating, int: unselected(), double: 23, default: (control ? unselected() : unselected()));
    int third = 3 + _Generic(pointer, int*: 29, default: unselected());
    int nested = _Generic(_Generic(floating, double: control, default: floating), int: 31, default: unselected());
    int string_type = _Generic("buster", char*: 37, default: unselected());
    return identity_family() || identity_parity() || identity_conversions() || first != 17 || second != 23 || third != 32 || nested != 31 || string_type != 37 || control != 0 || selected_calls != 1 || unselected_calls != 0;
}
