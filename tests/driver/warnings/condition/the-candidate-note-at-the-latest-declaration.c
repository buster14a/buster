void r(int x) __attribute__((enable_if(x > 0, "a")));
void r(int x);
int h(void) { r(0); return 0; }
