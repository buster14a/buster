int *p;
int x;
struct S { int m; } s;
void f(void)
{
    (int)p = 0;
    (char)*p += 1;
    ((int)p)++;
    --(long)x;
    (int)x = 1;
    ((int)(x)) = 2;
    (int)5 = 3;
    (int)s.m = 4;
    x + 1 = 5;
    (short)x <<= 2;
}
