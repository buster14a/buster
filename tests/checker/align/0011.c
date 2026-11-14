void g(void);
int f(void)
{
return __builtin_is_aligned(g, 4);
}
