__attribute__((warn_unused_result)) int w(int);
void f(int n) { (n ? w(n) : w(n)), 0; }
void f2(int n) { 0, (n ? w(n) : w(n)); }
