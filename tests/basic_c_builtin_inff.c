#include <math.h>

int main(void)
{
    float infinity = __builtin_inff();
    return !(isinf(infinity) && !isfinite(infinity));
}
