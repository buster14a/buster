_Atomic struct A { int m; } s;
int y;
int f(void)
{
    int a[3];
    a[3] = 0;
    int x = 1 / 0 + (int)sizeof(struct { int m[(1, 2)]; });
    x += (1 << 40) + (int)sizeof(struct { int m[(3, 4)]; });
    x += s.m + (int)sizeof(struct { int m[(5, 6)]; });
    x += (y, 1) + (int)sizeof(struct { int m[(7, 8)]; });
    return x;
}
