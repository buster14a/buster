void f(void); void g(void);
void h(int n) { n ? (f(), 0) : (g(), 0); }
