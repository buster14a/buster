int f(int);
long g(int x)
{
__attribute__((musttail)) return f(x);
}
