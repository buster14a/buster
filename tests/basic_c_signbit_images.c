// Pointer boundaries keep signaling inputs out of host floating conversions.
int signbit_image_float(float const* value)
{
    return __builtin_signbitf(*value);
}

int signbit_image_double(double const* value)
{
    return __builtin_signbit(*value);
}

// The driver separately enables this operation to check the exact x86
// binary128 rejection. Float and double remain positive object controls.
#if __LDBL_MANT_DIG__ != 113 || defined(__aarch64__) || defined(BUSTER_SIGNBIT_BINARY128_REJECTION)
int signbit_image_long_double(long double const* value)
{
    return __builtin_signbitl(*value);
}

#endif
