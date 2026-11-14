static char buf[64];
static _Alignas(32) char aligned[64];
struct S { char a; int b; char c[8]; } s;
_Static_assert(__builtin_align_up(5, 4) == 8 && __builtin_align_down(7, 4) == 4 && __builtin_is_aligned(16, 8), "");
_Static_assert(__builtin_align_up((signed char)127, 2) == -128 && __builtin_align_up(0xffffffffu, 16) == 0, "");
_Static_assert(__builtin_align_up(-5, 4) == -4 && __builtin_align_down(-5, 4) == -8, "");
_Static_assert(__builtin_align_up((unsigned __int128)1 << 100 | 1, (unsigned __int128)1 << 64) >> 64 == ((unsigned __int128)1 << 36) + 1, "");
_Static_assert(__builtin_is_aligned(&buf[16], 16) && !__builtin_is_aligned(&buf[3], 8) && __builtin_is_aligned(&aligned[32], 32), "");
_Static_assert(!__builtin_is_aligned(&aligned[16], 32) && __builtin_is_aligned(&s.b, 4) && !__builtin_is_aligned(&s.c[1], 2), "");
_Static_assert(__builtin_is_aligned("abc", 1) && __builtin_is_aligned((char*)&s + 4, 4), "");
_Static_assert(__builtin_expect_with_probability(3, 1, 0.25) == 3, "");
static char* up = __builtin_align_up(&buf[3], 8);
static char* down = __builtin_align_down(&buf[13], 8);
static char* same = __builtin_align_up(&aligned[32], 32);
static int flag = __builtin_is_aligned(&buf[16], 16);
int sizes[__builtin_align_up(3, 4)];
enum { E = __builtin_align_down(13, 8), F = __builtin_is_aligned(&buf[16], 16) };
int g(int x) { switch (x) { case __builtin_align_up(1, 8): return 1; case __builtin_is_aligned(&buf[16], 16): return 2; } return 0; }
