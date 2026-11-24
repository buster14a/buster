#define SET(a, b) ((a) = (b))
#define NARROW(x) ((float)0 + (x))
#define LOSE(x) float lost = x
float pragmas(double d, long double l, float f)
{
    f = d;
#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wimplicit-float-conversion"
    f = d;
    SET(f, l);
    f = NARROW(d);
    LOSE(d);
    f += lost;
    f += d;
#pragma clang diagnostic error "-Wimplicit-float-conversion"
    d = l;
#pragma clang diagnostic ignored "-Wimplicit-float-conversion"
    d = l;
#pragma clang diagnostic pop
    d = l;
#pragma clang diagnostic ignored "-Wliteral-range"
    f = f == 0.1;
#pragma clang diagnostic warning "-Wliteral-range"
    f = f == 0.1;
    return f + d;
}
