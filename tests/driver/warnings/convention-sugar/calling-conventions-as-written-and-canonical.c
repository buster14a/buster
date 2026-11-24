typedef void TI(void) __attribute__((intel_ocl_bicc));
typedef void TA(void) __attribute__((swiftasynccall));
void m(void) __attribute__((ms_abi));
void pm(void) __attribute__((preserve_most));
void pa(void) __attribute__((preserve_all));
void pn(void) __attribute__((preserve_none));
void io(void) __attribute__((intel_ocl_bicc));
void __regcall rc(void);
TI *ti;
TA *ta;
void use(void)
{
    int x;
    x = m;
    x = pm;
    x = pa;
    x = pn;
    x = io;
    x = rc;
    x = ti;
    x = ta;
}
