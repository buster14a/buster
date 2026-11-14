enum E { A };
int f(enum E);
int g(int x)
{
__attribute__((musttail)) return f(x);
}
