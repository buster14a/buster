struct A { const int a[2]; };
struct B { int n; const int fam[]; };
struct C { const int bf : 3; };
struct D { struct A arr[2]; };
struct E { const struct { int q; } s; };
typedef const int CI;
struct F { CI t; };
void f(void) { struct A a; struct B b; struct C c; struct D d; struct E e; struct F g; const struct A ca; struct A *p; }
void k(int n) { const int vla[n]; _Atomic const int at; const int ci = 1, cj; }
