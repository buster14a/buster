enum E { A };
int f(enum E e)
{
return __builtin_align_up(e, 4);
}
