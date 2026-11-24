typedef unsigned U;
typedef int I;
enum E { A = 9, B = -9 };
struct S { int b : 3; unsigned u : 3; long long l : 40; enum E e : 3; unsigned char c : 4; I t : 2; };
void f(struct S *p) {
    p->b = (I)9;
    p->u = (U)9;
    p->l = 0x10000000000LL;
    p->l = -0x8000000001LL;
    p->e = A;
    p->e = B;
    p->c = 300;
    p->t = -3;
    p->b = 'z';
    p->b = 1u << 31;
}
