int g(int);
int f(void)
{
return __builtin_invoke(g);
}
