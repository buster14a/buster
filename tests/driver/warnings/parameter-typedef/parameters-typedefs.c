typedef int I;
typedef I J;
typedef void (*F)(I);
void f(I x);
void g(void (*cb)(I), J (*h)(I, J));
void use(void)
{
    int *p;
    void (*q)(I) = 0;
    F r = 0;
    p = q;
    p = r;
    p = f;
    p = &f;
    p = g;
    int (*s)(I*, J) = 0;
    p = s;
    I a[2];
    p = &a;
}
