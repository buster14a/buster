unsigned int stdc_leading_zeros_uc(unsigned char);
unsigned int stdc_count_ones_us(unsigned short);
unsigned int stdc_bit_width_ui(unsigned int);
unsigned long stdc_bit_floor_ul(unsigned long);
_Bool stdc_has_single_bit_ull(unsigned long long);
unsigned char stdc_bit_ceil_uc(unsigned char);
unsigned int stdc_trailing_zeros_ull(unsigned long long);
unsigned int stdc_first_leading_one_ul(unsigned long);
#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);
void f(unsigned char c, int i)
{
T(stdc_leading_zeros_uc(c), unsigned int) T(stdc_bit_floor_ul(i), unsigned long)
T(stdc_has_single_bit_ull(i), _Bool) T(stdc_bit_ceil_uc(i), unsigned char)
}
_Static_assert(stdc_leading_zeros_uc(1) == 7 && stdc_count_ones_us(-1) == 16 && stdc_bit_width_ui(5) == 3 && stdc_bit_floor_ul(100) == 64, "");
_Static_assert(stdc_has_single_bit_ull(1ull << 63) && !stdc_has_single_bit_ull(3) && stdc_bit_ceil_uc(200) == 0 && stdc_bit_ceil_uc(0) == 1, "");
_Static_assert(stdc_leading_zeros_uc(-1) == 0 && stdc_count_ones_us(0x10000) == 0 && stdc_leading_zeros_uc(256) == 8, "");
enum { E = stdc_trailing_zeros_ull(1ull << 40) };
static int s = stdc_first_leading_one_ul(1);
int a[stdc_bit_ceil_uc(3)];
_Static_assert(E == 40 && sizeof a == 4 * sizeof(int), "");
