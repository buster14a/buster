struct P { int a, b; };
struct P p = { b: 1, a: 2 };
int g[4] = { [1 ... 2] = 3 };
int h[4] = { [1 ... 2] 3, [3] 4 };
struct P q[2] = { [1] { 1, 2 }, [0].a = 3, [0] .b = 4 };
int n[] = { [4] 1 };
int f(void)
{
    int e[3] = { [0] 2, 1, [2] 5.5 };
    struct P r = { a: 1.5 };
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-designator"
    int s[2] = { [1] 1 };
#pragma clang diagnostic pop
    return e[0] + r.a + s[1];
}
