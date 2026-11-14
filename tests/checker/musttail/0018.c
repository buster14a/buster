struct S { long a, b, c; };
struct S f(struct S, long double, _Complex double, __int128);
struct S g(struct S s, long double d, _Complex double z, __int128 w)
{
__attribute__((musttail)) return f(s, d, z, w);
}
