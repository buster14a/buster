void q2(int x) __attribute__((enable_if(x > 5, "p1")));
void q2(int x) __attribute__((enable_if(x > 1, "p2")));
int h(void) { q2(3); q2(1); return 0; }
