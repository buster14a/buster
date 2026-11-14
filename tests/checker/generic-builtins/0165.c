int f(int);
int g(void)
{
return __builtin_call_with_static_chain(f(1));
}
