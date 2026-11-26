typedef _Bool __attribute__((aligned(4))) Wide;
int f(Wide w, _Bool b, int x, int y)
{
    int r = 0;
    if (w == 2) r += 1;
    if (3 < w) r += 1;
    if (b == 2) r += 1;
    if (x == 2) r += 1;
    if (x != 3 && y) r += 1;
    return r;
}
