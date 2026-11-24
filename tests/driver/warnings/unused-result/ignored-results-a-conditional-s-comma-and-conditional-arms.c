__attribute__((warn_unused_result)) int w(int);
void f(int n) { n ? (n, w(n)) : (n, w(n)); }
void f2(int n) { n ? (n ? w(n) : w(n)) : w(n); }
