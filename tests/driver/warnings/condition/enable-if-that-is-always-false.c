void z(int x) __attribute__((enable_if(0, "never")));
int h(void) { z(1); return 0; }
