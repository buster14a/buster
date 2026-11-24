int a[];
int a[];
int b[]; int b[3];
static int c[];
extern int d[];
int e[], f[];
static void u(void) {}
typedef int A[];
A x;
int q[3]; int q[];
int r[]; int r[2] = { 1 };
int s[] = { 1, 2 }; int s[];
int t[]; extern int t[];
int *p[];
const int k[];
int m[][2];
void g(void) { extern int z[]; static int y[2]; (void)z; (void)y; }
#pragma clang diagnostic ignored "-Wtentative-definition-array"
int i[];
