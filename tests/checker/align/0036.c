int f(int x, int a)
{
return __builtin_align_down(x, a) + __builtin_is_aligned(x, a);
}
