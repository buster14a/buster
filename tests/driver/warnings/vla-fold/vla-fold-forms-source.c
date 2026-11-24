int a1[(int)2.5];
int a2[(int)(1.0 + 2)];
char a3[(unsigned)3.0f];
int a4[(long)1.5 + 1];
typedef int T1[(int)2.0];
struct S { int m[(int)3.0]; int bf : (int)2.0; };
int s1 = sizeof(int[(int)2.0]);
enum E { X = (int)2.5 };
_Static_assert((int)1.0, "");
int f(int n)
{
    int b1[(int)2.5];
    static int b2[(int)2.5];
    int b3[n];
    int b4[sizeof(b3) > 0 ? 1 : 2];
    switch (n) { case (int)1.0: return 1; }
    int c = sizeof(int[(int)2.0]);
    return b1[0] + b2[0] + b4[0] + c;
}
int g1[__builtin_constant_p(1) ? 2 : 3];
const int k = 3;
int g2[k];
int g3[(int)"x"[0] - 'x' + 1];
