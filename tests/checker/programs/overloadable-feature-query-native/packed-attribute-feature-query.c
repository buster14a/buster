#if __has_attribute(packed) && __has_attribute(__packed__)
#define PACKED __attribute__((packed))
#else
#define PACKED
#endif
#if __has_c_attribute(gnu::packed)
#define CPACKED [[gnu::packed]]
#else
#define CPACKED
#endif
struct PACKED A{char c;int i;short s;};struct CPACKED B{char c;int i;};enum PACKED E{X=1,Y=2};_Static_assert(sizeof(struct A)==7 && _Alignof(struct A)==1,"GNU packed query");_Static_assert(sizeof(struct B)==5 && _Alignof(struct B)==1,"scoped packed query");_Static_assert(sizeof(enum E)==1,"packed enum");int tested(int n){struct A a={1,n,3};struct B b={2,n};return sizeof(a)*100+sizeof(b)*10+sizeof(enum E)+a.i+b.i;}