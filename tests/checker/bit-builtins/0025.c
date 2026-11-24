unsigned f(unsigned u, unsigned long *p)
{
return __builtin_addc(u, u, 0, p);
}
