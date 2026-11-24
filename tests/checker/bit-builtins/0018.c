enum S { SA = -1 };
int f(unsigned u, enum S e)
{
return __builtin_clzg(u, e);
}
