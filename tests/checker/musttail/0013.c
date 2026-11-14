int f(int);
int g(int x)
{
l:
[[clang::musttail]] return f(x);
}
