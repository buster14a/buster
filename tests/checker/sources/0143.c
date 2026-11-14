constexpr int n = 2;
struct S { int a; };
int main(void) {
    constexpr int y = 1 + 1;
    constexpr double d = 2.5;
    constexpr int *p = 0;
    constexpr int *q = nullptr;
    constexpr char c = 'x';
    constexpr auto z = 3;
    constexpr struct S s = { 1 };
    int a[y];
    switch (y) { case n: break; default: break; }
    _Static_assert(y == 2, "an integer constant expression");
    _Static_assert(n == 2, "file scope");
    _Static_assert(z == 3, "with auto");
    return (int)sizeof a - 8 + (int)d + (p != 0) + (q != 0) + (c - 'x') + s.a - 1;
}
