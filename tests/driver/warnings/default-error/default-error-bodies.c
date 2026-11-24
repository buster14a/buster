void take(char *p);
int *f(long x, int *q)
{
    int *p = x; char *c = p; void (*fp)(int) = (void (*)(void))0;
    (void)c; (void)fp; p = 3; take(q); return x;
}
void g(void) { return 1; }
int h(void) { return; }
