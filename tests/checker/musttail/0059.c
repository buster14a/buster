int f(int);
int g(int x)
{
if (x)
__attribute__((musttail)) return f(x);
__attribute__((musttail)) return x;
}
