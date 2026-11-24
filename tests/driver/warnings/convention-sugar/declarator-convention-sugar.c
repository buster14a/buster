#define MS __attribute__((ms_abi))
void f(void) __attribute__((preserve_most));
void (*p)(void) __attribute__((preserve_most));
void (**pp)(void) __attribute__((preserve_all));
void (*a[2])(void) __attribute__((preserve_none));
typedef void F(void) __attribute__((preserve_most));
F *fp;
void (*q)(void) __attribute__((ms_abi));
void (MS *r)(void);
void (* MS s)(void);
void (MS **t)(void);
MS void (*u)(void);
void (*v)(void) MS;
void g(void) __attribute__((sysv_abi));
void (*w)(void) __attribute__((sysv_abi));
void h(void)
{
    int x1 = f, x2 = p, x3 = pp, x4 = a, x5 = fp, x6 = q, x7 = r, x8 = s;
    int x9 = t, x10 = u, x11 = v, x12 = &f, x13 = g, x14 = w, x15 = &g, x16 = *pp;
}
