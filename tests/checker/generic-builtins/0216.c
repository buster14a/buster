int g(int);
int v(int, ...);
int (*p)(int) = g;
struct S { int (*fn)(int); };
long f(struct S s)
{
_Static_assert(_Generic(__builtin_invoke(g, 1), int: 1, default: 0), "");
return __builtin_invoke(g, 1.5) + __builtin_invoke(p, 2) + __builtin_invoke(*p, 3) + __builtin_invoke(s.fn, 4) + __builtin_invoke(&g, 5) + __builtin_invoke(v, 1, 2.0f, "x");
}
