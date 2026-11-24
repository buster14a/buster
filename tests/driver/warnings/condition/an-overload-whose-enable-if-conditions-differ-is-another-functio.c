int n(int a) __attribute__((overloadable, enable_if(a > 0, "a")));
int n(int a) __attribute__((overloadable));
int n(int a) __attribute__((overloadable, enable_if(a > 0, "a")));
int n(int a) __attribute__((overloadable, enable_if(a > 0, "a"))) { return a; }
int h(int a) __attribute__((overloadable, enable_if(a > 0, "a")));
int h(int a) __attribute__((overloadable, enable_if(a < 10, "b")));
void t(void) { n(1); n(-1); h(30); h(-1); h(5); }
