void b(void) { for (int i asm("i1") = 0; i < 1; i++) {} }
void c(void) { for (int i __asm__("i2") __attribute__((unused)) = 0, j asm("j") = 1; i < j; i++) {} }
void d(void) { for (register int r asm("ebx") = 0; r < 1; r++) {} }
void e(void) { for (int *p asm("p1") = 0, (*q) asm("q1"), (k) asm("k1"); ;) { (void)p; (void)q; (void)k; break; } }
