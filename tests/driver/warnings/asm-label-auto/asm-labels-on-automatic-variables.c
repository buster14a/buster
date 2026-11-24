int g asm("g1");
void a(void) { auto int c asm("c1"); int d asm("d1") = 2, e asm("e1"); (void)c; (void)d; (void)e; }
void b(void) { typedef int T asm("t1"); int f(void) asm("ff"); extern int x asm("xx"); static int s asm("ss"); register int r asm("eax"); (void)r; (void)s; }
void c(int k) { int vla[k] asm("v\x41"); (void)vla; { struct { int a; } st asm("st" "1"); (void)st; } }
void d(void) { int q __asm__("q1") __attribute__((unused)); int u asm("u1"), w asm("w1"); u = w = 1; }
