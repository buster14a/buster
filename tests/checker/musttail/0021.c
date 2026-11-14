int f(int);
int g(int x)
{
__attribute__((musttail)) f(x);
return 0;
}
