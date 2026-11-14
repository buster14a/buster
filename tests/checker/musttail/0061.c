int f(int);
int g(int x, int (*p)(int))
{
__attribute__((musttail)) return __builtin_call_with_static_chain(f(x), f);
}
