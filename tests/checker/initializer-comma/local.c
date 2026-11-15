struct S { int x; };
void f(struct S s)
{
    int *r = (0, s);
}
