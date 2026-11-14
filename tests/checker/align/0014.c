enum E { A = 4 };
int f(int x)
{
return __builtin_align_up(x, A);
}
