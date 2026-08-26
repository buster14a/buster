int main(void)
{
    float infinity = __builtin_inff();
    return !(__builtin_isinf(infinity) && !__builtin_isfinite(infinity));
}
