void c(int *);
int f(int);
int g(int x)
{
int y __attribute__((cleanup(c))) = x;
__attribute__((musttail)) return f(x);
}
