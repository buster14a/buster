int f(int x)
{
    int r = 0;
    return r;
    r += x << 35;
    x << 36;
    r += ({ x << 37; 1; });
    ({ x << 38; 1; });
}
