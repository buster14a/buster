void q(int x) __attribute__((enable_if(1, "a"), enable_if(0, "b")));
void q(int x);
void h(void) { q(1); void (*p)(int) = q; (void)p; }
void w(int x) __attribute__((enable_if(0, "p1")));
void w(int x) __attribute__((enable_if(x < 10, "p2")));
void hw(void) { w(5); }
