int f(int);
int g(int x)
{
__attribute__((musttail)) return __builtin_choose_expr(1, f(x), 0);
}
