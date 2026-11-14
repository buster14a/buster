int f(_Atomic int a)
{
return __builtin_align_up(a, 4);
}
