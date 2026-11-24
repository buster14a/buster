#define MS __attribute__((ms_abi))
#define VC __attribute__((vectorcall))
#define PM __attribute__((preserve_most))
#define IO __attribute__((intel_ocl_bicc))
MS void a(void);
void b(void) MS;
void VC c(void);
PM void d(void);
IO void e(void);
typedef MS void T(void);
T *t;
void use(void)
{
    int x;
    x = a;
    x = b;
    x = c;
    x = d;
    x = e;
    x = t;
    x = &a;
}
