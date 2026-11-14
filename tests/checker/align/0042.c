static const double d = 3.0;
int f(int x)
{
return __builtin_align_up(x, (int)d);
}
