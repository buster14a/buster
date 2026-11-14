enum E { A = 4 };
int f(int x, enum E e)
{
return __builtin_align_up(x, e);
}
