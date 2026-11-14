#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);
struct B { unsigned x : 3; };
void f(char c, unsigned short s, long l, unsigned __int128 q, _BitInt(37) w, int* p, const char* k, int a[4], struct B r, const int ci)
{
T(__builtin_align_up(c, 4), char) T(__builtin_align_down(s, 2), unsigned short) T(__builtin_align_up(l, (char)8), long)
T(__builtin_align_down(q, (unsigned __int128)16), unsigned __int128) T(__builtin_align_up(w, 4), _BitInt(37))
T(__builtin_align_up(p, 64), int*) T(__builtin_align_down(k, 2), const char*) T(__builtin_align_up(a, 8), int*)
T(__builtin_align_up(r.x, 2), unsigned int) T(__builtin_is_aligned(p, 8), _Bool) T(__builtin_is_aligned(c, 2), _Bool)
T(__builtin_expect_with_probability(c, 1, 0.5), long) T(__builtin_align_up(ci, 4) + 0, int)
}
