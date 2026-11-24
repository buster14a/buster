int *p; struct S { int a; } s;
void f(void) { (void)__builtin_fabs(p); (void)__builtin_fabsf(p); (void)__builtin_fabsl(s); (void)__builtin_canonicalize(p); (void)__builtin_signbitf(p); (void)__builtin_abs(p); (void)__builtin_labs(p); }
