typedef _BitInt(200) W;
typedef unsigned _BitInt(300) U;
static const U k = (U)1 << 250;
_Static_assert(__builtin_align_up((W)5, (W)8) == 8 && __builtin_align_down((U)1 << 250 | 7, (U)1 << 200) == (U)1 << 250, "");
_Static_assert(__builtin_is_aligned((U)1 << 250, (U)1 << 240) && __builtin_align_up(5, (W)16) == 16, "");
_Static_assert(__builtin_align_up((U)3, 1u << 31) == (U)1 << 31 && __builtin_align_up((U)5, k) == k, "");
U f(U x)
{
return __builtin_align_down(x, k) + __builtin_align_up(x, (W)1 << 150);
}
