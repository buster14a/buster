typedef _BitInt(5) S5;typedef _BitInt(13) S13;typedef _BitInt(40) S40;
#define U5 unsigned _BitInt(5)
#define U13 unsigned _BitInt(13)
#define U27 unsigned _BitInt(27)
#define U40 unsigned _BitInt(40)
static S5 table[3]={-3,7,-16};U5 u5(int n){return (U5)n;}S5 s5(int n){return (S5)(n%16);}typedef _BitInt(30) S30;typedef _BitInt(60) S60;
#define U30 unsigned _BitInt(30)
int tested(int n){S30 d=0;unsigned char*p=(unsigned char*)&d;p[0]=(unsigned char)n;p[3]=(unsigned char)(0x80|(n&0x7f));U30 e=0;unsigned char*q=(unsigned char*)&e;q[0]=(unsigned char)(n*3);q[3]=(unsigned char)(0xc0|(n&0x3f));S60 f=0;unsigned char*r=(unsigned char*)&f;r[0]=(unsigned char)(n*5);r[7]=(unsigned char)(0x90|(n&0xf));int x=(int)d;unsigned y=(unsigned)e;long z=(long)f;long g=(long)(S30)(n*0x10000000);unsigned long h=(unsigned long)(U30)((unsigned)n*0x50000000u);long k=(long)(S60)((long)n<<58);return (int)((x+(long)y*3+z%1000003*5+g*7+(long)(h%1000003)*11+k/(1L<<40)*13)%1000003);}