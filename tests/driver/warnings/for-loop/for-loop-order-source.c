#define LOOP(v, n) for (int v = 0; v < (n); ) { }
#define STEP(v) v++
int g(int);
int f(int n, unsigned u) {
    int r = 0;
    for (int i = 0; i < n; ) {
        int unused;
        for (int j = 0; j < n; j++) { r += j < u; j++; }
        r == 1;
    }
    for (int i = 0; i < n; STEP(i)) { STEP(i); }
    for (int i = 0; i < n; i++) { r += g(i); i++; }
    for (int i = 0; i < n; ) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfor-loop-analysis"
        r++;
#pragma clang diagnostic pop
    }
    for (int i = 0; i < n; ) { for (int j = 0; j < i; ) { } }
    return r;
}
