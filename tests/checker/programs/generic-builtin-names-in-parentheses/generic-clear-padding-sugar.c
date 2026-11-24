void *memset(void *, int, unsigned long);static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
struct S{char c;int i;};typedef struct S A[2];typedef struct S TS;typedef struct S (*PA)[2];typedef A *PTA;struct M{A a;char z;int y;};
struct Q{struct M2{A a;char z;}m;int k;};
static void bytes(const void* p,unsigned long n){for(unsigned long i=0;i<n;i++)mix(((const unsigned char*)p)[i]);}
#define T(decl,arg) do{decl;memset((void*)&v,0xff,sizeof v);__builtin_clear_padding(arg);bytes((const void*)&v,sizeof v);}while(0)
static void param(struct S (*p)[2],struct S b[2][2],A *q){__builtin_clear_padding(p);__builtin_clear_padding(b);__builtin_clear_padding(q);}
int tested(int n){h=(unsigned)n;T(struct S v[2],&v);T(A v,&v);T(TS v[2],&v);T(struct S v[2];struct S (*p)[2]=&v,p);T(struct S v[2];__typeof__(v) *q=&v,q);
T(struct S v[2];__typeof__(struct S[2]) *q=&v,q);T(struct M v,&v);T(struct Q v,&v);T(A v[2],&v);T(A v[3],&v[1]);T(struct S v[3][2],&v[1]);
T(struct S v[2];PA p=&v,p);T(A v;PTA p=&v,p);T(struct S v[2];struct S (*p)[2]=&v;struct S (**pp)[2]=&p,*pp);T(struct S v[2];struct S ((*p))[2]=&v,p);
struct S b[2];struct S d[2][2];A a;memset(b,0xff,sizeof b);memset(d,0xff,sizeof d);memset(a,0xff,sizeof a);param(&b,d,&a);bytes(b,sizeof b);bytes(d,sizeof d);bytes(a,sizeof a);
return (int)(h^(h>>32));}
