int f(const int *(*p)[3]);
int g(int *(*p)[3])
{
__attribute__((musttail)) return f((const int *(*)[3])p);
}
