#define TOI(p) ((int)(p))
typedef void *VP; typedef unsigned U32; typedef enum E { E0 } EE;
void *q;
int z = sizeof((char)&q);
_Static_assert(sizeof((short)&q) == 2, "");
void f(int *p, VP v, int a[2], const char *s, _BitInt(32) *bp, int n) {
    int x = (int)p << 40;
    int y = TOI(v);
    U32 u = (U32)v + (U32)a + (U32)"lit" + (unsigned char)s + (U32)&x;
    EE e = (EE)v; enum E e2 = (enum E)p;
    _Bool b = (_Bool)p; long l = (long)p; unsigned long long m = (unsigned long long)p;
    _BitInt(32) w = (_BitInt(32))p; __int128 h = (__int128)p; int c = (int)(int[2]){ 0 }; int d = (int)*&p;
    int vla[n]; int g = (char)vla + (short)bp;
    (void)x; (void)y; (void)u; (void)e; (void)e2; (void)b; (void)l; (void)m; (void)w; (void)h; (void)c; (void)d; (void)g;
    (void)sizeof((short)p); (void)_Generic((char)v, char: 0);
}
