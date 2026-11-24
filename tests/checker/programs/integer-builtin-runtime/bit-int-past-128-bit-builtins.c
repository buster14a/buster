typedef unsigned _BitInt(200) U;typedef _BitInt(208) S;typedef unsigned _BitInt(256) W;typedef unsigned _BitInt(130) V;
static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
static void raw(const void*p,unsigned n){unsigned long long w[8];__builtin_memcpy(w,p,n);for(unsigned i=0;i<n/8;i++)mix(w[i]);}
static W val(unsigned i,unsigned long long s){W one=1;W v=0;
switch(i%14){case 0:v=0;break;case 1:v=1;break;case 2:v=~(W)0;break;case 3:v=(one<<199)-1;break;case 4:v=one<<199;break;
case 5:v=(one<<128)-1;break;case 6:v=one<<128;break;case 7:v=~((one<<199)-1);break;case 8:v=(one<<64)+s;break;
case 9:v=((W)s<<130)|s;break;case 10:v=~(((W)s)<<70);break;case 11:v=one<<(s%200);break;case 12:v=(one<<(s%200))+1;break;default:v=((W)s<<(s%190))^s;}
return v;}
int tested(int n){h=(unsigned)n;unsigned long long s=0x9e3779b97f4a7c15ull*(unsigned)(n+1);
for(unsigned i=0;i<28;i++){s^=s<<13;s^=s>>7;s^=s<<17;
W w=val(i,s);U x=(U)w;S y=(S)w;W z=w;V v=(V)w;U r;S q;W t;V rv;
mix(__builtin_popcountg(x));mix(__builtin_popcountg(z));mix(__builtin_clzg(x,-7));mix(__builtin_ctzg(x,-9));mix(__builtin_clzg(z,(int)i));
if(x){mix(__builtin_clzg(x));mix(__builtin_ctzg(x));}
mix(__builtin_stdc_leading_zeros(x));mix(__builtin_stdc_leading_ones(x));mix(__builtin_stdc_trailing_zeros(x));mix(__builtin_stdc_trailing_ones(x));
mix(__builtin_stdc_first_leading_zero(x));mix(__builtin_stdc_first_leading_one(x));mix(__builtin_stdc_first_trailing_zero(x));mix(__builtin_stdc_first_trailing_one(x));
mix(__builtin_stdc_count_zeros(x));mix(__builtin_stdc_count_ones(x));mix(__builtin_stdc_has_single_bit(x));mix(__builtin_stdc_bit_width(x));
mix(__builtin_stdc_leading_ones(z));mix(__builtin_stdc_trailing_ones(z));mix(__builtin_stdc_has_single_bit(z));
r=__builtin_stdc_bit_floor(x);raw(&r,32);r=__builtin_stdc_bit_ceil(x);raw(&r,32);t=__builtin_stdc_bit_ceil(z);raw(&t,32);
r=__builtin_bitreverseg(x);raw(&r,32);q=__builtin_bitreverseg(y);raw(&q,32);q=__builtin_bswapg(y);raw(&q,32);t=__builtin_bswapg(z);raw(&t,32);t=__builtin_bitreverseg(z);raw(&t,32);
t=__builtin_stdc_rotate_right(z,(U)~w);raw(&t,32);t=__builtin_stdc_rotate_left(z,i*8);raw(&t,32);t=__builtin_stdc_rotate_left(z,(int)(s%700)-350);raw(&t,32);
t=__builtin_stdc_rotate_right(z,(S)w);raw(&t,32);t=__builtin_stdc_rotate_left(z,(_BitInt(129))(long)s);raw(&t,32);
mix(__builtin_stdc_rotate_right((unsigned)s,y));mix(__builtin_stdc_rotate_left((unsigned char)s,x));mix(__builtin_stdc_rotate_left((unsigned short)s,(S)w));
{unsigned __int128 o=__builtin_stdc_rotate_right((unsigned __int128)w,y);raw(&o,16);unsigned _BitInt(100) p=__builtin_stdc_rotate_left((unsigned _BitInt(100))w,(V)~w);raw(&p,16);}
}return (int)(h^(h>>32));}