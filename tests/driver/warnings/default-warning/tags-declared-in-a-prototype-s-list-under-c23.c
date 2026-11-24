struct S6;
void f1(struct S1 *p);
void f2(struct S2 { int a; } *p);
void f3(union U3 *p);
void f4(enum E4 { A4 } e);
void f5(enum { A5 } e, enum : long { B5 } f);
void f6(struct S6 *p);
void f7(struct S7 *p) { (void)p; }
int f8(struct S8 (*fp)(struct S9 *q));
void f9(struct S10 *p, int (*g)(struct S10 *));
void (*f10)(struct S11 *);
void f11(void) { void g(struct S12 *); }
void f12(struct { int a; } *p, struct A { struct B *b; } *a);
struct V { void (*fp)(struct U *); };
struct T { int b; };
void g1(struct T *p, struct T { int c; } *q);
int g4(int (*a)[sizeof(struct Q { int q; })]);
