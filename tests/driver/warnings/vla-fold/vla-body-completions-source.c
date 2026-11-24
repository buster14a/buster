void g(int);
int f(int i)
{
    int a[3];
    char c;
    a[3] = 0;
    a[4] = (int)sizeof(struct { int m[(1, 2)]; });
    int x = a[5] + (int)sizeof(enum { E = (3, 4) }) + a[6];
    1 + (int)sizeof(struct { int m[(5, 6)]; });
    i++ + i++ + (int)sizeof(struct { int m[(7, 8)]; });
    c = 300 + (int)sizeof(struct { int m[(9, 1)]; });
    g(a[7] + (int)sizeof(struct { int m[(2, 3)]; }));
    return x + c;
}
