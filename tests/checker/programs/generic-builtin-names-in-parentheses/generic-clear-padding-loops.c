void *memset(void *, int, unsigned long);static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
static void bytes(const void* p,unsigned long n){for(unsigned long i=0;i<n;i++)mix(((const unsigned char*)p)[i]);}
#define T(decl) do{decl;memset((void*)&v,0xff,sizeof v);__builtin_clear_padding(&v);bytes((const void*)&v,sizeof v);}while(0)
#define F(n) char a##n;int b##n;
#define F10(n) F(n##0)F(n##1)F(n##2)F(n##3)F(n##4)F(n##5)F(n##6)F(n##7)F(n##8)F(n##9)
struct P{char c;int i;};struct E{F10(1)F10(2)F10(3)F10(4)F10(5)};struct L{char c;struct E e[20];short t;};
struct N4{char c;struct P p[2][3][2][5];char d;};struct N6{char c;struct P p[2][2][2][2][2][3];char d;};
struct G2{char c;struct P p[30];short s;struct{char x;long y;}q[30];char z;};
int tested(int n){h=(unsigned)n;T(struct G2 v);T(struct N4 v);T(struct N6 v);T(struct L v);
T(struct G2 v[2]);T(struct N4 v[3]);T(struct N6 v[2]);T(struct L v[2]);return (int)(h^(h>>32));}
