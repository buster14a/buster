typedef long double v2ld __attribute__((vector_size(32)));
typedef __int128 v2i __attribute__((vector_size(32)));
v2i f(v2ld v)
{
return __builtin_isfpclass(v, 0x108);
}
