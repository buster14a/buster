struct S { int a : 1; signed char c : 2; unsigned u : 3; int b : 3; int w; char z; };
struct S g1 = { 1, 200, 9, 1 + 8, 1.5, 300 };
struct S g2 = { .b = -5, .u = ~0u, .c = (char)200 };
int f(int x) {
    struct S t = { 1, 200, 9, 1 + 8, 0, 1.5 };
    struct S v[2] = { [1].b = 9, [0] = { .u = -9 } };
    struct S y = { x, 9, 9, x / 0, 300 };
    return t.a + v[0].b + y.w;
}
