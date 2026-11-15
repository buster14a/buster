struct S { int x; };
struct S s;
int x;
int *q = (x == 3, s);
void f(struct S s, int x)
{
    int *p = (x == 3, s);
}
