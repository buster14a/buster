#define OV __attribute__((overloadable))
long h(int a) OV;
int h(int a, ...) OV;
int h(int a, int b) OV;
int (*p4)(int) = h;
void (*p5)(int) = h;
