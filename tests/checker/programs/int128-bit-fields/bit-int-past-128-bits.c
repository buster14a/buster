typedef _BitInt(129) S129;typedef unsigned _BitInt(129) U129;typedef _BitInt(200) S200;typedef unsigned _BitInt(200) U200;
typedef _BitInt(256) S256;typedef unsigned _BitInt(256) U256;typedef _BitInt(300) S300;typedef unsigned _BitInt(511) U511;typedef unsigned _BitInt(2100) U2100;
static unsigned long fold(unsigned long h,const void*p,unsigned bytes,unsigned bits){const unsigned char*b=(const unsigned char*)p;
for(unsigned i=0;i<bytes;i++){unsigned char c=b[i];if(8*i+8>bits)c=8*i>=bits?0:(unsigned char)(c&((1u<<(bits-8*i))-1));h=(h^c)*1099511628211ul;}return h;}
#define FOLD(x,bits) do{__typeof__(x) t_=(x);h=fold(h,&t_,sizeof t_,bits);}while(0)
struct P{char c;S200 v;U129 w[2];};S200 gs;U2100 gb;static S256 gw;
static S200 add(S200 a,S200 b){return a+b;}
static U129 mix(int n,U129 a,double d,long e,U129 b,char c){return a*b-(U129)n+(U129)(long)d+(U129)e*(U129)c;}
static S256 sum(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);S256 r=0;for(int i=0;i<n;i++){r+=__builtin_va_arg(ap,S256);r=r>>1;}__builtin_va_end(ap);return r;}
static struct P pass(struct P p,int n){p.v-=n;p.w[1]^=p.w[0];return p;}
static U2100 big(U2100 a,U2100 b){return (a<<1000)+b*b;}
int tested(int n){unsigned long h=2166136261u;
S200 a=(S200)n*((S200)1<<150),b=-(S200)n*123456789,d=b+7;
U200 u=(U200)n<<199|5,v=~(U200)0/3;
S256 w=(S256)(-n)*((S256)1<<70),x=(S256)n*1000000007;
U129 p=(U129)n<<128|7;S300 q=-(S300)(n+4)*((S300)1<<250);
FOLD(a+b,200);FOLD(a-b,200);FOLD(-b,200);FOLD(~b,200);FOLD(a&b,200);FOLD(a|b,200);FOLD(a^b,200);
FOLD(u+v,200);FOLD(u-v,200);FOLD(-u,200);FOLD(~u,200);FOLD(u*v,200);FOLD(u/v,200);FOLD(u%v,200);
FOLD(a*b,200);FOLD(a/d,200);FOLD(a%d,200);FOLD(b/7,200);FOLD(b%7,200);FOLD(d/-3,200);
FOLD((U256)w<<3,256);FOLD(w>>3,256);FOLD((U256)w<<64,256);FOLD(w>>64,256);FOLD((U256)w<<130,256);FOLD(w>>130,256);FOLD(w>>255,256);
FOLD(u>>5,200);FOLD(u<<5,200);FOLD(u>>199,200);FOLD(b>>199,200);FOLD(b>>(n*37&127),200);FOLD(u<<(n*37&127),200);FOLD(u>>(n*0),200);
FOLD(u>>(S256)(n&63),200);FOLD(w>>(unsigned __int128)(n&127),256);FOLD(u<<(unsigned char)(n&7),200);FOLD(u>>(U511)64,200);
FOLD(p+p,129);FOLD(p*3,129);FOLD(p>>1,129);FOLD(-p,129);FOLD(p<<64,129);
FOLD(q>>3,300);FOLD(q+1,300);FOLD(q/12345,300);FOLD(q%12345,300);
int cmp=(a<b)+(a<=b)*2+(a>b)*4+(a>=b)*8+(a==b)*16+(a!=b)*32+!a*64+(b<0)*128+(u>v)*256+(w<x)*512+(q<w)*1024+(p>=(U129)7)*2048+(a==a)*4096;
long l=b;unsigned long ul=u;int i=w;__int128 i128=b;unsigned __int128 u128=u;_Bool bo=w;signed char sc=b;_BitInt(100) b100=b;unsigned _BitInt(13) b13=u;_BitInt(65) b65=x;
h=fold(h,&l,8,64);h=fold(h,&ul,8,64);h=fold(h,&i,4,32);h=fold(h,&i128,16,128);h=fold(h,&u128,16,128);h+=bo*3+sc*5;FOLD(b100,100);FOLD(b13,13);FOLD(b65,65);
double fd=b;float ff=v;long double fl=w;__float128 fq=a;h=fold(h,&fd,8,64);h=fold(h,&ff,4,32);h=fold(h,&fl,10,80);h=fold(h,&fq,16,128);
S200 fromd=-1.5e40*(n+4),froml=(S200)-123456789012345L*n;U200 fromf=2.5e30f*(n+4);S256 fromld=-1.25e70L*(n+4);S200 fromq=(__float128)1e45*(n+4);
U200 fromi128=(unsigned __int128)~0/7*(unsigned __int128)(n+4);S256 fromb=(_BitInt(100))-5*n;S300 fromx=b;S129 fromc=(signed char)n;U129 fromu=(unsigned)n;
FOLD(fromd,200);FOLD(froml,200);FOLD(fromf,200);FOLD(fromld,256);FOLD(fromq,200);FOLD(fromi128,200);FOLD(fromb,256);FOLD(fromx,300);FOLD(fromc,129);FOLD(fromu,129);
FOLD((S200)q,200);FOLD((U129)b,129);FOLD((S256)u,256);FOLD((U511)b,511);FOLD((S129)w,129);
gs+=a;gs*=3;gs-=b;gw=w;gw>>=2;gw=(S256)((U256)gw<<1);gw-=x;gw|=1;gw^=x;gw&=~(S256)7;gw%=1000003;gw/=7;FOLD(gs,200);FOLD(gw,256);
S200 c=a;c++;++c;c--;S200 old=c++;--c;FOLD(c,200);FOLD(old,200);U200 e=u;e<<=n&63;e>>=3;e+=v;e*=7;FOLD(e,200);
S200 fa=b;fa+=2.5;fa*=1.5f;fa-=(long double)n*1e30L;fa/=-3.0;U256 fu=(U256)v;fu+=(__float128)n*1e40;fu*=(__float128)0.5;S200 fr=(fa-=0.25);
FOLD(fa,200);FOLD(fu,256);FOLD(fr,200);S200 fz=b;fz+=1.5+2.0i;fz*=2.0-1.0i;U256 fv=(U256)v;fv-=(_Complex long double)(1e30L+3.0iL);
FOLD(fz,200);FOLD(fv,256);
int sh=7<<(S200)(n&15);unsigned long shl=(unsigned long)(n+3);shl<<=(U2100)(n&31);unsigned _BitInt(100) shb=(unsigned _BitInt(100))(n+3)<<(S256)(n&63);
unsigned __int128 shq=(unsigned __int128)~0u>>(U129)(n&127);h+=(unsigned long)sh*3+shl*5;FOLD(shb,100);h=fold(h,&shq,16,128);
int k=5;k+=b;k/=7;long m2=3;m2-=w;m2=(long)(m2%1000);_Bool flag=0;flag|=(_Bool)(w!=0);h+=(unsigned long)k*7+(unsigned long)m2*11+flag;
FOLD(add(a,b),200);FOLD(mix(n,p,2.5,-7,p+1,'x'),129);FOLD(sum(3,w,x,w),256);
struct P sp={'q',a,{p,p+3}};struct P rp=pass(sp,n);FOLD(rp.v,200);FOLD(rp.w[1],129);U129 arr[3]={p,p*2,p*3};FOLD(arr[n&1?1:2],129);
U2100 g1=(U2100)n*0x123456789abcdefull,g2=big(g1,(U2100)v);gb=g2;gb>>=777;FOLD(g2,2100);FOLD(gb,2100);FOLD(g2/(g1|1),2100);h+=(g2>g1)+(gb!=0)*2;
if(w)h+=17;if(!u)h+=19;h+=(a&&u)*23+(q||0)*29;S200 sel=n>3?a:b;FOLD(sel,200);
return (int)(h%1000003)^cmp;}
