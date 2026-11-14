static const int s = 6;
int f(int x)
{
return __builtin_is_aligned(x, s);
}
