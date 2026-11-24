#include <stdio.h>
static volatile double ar = 1.5, ai = -2.25, br = 0.5, bi = 3.0;
static volatile float fr = 2.0f, fi = 0.5f;
static volatile long double lr = 1.25L, li = -0.75L;
int main(void)
{
    _Complex double a = ar + ai * 1.0i, b = br + bi * 1.0i;
    _Complex float f = fr + fi * 1.0fi, g = fi - fr * 1.0fi;
    _Complex long double l = lr + li * 1.0Li, m = li + lr * 1.0Li;
    _Complex double p = a * b, q = a / b, r = ar / b;
    _Complex float s = f * g, t = f / g;
    _Complex long double u = l * m, v = l / m;
    printf("%a %a %a %a %a %a %a %a %a %a %La %La %La %La\n", __real__ p, __imag__ p, __real__ q, __imag__ q, __real__ r,
           __imag__ r, (double)__real__ s, (double)__imag__ s, (double)__real__ t, (double)__imag__ t, __real__ u, __imag__ u,
           __real__ v, __imag__ v);
    return 0;
}
