void spin(volatile int *p) { while (*p) __builtin_ia32_pause(); }
