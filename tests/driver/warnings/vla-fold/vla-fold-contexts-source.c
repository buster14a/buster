#define F (int)(1.0 + 2)
int a1[F];
int a2[2][F];
int *a3[F];
int (*a4)[F];
typedef int T1[F];
struct S { int m[F]; int bf : F; };
enum E { X = F };
_Static_assert(F, "");
extern int a5[F];
int a6[F], a7[F];
void p(int x[F]);
int f(int n)
{
    int b1[F];
    static int b2[F];
    extern int b3[F];
    typedef int T2[F];
    switch (n) { case F: return 1; }
    int c = sizeof(int[F]);
    _Alignas(F) int d;
    return b1[0] + b2[0] + c + d;
}
int q[] = { [F] = 1 };
_Alignas(F * 4) int e;
