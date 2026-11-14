int f(int);
int g(int x)
{
[[clang::musttail]] return f(x);
}
