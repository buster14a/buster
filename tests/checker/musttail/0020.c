__attribute__((noreturn)) int f(int);
_Noreturn int h(int);
int g(int x)
{
if (x)
__attribute__((musttail)) return f(x);
__attribute__((musttail)) return h(x);
}
