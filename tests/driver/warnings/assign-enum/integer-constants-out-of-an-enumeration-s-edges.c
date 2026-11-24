enum __attribute__((enum_extensibility(open))) O { O1 } o;
enum __attribute__((enum_extensibility(closed))) C { C1 } cl;
typedef enum { U1, U2 } T;
enum N { NM = -1, N0, NB = 0x80000000u } n;
enum M { M1 = 1 } __attribute__((flag_enum)) m;
enum : unsigned char { X0 = 0, X1 = 1 } ux;
struct R { enum N f : 4; enum N g; } r;
void take(T, ...);
const enum N cn = 7;
void f(int x)
{
    o = 3; cl = 3; T t = 9; n = -1; n = 0x80000000u; n = 3; m = 1; m = ~1; m = 2;
    ux = 256; ux = 257; r.f = 5; r.g = 0; r.g = 2;
    take(5, 6);
    if (0) n = 9;
    unsigned long z = sizeof(n = 9);
    enum N arr[] = { [3] = 9, 1, 0 };
    struct R rr = { .g = 9 };
    T *pt = &(T){ 7 };
    n = (_Bool)1; n = (char)'a'; n = 0 ? 1 : 2; n = (1, 3);
    (void)t; (void)z; (void)arr; (void)rr; (void)pt;
}
