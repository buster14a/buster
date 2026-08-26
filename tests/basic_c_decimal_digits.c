#include <float.h>

_Static_assert(FLT_DECIMAL_DIG == 9, "binary32 decimal precision");
_Static_assert(DBL_DECIMAL_DIG == 17, "binary64 decimal precision");

int main(void)
{
    return FLT_DECIMAL_DIG != 9 || DBL_DECIMAL_DIG != 17;
}
