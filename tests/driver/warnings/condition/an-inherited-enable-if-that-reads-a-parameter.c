int r(int n) __attribute__((enable_if(n < 100, "small")));
void t0(void) { r(8); }
int r(int n);
void t1(void) { r(8); }
int r(int m);
int r(int);
void t2(void) { r(9); }
int k(int n) __attribute__((overloadable, enable_if(n < 100, "small")));
int k(int n) __attribute__((overloadable, enable_if(n < 100, "small")));
void t3(void) { k(8); }
void w(int x) __attribute__((enable_if(0, "p1")));
void w(int x) __attribute__((enable_if(x < 10, "p2")));
void t4(void) { w(5); }
void w(int x);
void t5(void) { w(5); }
