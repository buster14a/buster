unsigned f(unsigned u, const unsigned *p)
{
return __builtin_subc(u, u, 1, p);
}
