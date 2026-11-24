int f(unsigned char c, signed char s, int i, short h)
{
    int r = 0;
    r += c == 300; r += -1 < c; r += s > 2; r += i < 3; r += h == 200;
    r += c < i; r += 1 < 129; r += c != 'x'; r += s <= -70000; r += (c == 1) == 9;
    r += c == 300; r += -1 < c; r += s > 2; r += i < 3; r += h == 200;
    r += c < i; r += 1 < 129; r += c != 'x'; r += s <= -70000; r += (c == 1) == 9;
    r += c == 300; r += -1 < c; r += s > 2; r += i < 3; r += h == 200;
    r += c < i; r += 1 < 129; r += c != 'x'; r += s <= -70000; r += (c == 1) == 9;
    r += c == 300; r += -1 < c; r += s > 2; r += i < 3; r += h == 200;
    r += c < i; r += 1 < 129; r += c != 'x'; r += s <= -70000; r += (c == 1) == 9;
    return r;
}
