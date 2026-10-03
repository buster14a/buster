#include "basic_c_parameter_alignment.h"

int main(void)
{
    struct ParameterAligned32 a = {{11, 12, 13, 14}};
    struct ParameterAligned64 b = {{21, 22, 23, 24, 25, 26, 27, 28}};
    int failed = 0;
    for (int repeat = 0; repeat < 4; repeat += 1)
    {
        failed |= parameter_aligned32(a);
        failed |= parameter_aligned64(b);
        failed |= parameter_aligned_after_integers(1, 2, 3, 4, 5, 6, 7, b, 9);
        failed |= parameter_aligned_after_vectors(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, a, 10.0);
    }
    return failed;
}
