#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);
enum U { UA = 1 };
struct B { unsigned x : 3; int z : 5; };
void f(unsigned char c, unsigned short s, _Bool b, enum U e, unsigned _BitInt(37) w, _BitInt(48) v, const unsigned k, struct B r)
{
T(__builtin_bitreverse8(c), unsigned char) T(__builtin_bitreverse64(1), unsigned long) T(__builtin_clrsbl(1), int) T(__builtin_ctzs(s), int)
T(__builtin_popcountg(b), int) T(__builtin_popcountg(e), int) T(__builtin_clzg(w, (short)1), int) T(__builtin_ctzg(r.x, r.z), int)
T(__builtin_bitreverseg(v), _BitInt(48)) T(__builtin_bitreverseg(k), unsigned int) T(__builtin_bswapg(e), enum U) T(__builtin_bitreverseg(r.z), int)
T(__builtin_stdc_leading_zeros(c), unsigned int) T(__builtin_stdc_has_single_bit(w), _Bool) T(__builtin_stdc_bit_floor(s), unsigned short)
T(__builtin_stdc_bit_ceil(w), unsigned _BitInt(37)) T(__builtin_stdc_rotate_left(b, -1), _Bool) T(__builtin_stdc_rotate_right(e, v), enum U)
T(__builtin_addcb(c, c, c, &c), unsigned char) T(__builtin_subcll(1, 2, 0, (unsigned long long *)0), unsigned long long)
}
