static const unsigned ku=7u;
static const unsigned _BitInt(37) kw=37;
static const unsigned _BitInt(16) kw16=0x1234;
static const unsigned short ks=0x1234;
static const unsigned char kc=0xf0;
_Static_assert(__builtin_popcountg(ku)==3 && __builtin_clzg(ku,99)==29 && __builtin_ctzg(ku,99)==0, "");
_Static_assert(__builtin_bswapg(ks)==0x3412 && __builtin_bitreverseg((unsigned char)ku)==0xe0, "");
_Static_assert(__builtin_stdc_count_ones(ku)==3 && __builtin_stdc_bit_width(ku)==3 && __builtin_stdc_bit_ceil(ku)==8, "");
_Static_assert(__builtin_popcountg(kw)==3 && __builtin_bswapg(kw16)==0x3412 && __builtin_stdc_rotate_left(kc,4)==0x0f, "");
static int gs = __builtin_popcountg(ku);
int ga[__builtin_popcountg(ku)];
enum { GE=__builtin_stdc_bit_floor(ku) };
struct GF { unsigned field:__builtin_stdc_bit_width(ku); };
unsigned f(unsigned n) { switch (n) { case __builtin_stdc_count_ones(ku): return gs; } return 0; }
