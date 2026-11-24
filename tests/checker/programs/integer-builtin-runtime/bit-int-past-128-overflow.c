typedef _BitInt(200) S200;typedef unsigned _BitInt(200) U200;typedef _BitInt(129) S129;typedef unsigned _BitInt(256) U256;
typedef unsigned _BitInt(129) U129;typedef _BitInt(300) S300;
static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
static void raw(const void*p,unsigned n){unsigned long long w[8];__builtin_memcpy(w,p,n);for(unsigned i=0;i<n/8;i++)mix(w[i]);}
static U256 val(unsigned i,unsigned long long s){U256 one=1;U256 v=0;
switch(i%12){case 0:v=0;break;case 1:v=1;break;case 2:v=~(U256)0;break;case 3:v=(one<<199)-1;break;case 4:v=one<<199;break;
case 5:v=(one<<128)-1;break;case 6:v=one<<128;break;case 7:v=~((one<<199)-1);break;case 8:v=(one<<64)+s;break;
case 9:v=((U256)s<<130)|s;break;case 10:v=~(((U256)s)<<70);break;default:v=((U256)s<<(s%190))^s;}
return v;}
int tested(int n){h=(unsigned)n;unsigned long long s=0x9e3779b97f4a7c15ull*(unsigned)(n+1);
for(unsigned i=0;i<24;i++)for(unsigned j=0;j<24;j++){s^=s<<13;s^=s>>7;s^=s<<17;
U256 x=val(i,s),y=val(j,s*3);S200 a=(S200)x,b=(S200)y;U200 ua=(U200)x,ub=(U200)y;S129 c=(S129)x;U129 uc=(U129)y;
S200 r200;U200 ru200;S129 r129;U129 ru129;S300 r300;U256 ru256;int ri;long rl;unsigned char rc;__int128 rw;_BitInt(100) r100;_Bool rb;
mix(__builtin_add_overflow(a,b,&r200));raw(&r200,32);mix(__builtin_sub_overflow(a,b,&r200));raw(&r200,32);
mix(__builtin_add_overflow(ua,ub,&ru200));raw(&ru200,32);mix(__builtin_sub_overflow(ua,ub,&ru200));raw(&ru200,32);
mix(__builtin_mul_overflow(ua,ub,&ru200));raw(&ru200,32);mix(__builtin_mul_overflow(ua,(unsigned)i,&ru256));raw(&ru256,32);
mix(__builtin_add_overflow(a,ub,&r129));raw(&r129,24);mix(__builtin_sub_overflow(c,uc,&ru129));raw(&ru129,24);
mix(__builtin_add_overflow(c,a,&r300));raw(&r300,40);mix(__builtin_sub_overflow(ua,b,&ri));mix((unsigned)ri);
mix(__builtin_add_overflow(a,(long)s,&rl));mix((unsigned long)rl);mix(__builtin_sub_overflow(ua,(int)i,&rc));mix(rc);
mix(__builtin_add_overflow(c,(__int128)s,&rw));raw(&rw,16);mix(__builtin_sub_overflow(a,c,&r100));raw(&r100,16);
mix(__builtin_add_overflow((int)i,(long)j,&r200));raw(&r200,32);mix(__builtin_mul_overflow(uc,ua,&ru256));raw(&ru256,32);
mix(__builtin_mul_overflow(ua,ub,&ru129));raw(&ru129,24);mix(__builtin_mul_overflow(uc,uc,&rl));mix((unsigned long)rl);
mix(__builtin_mul_overflow(ua,(unsigned long)s,&ri));mix((unsigned)ri);mix(__builtin_add_overflow(ua,ub,&rb));mix(rb);
mix(__builtin_mul_overflow(ua,(unsigned __int128)s,&ru200));raw(&ru200,32);
}return (int)(h^(h>>32));}