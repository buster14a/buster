void q(int x) __attribute__((enable_if(x > 0, "p1")));
void f(void) { (void)sizeof(q); (void)_Alignof(q); __typeof__(q) *z = 0; (void)z; (q)(5); (&q)(5); }
