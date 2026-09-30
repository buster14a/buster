/* Compile this separately with the reference host C compiler, never Buster.
 * Observe object bytes, not floating comparisons, formatting, or arithmetic. */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern const unsigned oracle_count;
extern const _Float16 oracle_values[];
_Static_assert(CHAR_BIT == 8 && sizeof(_Float16) == sizeof(uint16_t), "requires binary16 ABI");
int main(void)
{
    int result = 2;
    if (oracle_count > 0 && oracle_count <= 128)
    {
        result = 0;
        for (unsigned i = 0; i < oracle_count; ++i)
        {
            uint16_t bits;
            memcpy(&bits, &oracle_values[i], sizeof(bits));
            if (printf("%u %04x\n", i, (unsigned)bits) < 0) { result = 2; }
        }
    }
    return result;
}
