int g(int); int pf(void) __attribute__((pure)); int cf(int) __attribute__((const));
struct S { int m; } s, *ps;
void f(int i, int *p, volatile int v, __builtin_va_list ap, _Atomic int *ai) {
    (void)sizeof(i++); (void)sizeof(i = 1); (void)sizeof(g(i)); (void)sizeof(pf()); (void)sizeof(cf(i++));
    (void)sizeof(v); (void)sizeof(--i); (void)sizeof(i += 2); (void)sizeof i++; (void)sizeof (i++);
    (void)_Alignof(i++); (void)__alignof__(i <<= 1); (void)_Generic(i++, int: 1); (void)_Generic(i, int: 1);
    (void)sizeof(int[i++]); int a[i]; (void)sizeof(a[i++]); int b[i][i]; (void)sizeof(b[i++]);
    (void)sizeof(*p = 2); (void)sizeof(i += 2, 3); (void)sizeof(__builtin_constant_p(i++));
    (void)sizeof(i ? i++ : 0); (void)sizeof(i && i++); (void)sizeof(g(i = 1)); (void)sizeof(ps->m++); (void)sizeof(s.m = 1);
    (void)sizeof(p[i++]); (void)sizeof(_Generic(0, int: i++)); (void)sizeof(_Generic(i++, int: 1));
    (void)sizeof(__builtin_choose_expr(1, i++, 0)); (void)sizeof(__builtin_choose_expr(0, i++, 0));
    (void)sizeof((char){i++}); (void)sizeof(__extension__ (i++)); (void)sizeof(-i++); (void)sizeof((long)i++);
    (void)sizeof(__real__ i++); (void)sizeof(sizeof(i++));
    (void)sizeof(__c11_atomic_load(ai, 5)); (void)sizeof(__atomic_load_n(p, 5));
    (void)sizeof(__sync_fetch_and_add(p, 1)); (void)sizeof(__atomic_thread_fence(5), 0);
}
