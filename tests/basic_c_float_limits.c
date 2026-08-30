// The <float.h> predefine vocabulary: the hosted header defines every
// FLT_/DBL_/LDBL_ constant in terms of __FLT_MAX__-style compiler
// predefines, so a compiler that lacks one turns `double m = DBL_MAX;` into
// an undeclared identifier -- which is where CPython's
// Objects/floatobject.c stopped compiling.  The values are pinned as static
// initializers (the failing shape) and compared against their literal
// spellings, so a predefine that folds to the wrong bits fails at run time.
#include <float.h>

static double max_double = DBL_MAX;
static double min_double = DBL_MIN;
static double epsilon_double = DBL_EPSILON;
static float max_float = FLT_MAX;
static long double max_long_double = LDBL_MAX;

int main(void)
{
    if (max_double != 1.7976931348623157e+308)
    {
        return 1;
    }
    if (min_double != 2.2250738585072014e-308)
    {
        return 2;
    }
    if (epsilon_double != 2.220446049250313080847263336181640625e-16)
    {
        return 3;
    }
    if (max_float != 3.40282347e+38F)
    {
        return 4;
    }
    // On targets whose long double is double -- Windows, AArch64 macOS --
    // the two maxima are equal, so only a smaller value is wrong.
    if (max_long_double < (long double)max_double)
    {
        return 5;
    }
    if (DBL_MANT_DIG != 53 || FLT_RADIX != 2 || DBL_MAX_EXP != 1024 || DBL_MIN_EXP != -1021)
    {
        return 6;
    }
    if (FLT_DIG != 6 || DBL_DIG != 15 || FLT_MAX_10_EXP != 38 || DBL_MIN_10_EXP != -307)
    {
        return 7;
    }
    return 0;
}
