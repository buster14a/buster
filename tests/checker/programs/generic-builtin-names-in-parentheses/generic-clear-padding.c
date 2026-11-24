void *memset(void *, int, unsigned long);static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
struct A{char c;int i;short s;};struct B{int x:3;int:4;unsigned y:9;char z;long double ld;};union U{char c;int i;struct{char a;short b;}s;};
struct K{char k[3];};struct C{struct A a[2];_Complex long double z;_Atomic(struct K) at;_BitInt(37) bi;};typedef int V __attribute__((ext_vector_type(3)));
struct D{V v;char tail;};struct E{char c;struct{char d;long e;};};struct F{char c;int a[2][2];struct{char p:1;unsigned long q:50;}bits[2];};
static void bytes(const void* p,unsigned long n){for(unsigned long i=0;i<n;i++)mix(((const unsigned char*)p)[i]);}
#define T(decl) do{decl;memset((void*)&v,0xff,sizeof v);__builtin_clear_padding(&v);bytes((const void*)&v,sizeof v);}while(0)
static struct A big[300];struct G{char c[5000];int i;char d;};struct H{char c;struct A a[64];short t:3;};struct I{struct H h[3];long double x[40];};
int tested(int n){h=(unsigned)n;T(struct A v);T(struct B v);T(union U v);T(struct C v);T(struct D v);T(struct E v);T(long double v);T(struct F v);
T(struct A v[3]);T(struct A v[2][3]);T(struct G v);T(struct H v);T(struct I v);T(struct I v[2]);T(volatile struct A v);T(_Complex long double v);memset(big,0xff,sizeof big);__builtin_clear_padding(&big);bytes(big,sizeof big);
int k=(n&3)+1;int vla[k];memset(vla,0xff,sizeof vla);__builtin_clear_padding(&vla);bytes(vla,sizeof vla);return (int)(h^(h>>32));}
