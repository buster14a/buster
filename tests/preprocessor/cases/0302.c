#define ID(x) x
#if ID(defined Y)
yes
#else
no
#endif
#define D defined
#define X
#if D X
yes
#endif
#define DX defined(X)
#if DX
yes
#endif
