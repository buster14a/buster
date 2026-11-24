int f(void) { __typeof__((int){0} + (long){1}) x = 1; _Static_assert(sizeof x == sizeof(long), "conversion"); return x; }
