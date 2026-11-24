typedef int T;
typedef __attribute__((address_space(3))) int U;
__attribute__((address_space(1))) T *a;
const U *d;
__attribute__((address_space(1))) int * __attribute__((address_space(2))) const c;
int *q;
void f(int n) { if (n) q = a; else if (n > 1) q = d; else q = c; }
