typedef __typeof__((_Complex char)0+(_BitInt(200))0) CB;typedef __typeof__((_Complex char)0+(unsigned _BitInt(129))0) CU;
typedef __typeof__((_Complex char)0+(_BitInt(300))0) CW;typedef _BitInt(200) B;
static unsigned long fold(unsigned long h,const void*p,unsigned bytes){const unsigned char*b=(const unsigned char*)p;for(unsigned i=0;i<bytes;i++)h=(h^b[i])*1099511628211ul;return h;}
#define FOLD(x) do{__typeof__(x) t_=(x);h=fold(h,&t_,sizeof t_);}while(0)
static unsigned long foldb(unsigned long h,const void*p,unsigned bytes,unsigned bits){const unsigned char*b=(const unsigned char*)p;
for(unsigned i=0;i<bytes;i++){unsigned char c=b[i];if(8*i+8>bits)c=8*i>=bits?0:(unsigned char)(c&((1u<<(bits-8*i))-1));h=(h^c)*1099511628211ul;}return h;}
#define FOLDB(x,bits) do{__typeof__(x) t_=(x);h=foldb(h,&t_,sizeof t_,bits);}while(0)
#define FOLDC(x,bits) do{__typeof__(x) t_=(x);__typeof__(__real__ t_) r_=__real__ t_,i_=__imag__ t_;FOLDB(r_,bits);FOLDB(i_,bits);}while(0)
static CB mul(CB a,CB b){return a*b;}static CU mix(int n,CU a,double d,CU b){return a*b-(CU)(long)d+(CU)n;}CB gc;
static CB sum(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);CB r=0;for(int i=0;i<n;i++)r+=__builtin_va_arg(ap,CB);__builtin_va_end(ap);return r;}
int tested(int n){unsigned long h=2166136261u;CB a,b;__real__ a=(B)n*((B)1<<150);__imag__ a=-(B)(n+2)*1000003;__real__ b=(n&7)+1;__imag__ b=n%3-1;B q=(B)n*7+1;
CU u,v;__real__ u=(unsigned _BitInt(129))n<<128|5;__imag__ u=~(CU)0/3;__real__ v=(n&3)+1;__imag__ v=n&1;
CB c1=a+b,c2=a-b,c3=a*b,c4=a/b,c5=-a,c6=~a,c7=a*q,c8=a/q,c9=q+a,c10=q-a,c11=q*a,c12=q/b,c13=a+q,c14=a-q;
FOLDC(c1,200);FOLDC(c2,200);FOLDC(c3,200);FOLDC(c4,200);FOLDC(c5,200);FOLDC(c6,200);FOLDC(c7,200);FOLDC(c8,200);FOLDC(c9,200);FOLDC(c10,200);FOLDC(c11,200);FOLDC(c12,200);FOLDC(c13,200);FOLDC(c14,200);
CU u1=u+v,u2=u*v,u3=u/v,u4=-u;FOLDC(u1,129);FOLDC(u2,129);FOLDC(u3,129);FOLDC(u4,129);
int cmp=(a==b)+(a!=b)*2+(a==a)*4+(c1!=c1)*8+!a*16+(q==a)*32+(b!=q)*64+(u==v)*128;if(a)h+=3;
_Complex double cd=b;_Complex int ci=b;__typeof__((_Complex char)0+(__int128)0) cl=b;CW cw=a;CU cu=b;long l=b;B re=a;_Bool bo=c6;double d=b;
FOLD(cd);FOLD(ci);FOLD(cl);FOLDC(cw,300);FOLDC(cu,129);h+=(unsigned long)l*7+bo*5;FOLDB(re,200);FOLD(d);
CB fromd=cd,fromi=ci,froml=cl,fromw=cw,fromr=d,fromq=q,fromn=n;FOLDC(fromd,200);FOLDC(fromi,200);FOLDC(froml,200);FOLDC(fromw,200);FOLDC(fromr,200);FOLDC(fromq,200);FOLDC(fromn,200);
gc+=a;gc*=b;gc-=q;gc/=b;FOLDC(gc,200);CB w=a;w+=b;w*=2;w/=b;w-=1;FOLDC(w,200);B rq=n;rq+=b;rq*=a;FOLDB(rq,200);int ii=7;ii+=b;h+=(unsigned long)ii;
FOLDC(mul(a,b),200);FOLDC(mix(n|1,u,0.5,v),129);FOLDC(sum(3,a,b,c3),200);
return (int)(h%1000003)^cmp;}
