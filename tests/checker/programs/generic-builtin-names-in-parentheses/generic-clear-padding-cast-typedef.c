void *memset(void *, int, unsigned long);static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
struct S{char c;int i;};typedef struct S R[3];typedef R T[2];typedef struct S U;static struct S g[4][2][3];
static void bytes(const void* p,unsigned long n){for(unsigned long i=0;i<n;i++)mix(((const unsigned char*)p)[i]);}
#define C(arg) do{memset((void*)g,0xff,sizeof(g));__builtin_clear_padding(arg);bytes((const void*)g,sizeof(g));}while(0)
int tested(int n){h=(unsigned)n;void* p=g;C(&(*(R (*)[2])p)[1]);C(&(*(R (*)[2])p)[1][2]);C(&(*(T (*)[2])p)[1]);
C(&(*(T (*)[2])p)[1][1]);C(&(*(T (*)[2])p)[1][1][2]);C(&(*(volatile R (*)[2])p)[0]);C(&((R (*)[2])p)[1][0]);
C(&(*(U (*)[2][3])p)[1]);C(&(*(struct S (*)[2][3])p)[1]);C(&((R *)p)[1]);R (*q)[2]=p;C(&(*q)[1]);
return (int)(h^(h>>32));}
