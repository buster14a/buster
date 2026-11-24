typedef float F; typedef int *P; typedef double D; struct S { int *m; }; int *h(void);
void f(int *p, double d, P q, _Complex float z, const int *cp, int a[2], struct S s, D e, const double cd, _Complex int ci) {
(void)(float)p; (void)(F)q; (void)(double)(p + 1); (void)(int *)d; (void)(P)1.5f; (void)(_Complex double)p; (void)(int*)z;
(void)(long double)cp; (void)(float)(void*)0; (void)(_Float16)p; (void)(float)f; (void)(float)"x"; (void)(float)a;
(void)(float)p++; (void)(float)s.m; (void)(float)h(); (void)(float)(d ? p : p); (void)(int*)e; (void)(int*)cd;
(void)(_Atomic float)p; (void)(_Complex int)p; (void)(int*)ci; (void)(char*)(float)p; }
