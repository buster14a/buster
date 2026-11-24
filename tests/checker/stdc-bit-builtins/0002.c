unsigned stdc_leading_zeros_ui(const unsigned);
_Static_assert(stdc_leading_zeros_ui(1) == 31, "");
unsigned f(double d)
{
extern unsigned stdc_count_ones_ui(unsigned);
return stdc_leading_zeros_ui(d) + stdc_count_ones_ui(1.5);
}
