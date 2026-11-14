int f(int x)
{
register int r = x;
return *__builtin_addressof(r);
}
