typedef _Complex __float128 CQ;typedef __float128 Q;
static unsigned long fold(unsigned long h,const void*p,unsigned bytes){const unsigned char*b=(const unsigned char*)p;for(unsigned i=0;i<bytes;i++)h=(h^b[i])*1099511628211ul;return h;}
#define FOLD(x) do{__typeof__(x) t_=(x);h=fold(h,&t_,sizeof t_);}while(0)
static CQ mul(CQ a,CQ b){return a*b;}static CQ mix(int n,CQ a,double d,Q q,CQ b){return a*b-d+q/(Q)n;}CQ gc;
static CQ sum(int n,...){__builtin_va_list ap;__builtin_va_start(ap,n);CQ r=0;for(int i=0;i<n;i++)r+=__builtin_va_arg(ap,CQ);__builtin_va_end(ap);return r;}
int tested(int n){unsigned long h=2166136261u;CQ a,b;__real__ a=n*1.5;__imag__ a=-(n+2)*0.25;__real__ b=(n&7)+1;__imag__ b=n%3-1;Q q=n*0.5+1;
CQ c1=a+b,c2=a-b,c3=a*b,c4=a/b,c5=-a,c6=~a,c7=a*q,c8=a/q,c9=q+a,c10=q-a,c11=q*a,c12=q/b,c13=a+q,c14=a-q;
FOLD(c1);FOLD(c2);FOLD(c3);FOLD(c4);FOLD(c5);FOLD(c6);FOLD(c7);FOLD(c8);FOLD(c9);FOLD(c10);FOLD(c11);FOLD(c12);FOLD(c13);FOLD(c14);
int cmp=(a==b)+(a!=b)*2+(a==a)*4+(c1!=c1)*8+!a*16+(q==a)*32+(b!=q)*64;if(a)h+=3;
_Complex double cd=a;_Complex float cf=b;_Complex long double cl=c3;_Complex int ci=b;double d=a;Q re=a;_Bool bo=c6;long l=b;
FOLD(cd);FOLD(cf);h=fold(h,&cl,10);h=fold(h,(char*)&cl+16,10);FOLD(ci);FOLD(d);FOLD(re);h+=bo*5+(unsigned long)l*7;
CQ fromd=cd,fromf=cf,froml=cl,fromi=ci,fromr=d,fromq=q,fromn=n,fromu=(unsigned)n*3000000000u;
FOLD(fromd);FOLD(fromf);h=fold(h,&froml,32);FOLD(fromi);FOLD(fromr);FOLD(fromq);FOLD(fromn);FOLD(fromu);
gc+=a;gc*=b;gc-=q;gc/=a;FOLD(gc);CQ u=a;u+=b;u*=2;u/=b;u-=1;FOLD(u);Q rq=n;rq+=b;rq*=a;FOLD(rq);double dd=2;dd+=a;h+=(unsigned long)(long)(dd*1000);
_Complex double ud=b;ud*=a;FOLD(ud);
FOLD(mul(a,b));FOLD(mix(n|1,a,0.5,q,b));FOLD(sum(3,a,b,c3));
return (int)(h%1000003)^cmp;}
