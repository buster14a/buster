int f(int);
int g(int x)
{
__attribute__((musttail, musttail)) return f(x);
}
