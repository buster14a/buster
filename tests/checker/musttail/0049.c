int f(const int *(*p)[]);
int g(int *(*p)[])
{
__attribute__((musttail)) return f((const int *(*)[])p);
}
