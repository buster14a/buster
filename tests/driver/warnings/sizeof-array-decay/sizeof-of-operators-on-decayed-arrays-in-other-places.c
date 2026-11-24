int a[10];
typedef int A[3];
A t;
unsigned long z = sizeof(a + 1);
#define S(x) sizeof(x + 1)
unsigned long f(int n, int p[3]) { int v[n]; const int c[2] = {0}; return S(a) + sizeof(t + 1) + sizeof(v + 1) + sizeof(c + 1) + __alignof__(a + 1) + sizeof a + 1 + sizeof(p + 1) + sizeof(&a[0] + 1); }
