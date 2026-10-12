void spin(volatile int *p) { while (*p) { *p = *p; } }
