int i;
unsigned u;
unsigned char c;
int f(void)
{
    int a[3];
    a[3] = 0;
    int x = (i < u) + (int)sizeof(struct { int m[(1, 2)]; });
    x += (c < 300) + (int)sizeof(struct { int m[(3, 4)]; });
    return x;
}
