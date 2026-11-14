void f(int);
void g(int x)
{
__attribute__((musttail)) return f(x);
}
