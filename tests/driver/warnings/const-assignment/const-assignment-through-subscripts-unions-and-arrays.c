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
void use(const int *const p, int i)
{
    p[0] = 1;
    i[p] = 2;
    struct an a = { 0 }, b = { 0 };
    a.x = 1;
    a = b;
    union un u = { 0 }, w = { 0 };
    u = w;
    u.f = 2;
    struct arr r = { 0 }, s = { 0 };
    r.ca[1] = 3;
    r = s;
    r.inner[0].x = 4;
    r.inner[1] = a;
}
