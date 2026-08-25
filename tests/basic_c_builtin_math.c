int main(void)
{
    double nan = __builtin_nan("");
    volatile double zero = 0.0;
    double positive_infinity = 1.0 / zero;
    double negative_infinity = -1.0 / zero;
    return !__builtin_isnan(nan) || __builtin_isinf_sign(1.0) != 0 || __builtin_isinf_sign(positive_infinity) != 1 ||
                   __builtin_isinf_sign(negative_infinity) != -1
               ? 1
               : 0;
}
