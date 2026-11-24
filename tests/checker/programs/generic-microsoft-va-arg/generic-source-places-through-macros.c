#line 1 "macro-places.c"
#define COLUMN() __builtin_COLUMN()
#define LINE() __builtin_LINE()
#define WRAPPED() COLUMN()
#define OBJECT __builtin_COLUMN()
#define WRAPPEDARG(x) COLUMN()
#define ID(x) x
#define WHERE() (__builtin_LINE() * 1000 + __builtin_COLUMN())
static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
enum{S=COLUMN()*1000+WRAPPED(),T=OBJECT*1000+WRAPPEDARG(1),U=ID(LINE()
)};
int tested(int n){h=(unsigned)n;mix(S);mix(T);mix(U);mix(COLUMN());mix(WRAPPED());mix(OBJECT);mix(WRAPPEDARG(42));
mix(ID(__builtin_COLUMN()));mix(ID(COLUMN()));mix(LINE(
));mix(ID(
  __builtin_LINE()));mix(WHERE(
   ));
#line 300 "moved.c"
mix(WRAPPED());mix(LINE());mix(ID(WRAPPED()));mix(WHERE());mix(OBJECT);
return (int)(h^(h>>32));}
