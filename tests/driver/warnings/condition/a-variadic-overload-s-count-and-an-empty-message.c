int v(int a, ...) __attribute__((overloadable, enable_if(a > 1, "")));
int v(int a, int b, int c) __attribute__((overloadable));
void t(void) { v(); v(2, 3); v(0, 1); }
