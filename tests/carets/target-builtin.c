// A builtin that needs a target feature (#2094).
int b(void) { unsigned u; return __builtin_ia32_rdrand32_step(&u); }
