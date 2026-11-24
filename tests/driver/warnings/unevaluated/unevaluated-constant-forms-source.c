int x; int arr2[4];
typedef int T;
struct S { int a; int b[2]; };
int f(void);
#define INC(v) v++
int a1[sizeof(int)];
int a2[sizeof(T)];
int a3[sizeof((struct S){ .a = 1, .b[1] = 2 })];
int a4[sizeof x++];
int a5[__alignof__(x = 3)];
int a6[sizeof(sizeof(x++))];
int a7[sizeof((int)(x = 1))];
int a8[sizeof(x, x++)];
int a9[sizeof(f())];
int a10[sizeof(INC(x))];
int a11[sizeof(arr2[x++])];
struct B { int w : sizeof(x++); };
enum { E1 = _Generic(x++, int: 1), E2 = sizeof(x += 2) };
int a12[sizeof(x ? x-- : 0)];
int a13[sizeof(T[2])];
void g(void) { switch (x) { case sizeof(x++): break; } _Alignas(sizeof(x++)) int z; (void)z; }
