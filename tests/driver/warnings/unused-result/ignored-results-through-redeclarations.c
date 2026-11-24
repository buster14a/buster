int h(int);
int h(int) __attribute__((const));
int k(int) __attribute__((pure));
int k(int);
int m(int);
void before(int n) { m(n); }
int m(int) __attribute__((warn_unused_result));
void after(int n) { h(n); k(n); m(n); }
