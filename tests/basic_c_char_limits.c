// Plain char's <limits.h> range follows the target. Keep the sentinel
// regression while checking the signed System V and unsigned AAPCS64 models.
#include <limits.h>

static char sentinel(void)
{
    return CHAR_MAX;
}

int main(void)
{
#if CHAR_MIN == 0
    if (CHAR_MAX != 255 || CHAR_MIN != 0 || SCHAR_MAX != 127 || UCHAR_MAX != 255)
    {
        return 1;
    }
#if CHAR_MAX != 255 || CHAR_MIN != 0
    return 3;
#endif
#else
    if (CHAR_MAX != 127 || CHAR_MIN != -128 || SCHAR_MAX != 127 || UCHAR_MAX != 255)
    {
        return 1;
    }
#if CHAR_MAX != 127 || CHAR_MIN != -128
    return 3;
#endif
#endif
    if (sentinel() != CHAR_MAX)
    {
        return 2;
    }
    return 0;
}
