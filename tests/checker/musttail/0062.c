int f(int, int);
int g(int x, int y, void *p)
{
__attribute__((musttail)) return __builtin_call_with_static_chain(f(x, y), p);
}
