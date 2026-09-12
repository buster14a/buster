// Pointer boundaries keep signaling inputs out of host floating conversions.
int signbit_image_float(float const* value)
{
    return __builtin_signbitf(*value);
}

int signbit_image_double(double const* value)
{
    return __builtin_signbit(*value);
}

int signbit_image_long_double(long double const* value)
{
    return __builtin_signbitl(*value);
}
