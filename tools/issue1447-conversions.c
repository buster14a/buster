#if CASE == 1
const int b = 1;
int *c = &b;
#elif CASE == 2
int f(void) { const int b=1; int *c=&b; return *c; }
#elif CASE == 3
void f(const int *a, int *b) { b=a; }
#elif CASE == 4
const int b=1;
int *f(void) { return &b; }
#elif CASE == 5
typedef const int CI;
CI b=1;
typedef int *P;
P c=&b;
#elif CASE == 6
const int b[2]={1,2};
int *c=b;
#elif CASE == 7
const int b=1;
int *const c=&b;
#elif CASE == 8
void f(const int *const a) { int *const b=a; (void)b; }
#elif CASE == 9
int *f(const int *const a) { return a; }
#elif CASE == 10
void f(int *a, int *b);
void g(const int *a) { f(a,a); }
#elif CASE == 11
unsigned char a[2];
char *b=a;
char *f(void) { return a; }
#elif CASE == 12
int a=1.5;
int f(void) { int b=2.5; return b; }
#elif CASE == 13
int f(void) { return -1.5; }
void g(int);
void h(void) { g(2.5); }
#elif CASE == 14
void f(void) { int a; a=1.5; }
#elif CASE == 15
int a=1.1f;
int b=1.1;
int c=1.1L;
#elif CASE == 16
_Bool a=1.5;
int b=1.0;
int c=2.5-1.0;
int d=2.5+0.0;
#elif CASE == 17
int a=(double)1.5f;
int b=(int)1.5;
int c=((1.5));
#elif CASE == 18
#define VALUE 1.5
int a=VALUE;
#elif CASE == 19
unsigned char a=1000.5;
unsigned b=-1.5;
int c=1e30;
#elif CASE == 20
int a=__builtin_nan("");
int b=__builtin_inf();
#elif CASE == 21
int values[2]={1.5,2.5};
struct S { int n; } object={1.5};
#elif CASE == 22
int f(double a) { return a; }
int g(void) { double a=1.5; return a; }
#elif CASE == 23
int a=0.9999999999999999;
int b=1e-30;
int c=0x1.8p0;
#elif CASE == 24
int f(void) { return sizeof((int){1.5}); }
int g(void) { if(0) return 1.5; return 0; }
#endif
