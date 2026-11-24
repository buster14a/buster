void f(int x) __attribute__((enable_if(x > 0, "positive")));
int h(void) { f(1); f(0); return 0; }
