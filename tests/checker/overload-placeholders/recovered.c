#define OV __attribute__((overloadable))
int f(void) OV;
int f(int) OV;
int g(int) OV;
int g(double) OV;
int t(int x) {
    int sum = f + g;
    sum += g + f;
    sum += x ? f : g;
    sum += x ? g : f;
    sum += f ? f : 0;
    sum += f ? 0 : g;
    return sum;
}
