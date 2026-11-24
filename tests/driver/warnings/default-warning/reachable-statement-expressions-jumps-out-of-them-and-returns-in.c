int g(int);
int f(int x, int y)
{
    int r = ({ x << 40; 1; });
    r += y ? ({ x << 41; 2; }) : ({ x << 42; 3; });
    r += 1 && ({ x / 0; 1; });
    r += ({ if (x) goto out; 0; });
    r += ({ int k = ({ x << 43; 4; }); k; });
    for (;;) { r += ({ x << 44; 1; }); if (r) break; }
    while (1) { }
    r += ({ x << 45; 1; });
out:
    r += ({ x << 46; 1; });
    r += 0 && ({ int u; u + ({ x << 47; 1; }); });
    return r;
}
int h(int x)
{
    int r = ({ if (x) return 1; x << 48; 2; });
    r += ({ return 2; x << 49; 3; });
    return r;
}
