void q(int x) __attribute__((enable_if(x > 0, "p1")));
void f(void) { void (*p)(int) = (q); (void)p; }
