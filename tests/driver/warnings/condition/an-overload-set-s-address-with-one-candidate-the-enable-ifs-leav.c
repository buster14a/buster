long fc(char*);
int (*p)(int) = fc;
#define OV __attribute__((overloadable))
int f(int a) OV __attribute__((enable_if(a > 0, "pos")));
int f(double a) OV;
int (*p2)(int) = f;
int g(double a) OV;
int g(char a) OV;
int (*p3)(int) = g;
long h(double a) OV;
long h(char a) OV;
int (*p4)(int) = h;
