_Alignas(64) int global_aligned = 3;
_Alignas(long double) int type_aligned = 13;
_Alignas(16) _Alignas(64) int multiple_aligned = 17;

struct AlignedMember
{
    char prefix;
    _Alignas(32) int value;
};

int main(void)
{
    _Alignas(64) int local_aligned = 5;
    _Alignas(0) int zero_aligned = 19;
    static _Alignas(64) int static_aligned = 7;
    struct AlignedMember member = { 1, 11 };

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
    if ((unsigned long long)&type_aligned &
        (_Alignof(long double) - 1))
    {
        return 6;
    }
    if ((unsigned long long)&multiple_aligned & 63)
    {
        return 7;
    }
    return global_aligned + local_aligned +
        static_aligned + member.value +
        type_aligned + multiple_aligned +
        zero_aligned == 75 ? 0 : 8;
}
