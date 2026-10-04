int f(int a, int b, int c)
{
    if (a && (b || c))
    {
        return 1;
    }
    return (a && b) ? 2 : 3;
}
