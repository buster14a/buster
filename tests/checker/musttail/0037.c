int (*p)(long);
int g(int x)
{
__attribute__((musttail)) return p(x);
}
