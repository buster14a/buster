static int s;
long f(long x)
{
return __builtin_align_up(x, (s, 3));
}
