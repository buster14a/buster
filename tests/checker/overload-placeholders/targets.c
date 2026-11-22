#define OV __attribute__((overloadable))
int f(void) OV;
int f(int) OV;
int g(int) OV;
int g(double) OV;
typedef void *opaque;
struct record { int x; };
void *file_address = &g;
const void *file_constant = f;
void t(void *p, opaque o, struct record r) {
    void *local = g;
    opaque typed = f;
    p = f;
    p = &g;
    o = g;
    r = g;
    (void)local;
    (void)typed;
}
