int f(int);
int g(int x)
{
[[clang::musttail]] if (x) return f(x);
return 0;
}
