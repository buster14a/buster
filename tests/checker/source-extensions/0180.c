typedef __builtin_va_list V; _Static_assert(sizeof(V)==24, "va_list"); void f(V p) { (void)p; }
