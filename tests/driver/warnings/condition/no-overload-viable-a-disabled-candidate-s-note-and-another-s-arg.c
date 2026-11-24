int g(int a) __attribute__((overloadable, enable_if(a > 0, "pos")));
int g(int a, int b) __attribute__((overloadable));
void t(int x) { g(1); g(-1); g(x); g(); g(1, 2, 3); }
