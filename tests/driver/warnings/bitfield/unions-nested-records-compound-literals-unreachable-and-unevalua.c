struct S { int b : 3; unsigned u : 3; };
union W { int b : 3; unsigned u : 2; };
struct N { struct S s; int k : 2; };
union W gw = { 9 };
struct N gn = { { .u = 8 }, 4 };
void f(void) {
    (void)sizeof((struct S){ .b = 9 });
    (void)(struct S){ .b = 9, .u = 9 };
    if (0) { struct S q = { 9 }; (void)q; }
    union W w = { .u = 4 };
    (void)w;
    return;
    struct S r = { 10 };
    (void)r;
}
