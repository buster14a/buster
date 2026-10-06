// C17 6.7.9p17: after a designator, positional initializers continue with the
// member after the designated one in the innermost aggregate that contains
// it. When the designator names a member promoted out of anonymous
// structs/unions, that aggregate is the anonymous one, not the root record.
// Static initialization resumed at the root's next member (`{ .x = 6, 7 }`
// stored 7 in `b`, not in the anonymous union's first member); an inferred
// array size was counted the same way. Each shape is checked at static
// storage, at automatic storage and through an incomplete array, with a
// value that reaches the bytes at run time.

struct S
{
    int a;
    struct
    {
        int x;
        union
        {
            int u;
            float f;
        };
    };
    int b;
};

struct T
{
    int a;
    struct
    {
        struct
        {
            int deep;
            int e;
        };
    };
    int z;
};

// A union as the anonymous member: the designated member fills the union, so
// the next positional initializer leaves it.
struct U
{
    int a;
    union
    {
        struct
        {
            int p;
            int q;
        };
        int w;
    };
    int c;
    int d;
};

struct V
{
    int a;
    struct
    {
        int m;
        struct
        {
            int n;
            int o;
        };
        int r;
    };
    int z;
};

struct W
{
    struct S s;
    int k;
};

static struct S s_x = {.x = 6, 7};
static struct S s_u = {.u = 7, 8};
static struct S s_f = {.f = 1.0f, 8};
static struct S s_after = {.a = 1, .x = 6, 7, 9};
static struct T t_deep = {.deep = 10, 11, 12};
static struct T t_last = {.e = 1, 2};
static struct U u_p = {.p = 1, 2, 3, 4};
static struct U u_q = {.q = 1, 2, 3};
static struct U u_w = {.w = 1, 2, 3};
static struct V v_n = {.n = 1, 2, 3, 4};
static struct V v_o = {.o = 1, 2, 3};
static struct V v_m = {.m = 1, 2, 3, 4, 5};
static struct W w_braced = {.s = {.x = 6, 7}, 9};
static struct W w_chain = {.s.x = 6, 7, 8, 9};
static struct S s_array[] = {{.x = 6, 7}, {.x = 1, 2, 3}};
static struct S s_sized[] = {[1].x = 6, 7, 8};

static int fail_count;

static void check(int condition)
{
    fail_count += !condition;
}

static int automatic(int n)
{
    struct S s_x = {.x = n, n + 1};
    struct S s_u = {.u = n, n + 1};
    struct S s_after = {.a = 1, .x = n, n + 1, n + 3};
    struct T t_deep = {.deep = n, n + 1, n + 2};
    struct T t_last = {.e = n, n + 1};
    struct U u_p = {.p = n, n + 1, n + 2, n + 3};
    struct U u_q = {.q = n, n + 1, n + 2};
    struct U u_w = {.w = n, n + 1, n + 2};
    struct V v_n = {.n = n, n + 1, n + 2, n + 3};
    struct V v_o = {.o = n, n + 1, n + 2};
    struct V v_m = {.m = n, n + 1, n + 2, n + 3, n + 4};
    struct W w_braced = {.s = {.x = n, n + 1}, n + 2};
    struct W w_chain = {.s.x = n, n + 1, n + 2, n + 3};
    struct S s_sized[] = {[1].x = n, n + 1, n + 2};
    struct T* literal = &(struct T){.deep = n, n + 1, n + 2};

    check(s_x.a == 0 && s_x.x == n && s_x.u == n + 1 && s_x.b == 0);
    check(s_u.x == 0 && s_u.u == n && s_u.b == n + 1);
    check(s_after.a == 1 && s_after.x == n && s_after.u == n + 1 && s_after.b == n + 3);
    check(t_deep.a == 0 && t_deep.deep == n && t_deep.e == n + 1 && t_deep.z == n + 2);
    check(t_last.deep == 0 && t_last.e == n && t_last.z == n + 1);
    check(u_p.a == 0 && u_p.p == n && u_p.q == n + 1 && u_p.c == n + 2 && u_p.d == n + 3);
    check(u_q.p == 0 && u_q.q == n && u_q.c == n + 1 && u_q.d == n + 2);
    check(u_w.w == n && u_w.c == n + 1 && u_w.d == n + 2);
    check(v_n.m == 0 && v_n.n == n && v_n.o == n + 1 && v_n.r == n + 2 && v_n.z == n + 3);
    check(v_o.m == 0 && v_o.n == 0 && v_o.o == n && v_o.r == n + 1 && v_o.z == n + 2);
    check(v_m.m == n && v_m.n == n + 1 && v_m.o == n + 2 && v_m.r == n + 3 && v_m.z == n + 4);
    check(w_braced.s.x == n && w_braced.s.u == n + 1 && w_braced.s.b == 0 && w_braced.k == n + 2);
    check(w_chain.s.x == n && w_chain.s.u == n + 1 && w_chain.s.b == n + 2 && w_chain.k == n + 3);
    check(sizeof s_sized / sizeof s_sized[0] == 2 && s_sized[1].x == n && s_sized[1].u == n + 1 && s_sized[1].b == n + 2);
    check(literal->a == 0 && literal->deep == n && literal->e == n + 1 && literal->z == n + 2);
    return fail_count;
}

int main(void)
{
    check(s_x.a == 0 && s_x.x == 6 && s_x.u == 7 && s_x.b == 0);
    check(s_u.x == 0 && s_u.u == 7 && s_u.b == 8);
    check(s_f.x == 0 && s_f.f == 1.0f && s_f.b == 8);
    check(s_after.a == 1 && s_after.x == 6 && s_after.u == 7 && s_after.b == 9);
    check(t_deep.a == 0 && t_deep.deep == 10 && t_deep.e == 11 && t_deep.z == 12);
    check(t_last.deep == 0 && t_last.e == 1 && t_last.z == 2);
    check(u_p.a == 0 && u_p.p == 1 && u_p.q == 2 && u_p.c == 3 && u_p.d == 4);
    check(u_q.p == 0 && u_q.q == 1 && u_q.c == 2 && u_q.d == 3);
    check(u_w.w == 1 && u_w.c == 2 && u_w.d == 3);
    check(v_n.m == 0 && v_n.n == 1 && v_n.o == 2 && v_n.r == 3 && v_n.z == 4);
    check(v_o.m == 0 && v_o.n == 0 && v_o.o == 1 && v_o.r == 2 && v_o.z == 3);
    check(v_m.m == 1 && v_m.n == 2 && v_m.o == 3 && v_m.r == 4 && v_m.z == 5);
    check(w_braced.s.x == 6 && w_braced.s.u == 7 && w_braced.s.b == 0 && w_braced.k == 9);
    check(w_chain.s.x == 6 && w_chain.s.u == 7 && w_chain.s.b == 8 && w_chain.k == 9);
    check(sizeof s_array / sizeof s_array[0] == 2 && s_array[0].u == 7 && s_array[0].b == 0 && s_array[1].u == 2 && s_array[1].b == 3);
    check(sizeof s_sized / sizeof s_sized[0] == 2 && s_sized[1].x == 6 && s_sized[1].u == 7 && s_sized[1].b == 8);
    return automatic(5) != 0;
}
