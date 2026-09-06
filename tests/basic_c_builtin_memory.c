// No system headers: the driver test compiles this without an -isysroot, and
// on macOS there are none to find without the SDK.  memcpy and memcmp are
// declared here so the builtins take the translation unit's own prototype,
// while memset and memmove are left undeclared so the other half of the
// lowering -- importing the standard signature -- is covered too.
extern void *memcpy(void *destination, const void *source, unsigned long count);
extern int memcmp(const void *left, const void *right, unsigned long count);

// LZ4 spells its block copies __builtin_memcpy/__builtin_memmove once
// __clang__ is defined, so the builtins have to lower to the library calls
// they name, with the prototype's argument conversions and the destination
// returned.
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
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
#if defined(__APPLE__) || defined(__linux__)
    // Hosted fortified entry points retain the fourth argument. Use runtime
    // values to exercise the ABI rather than a folded constant-size copy.
    volatile int bounded_count = 4;
    volatile int bound = 16;
    if (__builtin___memset_chk(destination, 0x27, bounded_count, bound) != destination) return 7;
    if (destination[0] != 0x27 || destination[3] != 0x27) return 8;
    if (__builtin___memcpy_chk(destination, source, bounded_count, bound) != destination) return 9;
    if (destination[0] != 0x5a || destination[3] != 0x5a) return 10;
    destination[0] = 3;
    if (__builtin___memmove_chk(destination + 1, destination, bounded_count, bound - 1) != destination + 1) return 11;
    if (destination[1] != 3) return 12;
    if (__builtin___memcpy_chk(destination, source, bounded_count, (__SIZE_TYPE__)-1) != destination) return 13;
    if (__builtin___memset_chk(destination, 0, 0, 0) != destination) return 14;
    if (argc > 1)
    {
        // No physical overflow is possible: the array is sixteen bytes. The
        // explicit bound is intentionally smaller, so the checked runtime
        // must fail before writing. The supplemental gate expects failure.
        bound = 1;
        if (argv[1][0] == 'c') __builtin___memcpy_chk(destination, source, bounded_count, bound);
        else if (argv[1][0] == 'm') __builtin___memmove_chk(destination, source, bounded_count, bound);
        else __builtin___memset_chk(destination, 0, bounded_count, bound);
        return *(volatile unsigned char*)destination;
    }
#endif
    return 0;
}
