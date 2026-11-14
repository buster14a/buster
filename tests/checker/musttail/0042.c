__attribute__((ms_abi)) int f(int);
int g(int x)
{
__attribute__((musttail)) return f(x);
}
