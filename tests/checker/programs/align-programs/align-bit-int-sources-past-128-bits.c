typedef unsigned _BitInt(200) U;typedef _BitInt(200) S;typedef unsigned _BitInt(256) W;typedef _BitInt(129) T;typedef unsigned __int128 Q;
static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
static void raw(const void*p,unsigned n){unsigned long long w[8];__builtin_memcpy(w,p,n);for(unsigned i=0;i<n/8;i++)mix(w[i]);}
int tested(int n){h=(unsigned)n;unsigned long long s=0x9e3779b97f4a7c15ull*(unsigned)(n+1);
for(unsigned i=0;i<300;i++){s^=s<<13;s^=s>>7;s^=s<<17;
W w=((W)s<<(s%200))^s^(i%5==0?~(W)0:0);U x=(U)w;S y=(S)w;T t=(T)w;
Q a=(Q)1<<(s%128);int ai=1<<(i%31);long al=1L<<(i%63);unsigned char ac=(unsigned char)(1u<<(i%8));
U r=__builtin_align_up(x,ai);raw(&r,32);r=__builtin_align_down(x,al);raw(&r,32);mix(__builtin_is_aligned(x,a));
S q=__builtin_align_up(y,a);raw(&q,32);q=__builtin_align_down(y,ac);raw(&q,32);mix(__builtin_is_aligned(y,ai));
W v=__builtin_align_up(w,a);raw(&v,32);v=__builtin_align_down(w,al);raw(&v,32);mix(__builtin_is_aligned(w,ac));
T z=__builtin_align_up(t,a);raw(&z,24);z=__builtin_align_down(t,ai);raw(&z,24);mix(__builtin_is_aligned(t,al));
r=__builtin_align_up(x,(Q)1<<127);raw(&r,32);v=__builtin_align_up(w,16);raw(&v,32);
}return (int)(h^(h>>32));}