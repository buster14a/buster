int f(int);
int g(int x)
{
__attribute__((__musttail__)) return f(x);
}
