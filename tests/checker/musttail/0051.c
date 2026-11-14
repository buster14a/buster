int f(const long *(*p)[3]);
int g(int *(*p)[3])
{
__attribute__((musttail)) return f((const long *(*)[3])p);
}
