void *f(void *p __attribute__((pass_object_size(0))), unsigned long n)
    __attribute__((diagnose_if(zz, "always", "warning")));
void *f2(void *p __attribute__((pass_object_size(0))), unsigned long n)
    __attribute__((diagnose_if(1, "always", "warning")));
void g(void) { char b[4]; f2(b, 8); }
