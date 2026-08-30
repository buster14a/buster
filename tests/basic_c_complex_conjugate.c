// Two complex gaps that the differential harness found together (issues #822
// and #823): GNU spells complex conjugation `~`, which was refused as an
// operator with no complex form, and `__builtin_complex` -- the builtin behind
// C11's CMPLX macros in musl's <math.h> -- was not bound at all.
//
// `~` negates the imaginary half and leaves the real one; unary `-` negates
// both, and is the control that says the two did not get merged.
// `__builtin_complex` is not `re + im * I`: it is exact for a signed zero or an
// infinite imaginary part, which is why the macros are specified through a
// builtin, so the parts are read back through a union rather than compared as
// arithmetic.

union DoubleParts
{
    double _Complex value;
    double parts[2];
};

union FloatParts
{
    float _Complex value;
    float parts[2];
};

int main(void)
{
    union DoubleParts conjugated;
    union DoubleParts negated;
    union DoubleParts built;
    union FloatParts built_float;
    union DoubleParts built_negative_zero;
    double _Complex source = 3.0 + 4.0i;
    int result = 0;
    conjugated.value = ~source;
    negated.value = -source;
    built.value = __builtin_complex(3.0, 4.0);
    built_float.value = __builtin_complex(3.0f, 4.0f);
    built_negative_zero.value = __builtin_complex(1.0, -0.0);
    if (conjugated.parts[0] != 3.0 || conjugated.parts[1] != -4.0)
    {
        result = 1;
    }
    else if (negated.parts[0] != -3.0 || negated.parts[1] != -4.0)
    {
        result = 2;
    }
    else if (built.parts[0] != 3.0 || built.parts[1] != 4.0)
    {
        result = 3;
    }
    else if (built_float.parts[0] != 3.0f || built_float.parts[1] != 4.0f)
    {
        result = 4;
    }
    else if (~~source != source)
    {
        result = 5;
    }
    else if (__real__ (~source) != 3.0 || __imag__ (~source) != -4.0)
    {
        result = 6;
    }
    else if (built_negative_zero.parts[1] != 0.0 || 1.0 / built_negative_zero.parts[1] > 0.0)
    {
        // A negative zero imaginary part survives the builtin: it compares
        // equal to zero and its reciprocal is negative infinity, which is the
        // whole reason CMPLX cannot be written as `re + im * I`.
        result = 7;
    }
    return result;
}
