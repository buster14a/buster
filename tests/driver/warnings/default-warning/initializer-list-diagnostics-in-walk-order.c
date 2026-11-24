struct S { int n; int *p; };
struct S c[2] = { { 1, 2.5 }, { .n = 1, .n = 2 } };
struct S d = { 2.5, { 0, 3 } };
struct S e = { 1, { 2, 3 } };
struct S g = { 1, 2, 3 };
int *q = { 2, 3 };
int *r[2] = { 1, 2, 3 };
struct S h = { 1, { 2.5, 3 } };
void f(void) { struct S i = { 2.5, { 0, 3 } }; int x = 1.5; struct S j = { 3.5, 0, 1 }; struct S k = { 1, { 2, 3 } };
int *m = { 2, 3 }; (void)i; (void)x; (void)j; (void)k; (void)m; }
int t[2] = { 1.5, 2, 3 };
