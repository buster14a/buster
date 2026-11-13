int g(int n)
{
    int s = 0;
#pragma GCC unroll
    for (int i = 0; i < n; i += 1)
    {
        s += i;
    }
    return s;
}
