int g(int);
int f(int x)
{
    int u;
    int r = 0 && ({ u + 1; });
    r += 0 ? ({ g(u); }) : 1;
    int v;
    r += ({ v; });
    return r;
    ({ int w; w; });
}
