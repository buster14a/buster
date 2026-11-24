typedef _BitInt(5) S5;typedef _BitInt(13) S13;typedef _BitInt(40) S40;
#define U5 unsigned _BitInt(5)
#define U13 unsigned _BitInt(13)
#define U27 unsigned _BitInt(27)
#define U40 unsigned _BitInt(40)
static S5 table[3]={-3,7,-16};U5 u5(int n){return (U5)n;}S5 s5(int n){return (S5)(n%16);}int tested(int n){U5 a=u5(n*3),b=u5(n+29);U5 c=a+b,d=a*b,e=a-b,f=~a,g=a<<2,h=a>>1,i=a/(U5)(b|1u),j=a%(U5)(b|1u);S5 p=s5(n),q=s5(n+3);S5 r=-p,s=p&q,t=p^q,v=(S5)(p|q),k=p>>1;U13 big=(U13)(n*1000);big*=7;big<<=3;big-=(U13)n;big++;--big;big^=0x155;U27 m=(U27)n*1000003u;m+=(U27)0x7ffffff;U40 w=(U40)n*(U40)123456789;w+=w;w>>=3;S40 x=(S40)n*(S40)1000000;unsigned acc=(unsigned)c+3u*d+5u*e+7u*f+11u*g+13u*h+17u*(unsigned)(int)r+19u*(unsigned)(int)s+23u*i+29u*j;acc=acc*31u+(unsigned)(int)t+(unsigned)(int)v*3u+(unsigned)(int)k*5u+big*3u+m%1000u+(unsigned)(w%1000)+(unsigned)(x%997);return (int)(acc%1000003)+(a<b)+(p<q)*2+(big>4000)*4+(w>=x)*8+(a==0)*16+!p*32;}