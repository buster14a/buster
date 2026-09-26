#include <limits.h>
#include <stdio.h>
extern int check(void);
int main(void)
{
    int result = 2;
    if (CHAR_BIT == 8 && SCHAR_MIN == -128 && SHRT_MAX == 32767 && INT_MAX == 2147483647 && sizeof(long long) == 8)
    {
        result = check();
        printf("observed=%d expected=0\n", result);
    }
    else
    {
        puts("unsupported ABI assumptions");
    }
    return result;
}
