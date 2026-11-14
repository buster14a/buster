int f(int);
int g(int x)
{
__attribute__((musttail)) return x ? f(x) : f(0);
}
