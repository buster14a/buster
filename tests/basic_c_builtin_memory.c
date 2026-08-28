#include <string.h>

// LZ4 spells its block copies __builtin_memcpy/__builtin_memmove once
// __clang__ is defined, so the builtins have to lower to the library calls
// they name, with the prototype's argument conversions and the destination
// returned.
int main(void)
{
    char source[16];
    char destination[16];
    __builtin_memset(source, 0x5a, sizeof(source));
    if (__builtin_memcpy(destination, source, sizeof(destination)) != (void *)destination) return 1;
    if (__builtin_memcmp(source, destination, sizeof(source)) != 0) return 2;
    destination[3] = 0;
    if (__builtin_memcmp(source, destination, sizeof(source)) <= 0) return 3;
    // Overlapping forward move: byte 4 has to carry byte 3's cleared value.
    __builtin_memmove(destination + 1, destination, 8);
    if (destination[4] != 0) return 4;
    if (memcmp(source, destination, 1) != 0) return 5;
    // A count that is not already the size type still widens.
    int count = 4;
    __builtin_memset(destination, 7, count);
    if (destination[0] != 7 || destination[3] != 7 || destination[4] == 7) return 6;
    return 0;
}
