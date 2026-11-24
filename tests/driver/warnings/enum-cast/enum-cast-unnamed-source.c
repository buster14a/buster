enum A { A1 };
enum B { B1 };
struct S { enum A a; enum B b; };
enum B gb(void);
void sa(struct S *p, int i) { p->a = p->b; p->a = gb(); p->a = (enum B)i; }
struct S mk(struct S *p) { struct S s = { gb() }; return s; }
void take(enum A);
void call(struct S *p) { take(p->b); }
