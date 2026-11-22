#define OV __attribute__((overloadable))
int f(void) OV;
int f(int) OV;
int g(int) OV;
int g(double) OV;
int t(int x) {
    int sum = 0;
    sum += f + 1;
    sum += 2 * g;
    sum += (f) == 0;
    sum += &g != 0;
    sum += !f;
    sum += -g;
    sum += x ? 1 : 0;
    sum += f ? 1 : 2;
    sum += x && g;
    x += g;
    (void)f;
    sum += sizeof(f);
    sum += sizeof g;
    if (g) { sum += 1; }
    while (f) { break; }
    for (; g; ) { break; }
    switch (f) { default: break; }
    return sum;
}
