unsigned int stdc_leading_zeros_uc(unsigned char);
unsigned int stdc_count_ones_us(unsigned short);
unsigned int stdc_bit_width_ui(unsigned int);
unsigned long stdc_bit_floor_ul(unsigned long);
_Bool stdc_has_single_bit_ull(unsigned long long);
unsigned char stdc_bit_ceil_uc(unsigned char);
unsigned int stdc_trailing_zeros_ull(unsigned long long);
unsigned int stdc_first_leading_one_ul(unsigned long);
unsigned f(unsigned x)
{
return stdc_bit_width_ui(x, 1);
}
