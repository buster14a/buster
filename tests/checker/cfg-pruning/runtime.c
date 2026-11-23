// The other runtime warnings clang defers to a body's end follow the same pruning (#2990): none inside `if (x * 0)`, and
// each inside a cast's condition, which no heuristic reads.
int f(int x)
{
    int w = 0;
    if (x * 0)
    {
        w += 1 / 0;
        w += x << 40;
        w = *(int*)0;
    }
    if ((long)(x * 0))
    {
        w += 1 / 0;
    }
    return w;
}
