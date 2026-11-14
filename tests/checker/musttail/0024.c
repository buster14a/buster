int f(int);
int g(int x)
{
__attribute__((musttail)) int y = f(x);
return y;
}
