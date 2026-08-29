// A type name may define the aggregate it names rather than refer to one, and
// `sizeof(struct { ... })` is how musl's ioctl.c sizes the compat entries of
// its `compat_map`.  The parser registers such a definition against the
// position of its opening brace, which is what the sizeof fold resolves it
// through -- and it used to register only the ones inside a function body, so
// the same expression in a file-scope initializer had no type at all and the
// whole initializer was refused.

typedef unsigned long ulong;

// The named equivalent of the definition the initializer writes inline: the
// two spell one layout, so the expectations below are the compiler's own
// answer for the named one rather than a number written down twice.
struct misaligned
{
    int i;
    long t;
    char c[(68) - 4];
};

struct entry
{
    unsigned int request;
    unsigned char size;
    unsigned char count;
    unsigned char offsets[4];
};

// The ioctl shape: an anonymous struct defined inside the initializer of a
// file-scope array, its size shifted into a request number.
static const struct entry map[] = {
    {(2U << 30) | (sizeof(struct { int i; long t; char c[(68) - 4]; }) << 16) | 9, 68, 2, {20, 24}},
    {(2U << 30) | (sizeof(char[96]) << 16) | 20, 88, 2, {0, 4}},
    {0, 4, 0},
};

// Every aggregate keyword takes the same shape, and an initializer is not the
// only place a file-scope declaration writes one.
static const ulong anonymous_struct = sizeof(struct { int i; char c[64]; });
static const ulong tagged_struct = sizeof(struct inline_tag { int i; char c[64]; });
static const ulong anonymous_union = sizeof(union { int i; char c[9]; });
static const ulong anonymous_enum = sizeof(enum { inline_first, inline_second });
static const ulong alignment = _Alignof(struct { char c; long x; });

// The tag a file-scope type name defines is a definition like any other, so a
// later declaration may name it.
static struct inline_tag tagged_object;

int main(void)
{
    if (anonymous_struct != sizeof(struct inline_tag) || anonymous_struct != 68)
    {
        return 1;
    }
    if (tagged_struct != anonymous_struct || sizeof(tagged_object) != anonymous_struct)
    {
        return 2;
    }
    if (anonymous_union != 12 || anonymous_enum != sizeof(int))
    {
        return 3;
    }
    if (alignment != _Alignof(long))
    {
        return 4;
    }
    if (map[0].request != ((2U << 30) | ((unsigned int)sizeof(struct misaligned) << 16) | 9) || map[0].offsets[1] != 24)
    {
        return 5;
    }
    if (map[1].request != ((2U << 30) | (96U << 16) | 20) || map[1].size != 88)
    {
        return 6;
    }
    if (map[2].request != 0 || map[2].count != 0 || map[2].offsets[0] != 0)
    {
        return 7;
    }
    if (sizeof(map) / sizeof(map[0]) != 3)
    {
        return 8;
    }
    // The same spelling inside a function body already resolved; both have to
    // reach the one type the parser registered for that brace.
    if (sizeof(struct { int i; char c[64]; }) != anonymous_struct)
    {
        return 9;
    }
    return 0;
}
