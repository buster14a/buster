int f(int);
int g(int p)
{
return __builtin_call_with_static_chain(f(1), p);
}
