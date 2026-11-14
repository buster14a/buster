int f(int x)
{
const int c = 3;
return __builtin_align_up(x, c + 0);
}
