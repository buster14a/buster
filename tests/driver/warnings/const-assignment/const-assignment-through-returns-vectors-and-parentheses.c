typedef const int CI;
typedef int I;
typedef const struct s0 { int v; } CS;
struct an { struct { const int x; int y; }; int z; };
union un { const int a; float f; };
struct arr { const int ca[2]; struct an inner[2]; };
struct deep { struct an an; union un un; CI ci; const I cti; };
static const int *const g(void);
extern __attribute__((unused)) CS *h(void);
typedef float v4f __attribute__((ext_vector_type(4)));
struct vec { const v4f v; };
void use(void)
{
    struct an a = { 0 }, b = { 0 };
    struct deep d = { 0 }, e = { 0 };
    d = e;
    d.ci = 1;
    d.cti = 2;
    *g() = 1;
    h()->v = 5;
    struct vec vv = { 0 };
    vv.v.x = 1;
    vv.v[0] = 2;
    a.y += 1;
    (a).x = 1;
    (a) = b;
    (&a)->x = 2;
    *(&a) = b;
}
