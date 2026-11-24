int g0;
int *gp = 5;
char *gc = &g0;
struct S { int *m; long n; } s = { 7, &g0 };
long arr[2] = { &g0, 0 };
void f(int *q)
{
    struct S t = { q, q };
    char *u[] = { q, 0 };
    int *w = (int *[]){ 3 }[0];
    (void)t; (void)u; (void)w;
}
