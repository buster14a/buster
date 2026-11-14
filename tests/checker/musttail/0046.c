int f(int x) { return x; }
[[noreturn]] int f(int);
int g(int x)
{
__attribute__((musttail)) return f(x);
}
