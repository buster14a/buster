void take(double *);
void f(int n)
{
    int a[n];
    int (*p)[n] = &a;
    double *d = p;
    take(&a);
    float x = a;
    int b[n + 1][2];
    struct T { int z; } t = b;
    a = 0;
}
