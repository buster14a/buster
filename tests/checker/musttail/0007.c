int f(int);
int g(int x)
{
__attribute__((musttail)) return _Generic(x, int: f(x));
}
