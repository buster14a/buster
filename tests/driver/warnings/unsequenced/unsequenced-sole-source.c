#define STEP(v) ((v)++)
void pair(int, int);
int f(volatile int x, int y, int *p) {
    x = x + y; x += x; ++x; x--; (void)(x -= y); *p = x;
    if ((x = y)) { (void)STEP(x); }
    x = STEP(x);
    x += ++x;
    pair(STEP(x), x);
    return x += y;
}
