static const long big = 1L << 40;
int f(int x)
{
return __builtin_align_down(x, big);
}
