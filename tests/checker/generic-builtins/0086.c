_Static_assert((__builtin_popcount)(7) == 3, "");
_Static_assert(((__builtin_clz))(1) == 31, "");
enum { E = (__builtin_ctz)(8) };
int f(unsigned x)
{
return (__builtin_popcount)(x) + ((__builtin_ffs))((int)x) + E;
}
