struct S { int x; } s;
int *a = (1, s), *b = (2, s);
void f(int c)
{
    int *p = (c ? (1, s) : (2, s));
    int *q = (0, s);
}
void g(void)
{
    if (0) { int *p = (1, s); }
}
