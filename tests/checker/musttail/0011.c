struct S { int (*f)(int); };
int g(int x, struct S *s);
int g(int x, struct S *s)
{
__attribute__((musttail)) return s->f(x);
}
