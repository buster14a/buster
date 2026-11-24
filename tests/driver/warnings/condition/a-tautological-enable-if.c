void k(int x) __attribute__((enable_if(1, "always")));
void f(void) { void (*p)(int) = k; k(2); (void)p; }
