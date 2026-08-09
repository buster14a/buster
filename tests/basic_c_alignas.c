typedef unsigned char u8;

_Alignas(64) int global_aligned = 3;
_Alignas(long double) int type_aligned = 13;
_Alignas(16) _Alignas(64) int multiple_aligned = 17;

struct AlignedMember
{
    char prefix;
    _Alignas(32) int value;
};

// An alignment specifier belongs to the declaration specifiers, so it has to
// be accepted ahead of a typedef name or a struct tag, not just ahead of a
// builtin type keyword, and after the type as well.
static _Alignas(64) u8 typedef_array[64];
int _Alignas(64) trailing_specifier = 23;
typedef int AlignedInt;
AlignedInt _Alignas(64) trailing_typedef_specifier = 29;

struct AlignedTag
{
    int value;
};
_Alignas(64) struct AlignedTag tag_aligned = {31};

typedef struct AlignedTypedefMember
{
    _Alignas(64) u8 bytes[64];
} AlignedTypedefMember;

typedef struct AlignedTypedefTail
{
    u8 head;
    _Alignas(64) u8 tail[8];
} AlignedTypedefTail;

_Static_assert(sizeof(AlignedTypedefMember) == 64, "over-aligned member keeps the struct one cache line");
_Static_assert(_Alignof(AlignedTypedefMember) == 64, "member alignment raises the struct alignment");
_Static_assert(sizeof(AlignedTypedefTail) == 128, "the aligned tail is padded up to its own alignment");
_Static_assert(_Alignof(AlignedTypedefTail) == 64, "the aligned tail raises the struct alignment");

int main(void)
{
    _Alignas(64) int local_aligned = 5;
    _Alignas(0) int zero_aligned = 19;
    static _Alignas(64) int static_aligned = 7;
    struct AlignedMember member = {1, 11};
    _Alignas(64) u8 typedef_local[64];
    AlignedTypedefMember typedef_member;
    AlignedTypedefTail typedef_tail;

    if (_Alignof(struct AlignedMember) != 32)
    {
        return 1;
    }
    if (__builtin_offsetof(struct AlignedMember, value) != 32)
    {
        return 2;
    }
    if ((unsigned long long)&global_aligned & 63)
    {
        return 3;
    }
    if ((unsigned long long)&local_aligned & 63)
    {
        return 4;
    }
    if ((unsigned long long)&static_aligned & 63)
    {
        return 5;
    }
    if ((unsigned long long)&type_aligned & (_Alignof(long double) - 1))
    {
        return 6;
    }
    if ((unsigned long long)&multiple_aligned & 63)
    {
        return 7;
    }

    if (sizeof(AlignedTypedefMember) != 64 || _Alignof(AlignedTypedefMember) != 64)
    {
        return 9;
    }
    if (sizeof(AlignedTypedefTail) != 128 || _Alignof(AlignedTypedefTail) != 64)
    {
        return 10;
    }
    if (__builtin_offsetof(AlignedTypedefTail, tail) != 64)
    {
        return 11;
    }
    if ((unsigned long long)&typedef_array[0] & 63)
    {
        return 12;
    }
    if ((unsigned long long)&typedef_local[0] & 63)
    {
        return 13;
    }
    if ((unsigned long long)&typedef_member & 63)
    {
        return 14;
    }
    if ((unsigned long long)&typedef_tail.tail[0] & 63)
    {
        return 15;
    }
    if ((unsigned long long)&trailing_specifier & 63)
    {
        return 16;
    }
    if ((unsigned long long)&trailing_typedef_specifier & 63)
    {
        return 17;
    }
    if ((unsigned long long)&tag_aligned & 63)
    {
        return 18;
    }

    typedef_array[0] = 37;
    typedef_array[63] = 41;
    typedef_local[0] = 43;
    typedef_local[63] = 47;
    typedef_member.bytes[0] = 53;
    typedef_member.bytes[63] = 59;
    typedef_tail.head = 61;
    typedef_tail.tail[0] = 67;
    typedef_tail.tail[7] = 71;

    if (typedef_array[0] + typedef_array[63] + typedef_local[0] + typedef_local[63] != 168)
    {
        return 19;
    }
    if (typedef_member.bytes[0] + typedef_member.bytes[63] != 112)
    {
        return 20;
    }
    if (typedef_tail.head + typedef_tail.tail[0] + typedef_tail.tail[7] != 199)
    {
        return 21;
    }
    if (trailing_specifier + trailing_typedef_specifier + tag_aligned.value != 83)
    {
        return 22;
    }

    return global_aligned + local_aligned + static_aligned + member.value + type_aligned + multiple_aligned + zero_aligned == 75 ? 0 : 8;
}
