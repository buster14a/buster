#pragma clang fp eval_method(double)
float f(float a, float b, __fp16 *h, double d)
{
    a += b;
    a *= 2.0f;
    *h += a;
    d += a;
    return a + d;
}
