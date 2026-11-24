struct S { int m[3]; const char *name; };
int a[3];
const int ca[2] = { 1, 2 };
int grid[2][3];
struct S s;
int *gp = a;
const int *gk = a;
const char *gs = "B";
const char *gq[2] = { "B", "C" };
int (*gr)[3] = grid;
const int *gc = ca;
int *gm = s.m;
int *gl = (int[]){ 1, 2 };
struct S gt = { { 1, 2, 3 }, "name" };
void take(const char *text, int *values, int (*rows)[3], const int *fixed);
int *chosen(void) { return a; }
const char *named(void) { return "x"; }
int (*rows(void))[3] { return grid; }
int *member(struct S *p) { return p->m; }
const int *fixed(void) { return ca; }
void body(int n, struct S *p)
{
    int vla[n];
    int local[4];
    int *v = vla;
    int *l = local;
    const char *t[2] = { "B", "C" };
    struct S u = { { 0 }, "u" };
    int *w[3] = { local, p->m, vla };
    int *c = (int[]){ 1, 2 };
    int (*d[1])[3] = { grid };
    void (*fn[1])(int, struct S *) = { body };
    const int *k;
    int (*r)[3];
    k = ca;
    r = grid;
    v = p->m;
    l = (int[2]){ 3, 4 };
    t[0] = "D";
    take("x", a, grid, ca);
    take(t[1], p->m, grid, (const int[]){ 5 });
    take(gs, vla, r, k);
    (void)v; (void)l; (void)u; (void)w; (void)c; (void)d; (void)fn;
}
