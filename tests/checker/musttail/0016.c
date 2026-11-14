int f(const int, const char *);
int g(int x, char *p)
{
__attribute__((musttail)) return f(x, p);
}
