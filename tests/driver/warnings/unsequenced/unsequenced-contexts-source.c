#define INC(v) v++
int g(int);
void f(int c, int x, int y, signed char sc, _Bool b) {
    if (0) { x = x++; }
    sc = c ? (sc = 300) : 400;
    b = c ? (b = c ? 5 : 6) : 7;
    sc = c ? (c ? (sc = 300) : 301) : (sc = 302, 303);
    x = INC(x);
    y = ({ x++ + x++; });
    y = ({ x; }) + x++;
    y = __builtin_constant_p(x++) + x++;
    y = _Generic(x++, int: x) + x++;
    y = sizeof(x++) + x++;
    x = __builtin_expect(x++, 1) + x;
    for (int i = 0; i < 10; i = i++) {}
    while ((x = x++)) {}
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunsequenced"
    y = y++;
    #pragma clang diagnostic pop
    y = y++ + (char)300;
}
void h(int y)
{
    y = y + 1;
    return;
    y = y++;
}
