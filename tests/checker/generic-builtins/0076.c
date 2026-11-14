struct S;struct T{int x;};union U{int y;};enum E{EA};
#define D(t, v) _Static_assert(__is_destructible(t) == v && __is_nothrow_destructible(t) == v, #t);
#define C(t, v) _Static_assert(__is_bitwise_cloneable(t) == v, #t);
D(int, 1) D(struct T, 1) D(union U, 1) D(enum E, 1) D(int[3], 1) D(int*, 1) D(struct S*, 1) D(_Atomic long double, 1) D(const volatile int, 1)
D(void, 0) D(const void, 0) D(int[], 0) D(void(void), 0) D(int(*)(void), 1) D(double[2][3], 1)
C(int, 1) C(struct T, 1) C(int[], 1) C(void(void), 1) C(void, 0) C(struct T[][2], 1) C(_Atomic int, 1) C(__int128, 1)
_Static_assert(_Generic(__is_destructible(int), int: 1, default: 0) && _Generic(__is_bitwise_cloneable(int), int: 1, default: 0), "");
enum { K = __is_destructible(int) + __is_bitwise_cloneable(void) };
int a[__is_nothrow_destructible(int) + 1];
static int s = __is_bitwise_cloneable(struct T);
int f(int n)
{
switch (n) { case __is_destructible(int): return __is_destructible(int[n]) + __is_bitwise_cloneable(int[n]); }
return K;
}
