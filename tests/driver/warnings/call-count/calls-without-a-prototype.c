void knr();
void (*fp)();
struct S { void (*g)(); } s;
void kd(a, b) int a, b; {}
void f(void)
{
    knr(1);
    fp(1);
    (knr)(1);
    (&knr)(1);
    s.g(1);
    (*fp)(2);
    kd(1);
    kd(1, 2, 3);
    (kd)(1);
    knr();
}
