struct S { int a[4]; };
int f(int);
struct S h(void);
static int abs(int x) { return x; }
void v(void);
int g(void* p, char* q, int (*k)(int, ...))
{
struct S s = __builtin_call_with_static_chain(h(), q);
__builtin_call_with_static_chain(v(), p);
(void)sizeof(__builtin_call_with_static_chain(f(1), p));
return __builtin_call_with_static_chain(f(1), p) + __builtin_call_with_static_chain((*k)(1, 2.0), p++) + s.a[0] +
__builtin_call_with_static_chain(abs(2), p);
}
