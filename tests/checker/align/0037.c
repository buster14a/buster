int f(int x)
{
const int a = 3;
return __builtin_align_up(x, a);
}
