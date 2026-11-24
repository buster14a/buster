typedef int T;
T f1(T x) { T y = x; return y; }
int f2(void) { typedef char T; T c = 1; return c; }
T g1;
int f3(void) { int T = 3; return T; }
typedef T U, *V;
U f4(V v) { return *v; }
struct S { U m; } s = { 1 };
int main(void) { T t = 2; return f1(t) + f2() + f3() + f4(&t) + s.m + g1; }
