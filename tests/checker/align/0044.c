static const int x = 5;
static const int a = 8;
_Static_assert(__builtin_align_up(x, 8) == 8 && __builtin_align_down(13, a) == 8 && __builtin_is_aligned(16, a), "");
int sizes[__builtin_align_up(x, 8)];
int f(void)
{
const int y = 6;
switch (0) { case __builtin_align_up(y, 4) - 8: return 1; }
return 0;
}
