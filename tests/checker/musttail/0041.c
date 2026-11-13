int f(void);
int g()
{
__attribute__((musttail)) return f();
}
