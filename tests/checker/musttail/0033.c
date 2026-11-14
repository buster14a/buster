int f(int, int);
int g(int x)
{
__attribute__((musttail)) return f(x, x);
}
