#include <stdio.h>
#include <stdlib.h>

static long (*next_value)(void) = lrand48;
static unsigned short seed[3] = { 0x330e, 123, 456 };
static unsigned short *initial = seed;

int main(void)
{
    seed48(initial);
    unsigned checksum = 0;
    for (unsigned i = 0; i < 64; ++i)
    {
        checksum = checksum * 31u + (unsigned)next_value();
        checksum ^= (unsigned)mrand48();
    }
    printf("rand48 checksum %u\n", checksum);
    return 0;
}
