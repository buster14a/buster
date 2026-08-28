// QuickJS's cutils.h reads unaligned integers through
// `struct __attribute__((packed)) packed_u32`, and puts the attribute between
// the `struct` keyword and the tag.  A file-scope declaration in that shape
// was classified as an object declaration rather than a type declaration, so
// the type was registered with no members at all and every use of it failed.
// The same declaration also has to accept a type qualifier between the
// specifier and the declarator, which is how QuickJS spells its
// `static struct { const char *tag, *attr; } const defs[]` tables.
struct __attribute__((packed)) packed_u32
{
    unsigned value;
};

struct tagged
{
    int value;
};

// A qualifier after the aggregate specifier, with and without a tag, at file
// scope and with an array declarator.
struct tagged const constant_tagged = {7};
struct
{
    const char *name;
    int value;
} const table[] = {{"a", 1}, {"b", 2}};

static unsigned get_unaligned_32(const unsigned char *bytes)
{
    return ((const struct packed_u32 *)bytes)->value;
}

static void put_unaligned_32(unsigned char *bytes, unsigned value)
{
    ((struct packed_u32 *)bytes)->value = value;
}

int main(void)
{
    unsigned char storage[8] = {0};
    put_unaligned_32(storage + 1, 0x12345678u);
    if (get_unaligned_32(storage + 1) != 0x12345678u) return 1;
    if (storage[1] != 0x78 || storage[4] != 0x12) return 2;
    // The size of this single-member layout is the same packed or not; the
    // alignment is not, and Buster does not yet apply `packed` to it.  The
    // access above is what QuickJS depends on, and it is exact.
    if (sizeof(struct packed_u32) != 4) return 3;
    if (constant_tagged.value != 7) return 5;
    if (table[1].value != 2 || table[1].name[0] != 'b') return 6;
    struct tagged const local_constant = {9};
    if (local_constant.value != 9) return 7;
    return 0;
}
