// #837: Clang's SSE2 headers use these five compiler-owned builtins.
// Buster lowers them to target-independent canonical vector operations;
// the regression also covers the immediate form without an explicit ABI helper.
#if __has_builtin(__builtin_ia32_pslldi128) != 1
#error __builtin_ia32_pslldi128 must be advertised
#endif
#if __has_builtin(__builtin_ia32_psllqi128) != 1
#error __builtin_ia32_psllqi128 must be advertised
#endif
#if __has_builtin(__builtin_ia32_psradi128) != 1
#error __builtin_ia32_psradi128 must be advertised
#endif
#if __has_builtin(__builtin_ia32_psrldi128) != 1
#error __builtin_ia32_psrldi128 must be advertised
#endif
#if __has_builtin(__builtin_ia32_psrlqi128) != 1
#error __builtin_ia32_psrlqi128 must be advertised
#endif

typedef int V4I32 __attribute__((vector_size(16)));
typedef long long V2I64 __attribute__((vector_size(16)));

static int v4_equal(V4I32 value, int lane0, int lane1, int lane2, int lane3)
{
    return value[0] == lane0 && value[1] == lane1 && value[2] == lane2 && value[3] == lane3;
}

static int v2_equal(V2I64 value, long long lane0, long long lane1)
{
    return value[0] == lane0 && value[1] == lane1;
}

int main(void)
{
    int valid = 1;
    V4I32 words = {1, -2, 0x10000000, -8};
    V4I32 signed_words = {8, -8, 0x40000000, (-2147483647 - 1)};
    V4I32 logical_words = {8, -8, -1, 0x40000000};
    V2I64 qwords = {1, -2};
    V2I64 logical_qwords = {8, -8};

    valid &= v4_equal(__builtin_ia32_pslldi128(words, 0), 1, -2, 0x10000000, -8);
    valid &= v2_equal(__builtin_ia32_psllqi128(qwords, 0), 1, -2);
    valid &= v4_equal(__builtin_ia32_psradi128(signed_words, 0), 8, -8, 0x40000000, (-2147483647 - 1));
    valid &= v4_equal(__builtin_ia32_psrldi128(logical_words, 0), 8, -8, -1, 0x40000000);
    valid &= v2_equal(__builtin_ia32_psrlqi128(logical_qwords, 0), 8, -8);

    valid &= v4_equal(__builtin_ia32_pslldi128(words, 1 + 1), 4, -8, 0x40000000, -32);
    valid &= v2_equal(__builtin_ia32_psllqi128(qwords, 3), 8, -16);
    valid &= v4_equal(__builtin_ia32_psradi128(signed_words, 2), 2, -2, 0x10000000, -536870912);
    valid &= v4_equal(__builtin_ia32_psrldi128(logical_words, 2), 2, 1073741822, 1073741823, 268435456);
    valid &= v2_equal(__builtin_ia32_psrlqi128(logical_qwords, 2), 2, 0x3ffffffffffffffeLL);

    valid &= v4_equal(__builtin_ia32_pslldi128(words, 32), 0, 0, 0, 0);
    valid &= v2_equal(__builtin_ia32_psllqi128(qwords, 65), 0, 0);
    valid &= v4_equal(__builtin_ia32_psrldi128(logical_words, 33), 0, 0, 0, 0);
    valid &= v2_equal(__builtin_ia32_psrlqi128(logical_qwords, 64), 0, 0);
    valid &= v4_equal(__builtin_ia32_psradi128(signed_words, 32), 0, -1, 0, -1);

    return valid ? 0 : 1;
}
