int f(const int *(*p)[4]);
int g(int *(*p)[3])
{
__attribute__((musttail)) return f((const int *(*)[4])p);
}
