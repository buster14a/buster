void *memset(void *, int, unsigned long);static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
struct S{char c;int i;};typedef struct S A[2];struct M{A a;struct S b[2];struct S (*p)[2];char t;};
static struct S g2[2];static struct S g23[2][3];static struct M m;
static struct S (*ret(void))[2]{return &g2;}static A *reta(void){return &g2;}static struct S *rets(void){return g2;}
static void bytes(const void* p,unsigned long n){for(unsigned long i=0;i<n;i++)mix(((const unsigned char*)p)[i]);}
#define T(arg,obj) do{memset((void*)&(obj),0xff,sizeof(obj));__builtin_clear_padding(arg);bytes((const void*)&(obj),sizeof(obj));}while(0)
int tested(int n){h=(unsigned)n;void* v=g2;void* w=g23;struct M* pm=&m;T((struct S (*)[2])v,g2);T((A *)v,g2);T((__typeof__(g2) *)v,g2);
T(&(*(struct S (*)[2][3])w)[1],g23);T(&((struct S (*)[2][3])w)[0][1],g23);T((struct S *)v,g2);T(&m.a,m);T(&m.b,m);T(&m.a[1],m);
T(&pm->a,m);T(&pm->b,m);m.p=&g2;T(m.p,g2);T(pm->p,g2);T(ret(),g2);T(reta(),g2);T(rets(),g2);T(&ret()[0][1],g2);
return (int)(h^(h>>32));}
