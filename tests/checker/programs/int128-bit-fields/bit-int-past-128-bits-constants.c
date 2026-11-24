typedef _BitInt(129) S129;typedef _BitInt(200) S200;typedef unsigned _BitInt(200) U200;typedef _BitInt(256) S256;
typedef unsigned _BitInt(300) U300;typedef unsigned _BitInt(1000) U1000;typedef _BitInt(4100) S4100;
static unsigned long fold(unsigned long h,const void*p,unsigned bytes){const unsigned char*b=(const unsigned char*)p;
for(unsigned i=0;i<bytes;i++)h=(h^b[i])*1099511628211ul;return h;}
#define FOLD(x) do{__typeof__(x) t_=(x);h=fold(h,&t_,sizeof t_);}while(0)
struct M{int a;S200 w;char c;U1000 u;};
S200 g1=5,g2=-7,g3=(S200)1<<199>>3,g4=-((S200)1<<150)/12345,g5=(S200)-123456789012345678901234567890wb%997;
U300 g6=0x123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123uwb,g7=~(U300)0/3,g8=(U300)1<<299,g9=0b1011wb;
S129 g10=340282366920938463463374607431768211455wb,g11=-340282366920938463463374607431768211455wb-1;
S256 g12=(S256)0777777777777777777777777777777777777777777777777777777777777wb*3,g13=1000000000000000000000000000000000000000000000wb;
U1000 g14=~(U1000)0>>17^(U1000)0xdeadbeefuwb<<900;S4100 g15=-((S4100)1<<4000)+(S4100)3;
struct M gm={1,-((S200)5<<100),'x',~(U1000)0/3};S256 ga[3]={1,(S256)1<<254>>1,-1};
double gd=(double)((S200)1<<150);float gf=(float)-((U300)1<<200);long double gl=(long double)(U300)0x123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123uwb;S200 gfd=(S200)-1.5e50,gfl=(S200)1e40L;
int gc[8]={(S200)3<(S200)4,(S200)-3<(S200)2,(U200)3<(U200)-1,(S200)1<<150==(S200)1<<150,!(S200)0,(S200)1<<140&&(S200)0,(S200)0||(S200)1<<190,(int)((U1000)1<<999>>990)};
enum{E1=(S200)1<<180>>178,E2=(int)(((U300)1<<299)>>295)};
long gn=(long)(-((S200)77<<130)>>130);unsigned gu=(unsigned)((U300)0x123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123uwb>>200);_Bool gb=(S200)1<<160;__int128 gq=(__int128)-((S256)5<<100);
int gs1=1<<(S200)30,gs2=(int)((S200)1<<(_BitInt(300))70>>(unsigned char)64);S200 gs3=(S200)-1>>(U300)150;
int tested(int n){unsigned long h=2166136261u;
static S256 s1=(S256)1<<255>>200;S200 l1=((S200)1<<150)+12345,l2=-(S200)3*(S200)1000000000000000000000000000000000000000wb;
S200 l3=l2/7,l4=l2%7,l5=(S200)1<<150?(S200)3:(S200)4;U1000 l6=~(U1000)0>>5;char arr[(int)((S200)1<<140>>137)];
FOLD(g1);FOLD(g2);FOLD(g3);FOLD(g4);FOLD(g5);FOLD(g6);FOLD(g7);FOLD(g8);FOLD(g9);FOLD(g10);FOLD(g11);FOLD(g12);FOLD(g13);FOLD(g14);FOLD(g15);
FOLD(gm);h=fold(h,ga,sizeof ga);FOLD(gd);FOLD(gf);h=fold(h,&gl,10);FOLD(gfd);FOLD(gfl);h=fold(h,gc,sizeof gc);FOLD(gn);FOLD(gu);FOLD(gb);FOLD(gq);FOLD(gs1);FOLD(gs2);FOLD(gs3);
FOLD(s1);FOLD(l1);FOLD(l2);FOLD(l3);FOLD(l4);FOLD(l5);FOLD(l6);h+=sizeof arr+E1*3+E2*5;
if((S200)0)h+=7;if((S200)1<<140)h+=11;S200 v=(S200)n<<150;FOLD(v+((S200)1<<160));FOLD(v<((S200)1<<151));
return (int)(h%1000003);}
