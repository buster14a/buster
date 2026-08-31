// Plain char's <limits.h> range follows the target -- signed on every SysV
// target -- and the builtin header spelled it unsigned unconditionally.
// _testbuffer's error paths return CHAR_MAX through a plain char sentinel
// and compare it back against the macro: with CHAR_MAX at 255 the sentinel
// never matched and every error fell through into a SystemError.
#include <limits.h>
#include <stdio.h>

static char sentinel(void)
{
    return CHAR_MAX;
}

int main(void)
{
    if (CHAR_MAX != 127 || CHAR_MIN != -128 || SCHAR_MAX != 127 || UCHAR_MAX != 255)
    {
        return 1;
    }
    if (sentinel() != CHAR_MAX)
    {
        return 2;
    }
#if CHAR_MAX != 127
    return 3;
#endif
    printf("char limits ok\n");
    return 0;
}
