int f(int* p, int a, int b, int c)
{
    int x, y;
    x = y = a + b;
    *p++ = x;
    p[1] += c, p[2] <<= 1;
    x = (a > b) ? (y = a) : b;
    return x + y + (p[0] = c);
}
