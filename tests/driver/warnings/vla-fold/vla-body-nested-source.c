void g(int, int);
void f(void)
{
    int a[3];
    a[3] = 0;
    a[4] = ({ enum { E = (1, 2) }; 0; });
    g(a[5], ({ static int s[(3, 4)]; s[0]; }));
    a[6] = ({ a[7]; enum { F = (5, 6) }; 0; });
    int x = *(int *)0 + (int)sizeof(struct { int m[(7, 8)]; });
    x = x / (sizeof(struct { int m[(9, 2)]; }) - 8);
    x = 1 << (sizeof(struct { int m[(1, 2)]; }) * 8);
    for (a[8] = 0; (int)sizeof(struct { int m[(3, 1)]; }) > a[9]; a[10]++)
    {
        enum { G = (4, 5) };
    }
    (void)x;
}
