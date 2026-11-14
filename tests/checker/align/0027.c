unsigned _BitInt(200) f(unsigned _BitInt(200) x, long a)
{
return __builtin_align_up(x, 16) + __builtin_align_down(x, (unsigned __int128)1 << 127) + __builtin_align_up(x, a);
}
