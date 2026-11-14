int f(int);
int g(int x)
{
__attribute__((musttail)) return f(x);
}
[[noreturn]] int f(int);
