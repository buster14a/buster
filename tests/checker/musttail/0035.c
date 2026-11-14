int f(char *);
int g(int *x)
{
__attribute__((musttail)) return f((char *)x);
}
