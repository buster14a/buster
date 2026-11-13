int f();
int g(int x)
{
__attribute__((musttail)) return f(x);
}
