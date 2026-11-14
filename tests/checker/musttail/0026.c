int f(int);
int g(int x)
{
l: __attribute__((musttail));
return f(x);
}
