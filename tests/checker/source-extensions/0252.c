int f(int *) __attribute__((__nonnull__(1)));
int g(int *p){return f(p);}
#pragma pack(1)
struct S{char c;int x;};
_Static_assert(sizeof(struct S)==5,"");
