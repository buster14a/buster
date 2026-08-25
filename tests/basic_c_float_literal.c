#include <stdint.h>

int main(void)
{
    union
    {
        double value;
        uint64_t bits;
    } literal = { .value = 123e+127 };
    return literal.bits == UINT64_C(0x5abc64336586c67c) ? 0 : 1;
}
