// #1393: indirect calls follow the function type of the complete callee expression.
_Noreturn void die(void);
__attribute__((noreturn)) void die2(void);
void die3(void) __attribute__((noreturn));
void die3(void);
void plain(void);
typedef void F(void) __attribute__((noreturn));
typedef void (*NP)(void) __attribute__((noreturn));
typedef void (*P)(void);
typedef NP NP2;
F *f1;
NP np;
NP2 np2;
P pp;
void (*ap)(void) __attribute__((noreturn));
void (*fp)(void);
__typeof__(die2) *t1;
__typeof__(&die2) t2;
__typeof__(die) *t3;
__typeof__(np) t4;
NP arr[2];
void (*parr[2])(void);
struct S { void (*cb)(void); NP ncb; void (*acb)(void) __attribute__((noreturn)); struct { NP inner; } in; } s, *ps;
P get(void);
int c01(int x) { if (x) return 1; f1(); }
int c02(int x) { if (x) return 1; np(); }
int c03(int x) { if (x) return 1; np2(); }
int c04(int x) { if (x) return 1; pp(); }
int c05(int x) { if (x) return 1; ap(); }
int c06(int x) { if (x) return 1; fp(); }
int c07(int x) { if (x) return 1; t1(); }
int c08(int x) { if (x) return 1; t2(); }
int c09(int x) { if (x) return 1; t3(); }
int c10(int x) { if (x) return 1; t4(); }
int c11(int x) { if (x) return 1; arr[x](); }
int c14(int x) { if (x) return 1; s.ncb(); }
int c15(int x) { if (x) return 1; ps->acb(); }
int c16(int x) { if (x) return 1; ps->in.inner(); }
int c17(int x) { if (x) return 1; ((NP)fp)(); }
int c19(int x) { if (x) return 1; (x ? die2 : die3)(); }
int c22(int x) { if (x) return 1; (x ? np : np2)(); }
int c23(int x) { if (x) return 1; (0, die2)(); }
int c25(int x) { if (x) return 1; (*np)(); }
int c27(int x) { if (x) return 1; (&*np)(); }
int c28(void (*cb)(void)) { if (cb) return 1; cb(); }
int c29(NP cb) { if (cb) return 1; cb(); }
int c30(int x) { NP local = np; if (x) return 1; local(); }
int c31(int x) { void (*local)(void) = fp; if (x) return 1; local(); }
int c32(int x) { static NP st; if (x) return 1; st(); }
int c33(int x) { if (x) return 1; (x ? die3 : die2)(); }
int c35(int x) { if (x) return 1; (x ? &die2 : &die3)(); }

int element(int x) { if (x) return 1; parr[x](); }
int member(int x) { if (x) return 1; ps->cb(); }
int cast_plain(int x) { if (x) return 1; ((P)np)(); }
int deref(int x) { if (x) return 1; (*fp)(); }
int decl_only(int x) { if (x) return 1; (x ? die : die)(); }
int mixed(int x) { if (x) return 1; (x ? die2 : plain)(); }
int mixed_reverse(int x) { if (x) return 1; (x ? plain : die2)(); }
int comma(int x) { if (x) return 1; ((void)x, die)(); }
int result(int x) { if (x) return 1; get()(); }
NP get_nr(void);
int nr_result(int x) { if (x) return 1; get_nr()(); }
int branch(int x) { if (x) ps->ncb(); else ps->cb(); }
[[noreturn]] void standard(void);
[[gnu::noreturn]] void extension(void);
int c23_direct(void) { standard(); }
int c23_pointer(int x) { (x ? standard : standard)(); }
int gnu_pointer(int x) { (x ? extension : extension)(); }
