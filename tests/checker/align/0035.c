int g(void);
int f(int x)
{
return __builtin_align_up(x, (g(), 3));
}
