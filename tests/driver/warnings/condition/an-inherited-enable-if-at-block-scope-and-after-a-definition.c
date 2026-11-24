int r(int n) __attribute__((enable_if(n < 100, "small")));
void t1(void) { int r(int n); r(8); }
void t2(void) { r(8); }
void t3(void) { extern int r(int k); r(8); }
int r(int n) { return n; }
void t4(void) { r(8); }
