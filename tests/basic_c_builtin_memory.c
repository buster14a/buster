// No system headers: the driver test compiles this without an -isysroot, and
// on macOS there are none to find without the SDK.  memcpy and memcmp are
// declared here so the builtins take the translation unit's own prototype,
// while memset and memmove are left undeclared so the other half of the
// lowering -- importing the standard signature -- is covered too.
extern void *memcpy(void *destination, const void *source, unsigned long count);
extern int memcmp(const void *left, const void *right, unsigned long count);

// Object-size queries must never run a side-effecting pointer operand.
// The same expression occurs twice in the SDK's fortified-memory macros.
static char object_size_storage[16];
static int object_size_calls;
static char* object_size_destination(void)
{
    object_size_calls += 1;
    return object_size_storage;
}
static __SIZE_TYPE__ object_size_outer(__SIZE_TYPE__ size)
{
    object_size_calls += 10;
    return size;
}

// LZ4 spells its block copies __builtin_memcpy/__builtin_memmove once
// __clang__ is defined, so the builtins have to lower to the library calls
// they name, with the prototype's argument conversions and the destination
// returned.
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    if (__builtin_object_size(object_size_destination(), 0) != (__SIZE_TYPE__)-1) return 15;
    if (__builtin_object_size(object_size_destination(), 1) != (__SIZE_TYPE__)-1) return 16;
    if (__builtin_object_size(object_size_destination(), 2) != 0) return 17;
    if (__builtin_object_size(object_size_destination(), 3) != 0) return 18;
    char* object_pointer = object_size_storage;
    if (__builtin_object_size(object_pointer++, 0) != (__SIZE_TYPE__)-1) return 19;
    (void)__builtin_object_size((object_size_destination(), object_pointer), 0);
    if (object_pointer != object_size_storage || object_size_calls != 0) return 20;
    if (object_size_outer(__builtin_object_size(object_size_destination(), 0)) != (__SIZE_TYPE__)-1) return 21;
    if (object_size_calls != 10) return 22;
    object_size_calls = 0;
    // Constant predicates also discard direct, nested and indirect calls.
    char* (*constant_indirect)(void) = object_size_destination;
    if (__builtin_constant_p(object_size_destination())) return 25;
    if (__builtin_constant_p(object_size_outer((__SIZE_TYPE__)object_size_destination()))) return 26;
    if (__builtin_constant_p(constant_indirect())) return 27;
    if (__builtin_constant_p((object_size_destination(), 1))) return 28;
    if (object_size_calls != 0) return 29;
    if (object_size_outer(__builtin_constant_p(object_size_destination())) != 0) return 30;
    if (object_size_calls != 10) return 31;
    object_size_calls = 0;
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
    // The call used as the actual destination executes once; its second
    // appearance in the size query does not execute at all.
    if (__builtin___memcpy_chk(object_size_destination(), source, bounded_count,
            __builtin_object_size(object_size_destination(), 0)) != object_size_storage) return 23;
    if (object_size_calls != 1 || object_size_storage[3] != 0x5a) return 24;
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
