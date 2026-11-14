int f(int x, int y)
{
const volatile int v = 3;
const int w = y;
return __builtin_align_up(x, v) + __builtin_align_down(x, w);
}
