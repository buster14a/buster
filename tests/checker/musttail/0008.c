int f(int);
int g(int x)
{
__attribute__((musttail)) return __extension__ f(x);
}
