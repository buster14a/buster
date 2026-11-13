int equality(int x) { if ((x == 1)) return 1; return 0; }
int inequality(int x) { if ((x != 1)) return 1; return 0; }
int relation(int x) { if ((x < 1)) return 1; return 0; }
int equality_nested(int x) { if (((x == 1))) return 1; return 0; }
int equality_while(int x) { while ((x == 1)) x++; return x; }
int equality_for(int x) { for (; (x == 1); x++) {} return x; }
int equality_do(int x) { do { x++; } while ((x == 1)); return x; }
int equality_return(int x) { return (x == 1); }
int logical_and(int x) { return !x & 1; }
int logical_or(int x) { return !x | 1; }
int logical_xor(int x) { return !x ^ 1; }
int logical_rhs(int x, int y) { return x & !y; }
int logical_both(int x, int y) { return !x & !y; }
int logical_bool(int x, _Bool y) { return !x & y; }
int logical_wrapped(int x) { return (!x) & 1; }
int logical_sizeof(int x) { return sizeof(!x & 1); }
int switch_bool(_Bool b) { switch (b) { case 0: return 1; default: return 0; } }
int switch_equal(int x) { switch (x == 1) { case 0: return 1; default: return 0; } }
int switch_not(int x) { switch (!x) { case 0: return 1; default: return 0; } }
int switch_logic(int x, int y) { switch (x && y) { case 0: return 1; default: return 0; } }
int switch_cast(_Bool b) { switch ((int)b) { case 0: return 1; default: return 0; } }
char *cast_int(int x) { return (char *)x; }
char *cast_uint(unsigned x) { return (char *)x; }
char *cast_long(long x) { return (char *)x; }
char *cast_bool(_Bool x) { return (char *)x; }
char *cast_constant(void) { return (char *)1; }
char *cast_constant_expression(void) { return (char *)(1 + 2); }
char *cast_bitint(_BitInt(17) x) { return (char *)x; }
enum E { E0 };
char *cast_enum(enum E x) { return (char *)x; }
int cast_unevaluated(int x) { return sizeof((char *)x); }
typedef int I;
typedef char *P;
P cast_typedef(I x) { return (P)x; }
int sub_char(int *a, char c) { return a[c]; }
int sub_signed(int *a, signed char c) { return a[c]; }
int sub_unsigned(int *a, unsigned char c) { return a[c]; }
int sub_reverse(int *a, char c) { return c[a]; }
int sub_promoted(int *a, char c) { return a[+c]; }
int sub_constant(int *a) { return a[(char)0]; }
int sub_sizeof(int *a, char c) { return sizeof(a[c]); }
typedef char C;
int sub_typedef(int *a, C c) { return a[c]; }
int size_array(int a[10]) { return sizeof(a); }
int size_static(int a[static 10]) { return sizeof a; }
int size_const(const int a[10]) { return sizeof((a)); }
int size_qualified(int a[const 10]) { return sizeof(a); }
int size_vla(int n, int a[n]) { return sizeof(a); }
int size_deref(int a[10]) { return sizeof(*a); }
int size_pointer(int *a) { return sizeof(a); }
int size_address(int a[10]) { return sizeof(&a); }
typedef int A[10];
int size_alias(A a) { return sizeof(a); }
int size_nested(int a[10]) { return sizeof(sizeof(a)); }
int size_align(int a[10]) { return __alignof__(a); }
