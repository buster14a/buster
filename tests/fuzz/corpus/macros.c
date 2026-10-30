#define CAT(a, b) a ## b
#define ID(...) __VA_ARGS__
#define N 4
typedef struct { int a[N]; } CAT(Rec, ord);
int f(Record *p) { return ID(p->a[2]); }
