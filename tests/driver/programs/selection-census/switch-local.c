int g(char* p);
int f(int x)
{
    switch (0)
    {
    case 1:
    {
        char unreached[256];
        return g(unreached);
    }
    default:
        break;
    }
    return x;
}
