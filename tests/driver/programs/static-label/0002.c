int printf(const char*, ...);
extern int fn __asm__("f.n");
int g(void) { return fn; }
int f(void) { static int n = 5; return n++; }
int main(void) { f(); printf("%d\n", g()); return 0; }
