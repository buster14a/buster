void c(int *);
int f(int);
int g(int x)
{
for (int y __attribute__((cleanup(c))) = 0; y < x; y++)
{
__attribute__((musttail)) return f(x);
}
return 0;
}
