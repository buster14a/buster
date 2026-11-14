long f(int);
long g(int x)
{
__attribute__((musttail)) return (long)f(x);
}
