void q(int x) __attribute__((enable_if(x > 0, "p1"), enable_if(x > 1, "p2")));
int h(void) { q(0); return 0; }
