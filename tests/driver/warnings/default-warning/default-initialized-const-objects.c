typedef const int CI;
typedef const volatile int CVI;
const int file_a;
static const int file_b;
extern const int file_c;
struct M { const int m; int x; };
struct N { struct M inner; int y; };
union U { const int u; float f; };
struct M file_m;
void f(void)
{
    const int a;
    const int b[2];
    const volatile int c;
    volatile CI d[2];
    CVI e;
    static const int s;
    extern const int ex;
    struct M m;
    struct N n;
    union U u;
    const struct M cm;
    struct M ms[2];
    const int ok = 1;
    register const int r;
    struct { const int z; } anon;
    const int *p;
    int *const q;
}
