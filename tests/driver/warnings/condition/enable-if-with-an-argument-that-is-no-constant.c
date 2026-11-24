void f(int x) __attribute__((enable_if(x > 0, "positive")));
int h(int n) { f(n); return 0; }
