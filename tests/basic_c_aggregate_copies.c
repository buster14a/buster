// Aggregate copies move whole sixteen-byte chunks through XMM0
// (docs/machine-rewrite-campaign.md), then 8/4/2/1-byte tails. Every size
// class and every copy direction the machine rows cover is checked byte by
// byte: frame to frame, frame to pointer, pointer to frame, self-assignment,
// by-value arguments and results, and unaligned (packed) destinations.
// main returns the first failing check.

typedef unsigned char u8;

#define DEFINE_BLOB(size) \
    typedef struct Blob##size { u8 bytes[size]; } Blob##size; \
    __attribute__((noinline)) static Blob##size make##size(u8 seed) \
    { \
        Blob##size blob; \
        for (int index = 0; index < size; index += 1) blob.bytes[index] = (u8)(seed + index * 7); \
        return blob; \
    } \
    __attribute__((noinline)) static int check##size(Blob##size const* blob, u8 seed) \
    { \
        int ok = 1; \
        for (int index = 0; index < size; index += 1) ok &= blob->bytes[index] == (u8)(seed + index * 7); \
        return ok; \
    } \
    __attribute__((noinline)) static Blob##size pass##size(Blob##size blob) \
    { \
        Blob##size copy = blob; \
        return copy; \
    } \
    __attribute__((noinline)) static void store##size(Blob##size* destination, Blob##size value) \
    { \
        *destination = value; \
    } \
    __attribute__((noinline)) static Blob##size load##size(Blob##size const* source) \
    { \
        Blob##size value = *source; \
        return value; \
    } \
    __attribute__((noinline)) static int run##size(u8 seed) \
    { \
        Blob##size first = make##size(seed); \
        Blob##size second = first; \
        int ok = check##size(&second, seed); \
        second = second; \
        ok &= check##size(&second, seed); \
        Blob##size third = pass##size(second); \
        ok &= check##size(&third, seed); \
        Blob##size heap[3]; \
        heap[0] = make##size((u8)(seed + 1)); \
        heap[2] = make##size((u8)(seed + 2)); \
        store##size(&heap[1], third); \
        ok &= check##size(&heap[0], (u8)(seed + 1)) & check##size(&heap[1], seed) & check##size(&heap[2], (u8)(seed + 2)); \
        Blob##size fourth = load##size(&heap[1]); \
        ok &= check##size(&fourth, seed); \
        return ok; \
    }

DEFINE_BLOB(1)
DEFINE_BLOB(7)
DEFINE_BLOB(8)
DEFINE_BLOB(12)
DEFINE_BLOB(15)
DEFINE_BLOB(16)
DEFINE_BLOB(17)
DEFINE_BLOB(24)
DEFINE_BLOB(31)
DEFINE_BLOB(32)
DEFINE_BLOB(33)
DEFINE_BLOB(48)
DEFINE_BLOB(56)
DEFINE_BLOB(64)
DEFINE_BLOB(88)
DEFINE_BLOB(176)
DEFINE_BLOB(255)
DEFINE_BLOB(752)

typedef struct Mixed
{
    long long a;
    double b;
    int c;
    short d;
    char e;
    long long f[3];
} Mixed;

typedef struct __attribute__((packed)) Packed
{
    char lead;
    Mixed inner;
    char tail;
} Packed;

__attribute__((noinline)) static Mixed mixed(long long seed)
{
    Mixed value;
    value.a = seed;
    value.b = (double)seed * 0.5;
    value.c = (int)seed + 3;
    value.d = (short)(seed - 9);
    value.e = (char)seed;
    value.f[0] = seed * 11;
    value.f[1] = -seed;
    value.f[2] = seed ^ 0x5a5a;
    return value;
}

__attribute__((noinline)) static int mixed_equal(Mixed const* x, Mixed const* y)
{
    return x->a == y->a && x->b == y->b && x->c == y->c && x->d == y->d && x->e == y->e && x->f[0] == y->f[0] &&
           x->f[1] == y->f[1] && x->f[2] == y->f[2];
}

int main(void)
{
    int result = 0;
    int ok[18] = {run1(3), run7(5), run8(7), run12(11), run15(13), run16(17), run17(19), run24(23), run31(29), run32(31),
                  run33(37), run48(41), run56(43), run64(47), run88(53), run176(59), run255(61), run752(67)};
    for (int index = 0; index < 18 && !result; index += 1)
    {
        if (!ok[index]) result = 1 + index;
    }
    Packed packed;
    packed.lead = 'L';
    packed.tail = 'T';
    Mixed value = mixed(12345);
    packed.inner = value;
    Mixed back = packed.inner;
    if (!result && (!mixed_equal(&back, &value) || packed.lead != 'L' || packed.tail != 'T')) result = 30;
    Mixed array[4];
    for (int index = 0; index < 4; index += 1) array[index] = mixed(index + 100);
    array[1] = array[2];
    array[3] = array[3];
    Mixed expected_one = mixed(102);
    Mixed expected_three = mixed(103);
    if (!result && (!mixed_equal(&array[1], &expected_one) || !mixed_equal(&array[3], &expected_three))) result = 31;
    return result;
}
