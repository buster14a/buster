static int s;
long f(long x)
{
int l = 0;
return __builtin_align_up(x, (s = 3)) + __builtin_align_down(x, (s = 3) + 0) +
__builtin_is_aligned(x, (l = 3)) + __builtin_align_up(x, (s += 3)) + __builtin_align_up(x, s++) + __builtin_align_up(x, ++l);
}
