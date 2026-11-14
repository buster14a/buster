int f(long);
int g(int x)
{
__attribute__((musttail)) return f(x);
}
