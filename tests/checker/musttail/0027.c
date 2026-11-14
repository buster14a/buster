int f(int);
int g(int x)
{
__attribute__((musttail(1))) return f(x);
}
