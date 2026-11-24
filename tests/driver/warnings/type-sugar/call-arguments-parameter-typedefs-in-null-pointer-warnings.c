typedef struct N { int x; } N;
typedef N *NP;
typedef int *IP;
typedef void F(N *);
void a(const N *q);
void b(NP);
void h(int x, IP p, N *const r);
void e(N *q[]);
void f(int n, N *(*g)(void), NP const r);
void d(int, ...);
void (*fp)(N *);
F *gp;
struct S { void (*m)(N *); } s;
static int z = sizeof(a(1-1), 0);
int main(void)
{
    a(1-1); b(1-1); h(0, 1-1, 1-1); e(1-1); f(0, 1-1, 1-1); d(1, 1-1);
    (a)(1-1); fp(1-1); (*fp)(1-1); gp(1-1); s.m(1-1);
    void (*lp)(int, N *) = 0;
    lp(0, 1-1);
    return 0;
}
