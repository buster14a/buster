int printf(const char*, ...);
int f(void) { static int n = 5; return n++; }
extern int fn __asm__("f.n");
int main(void) { f(); printf("%d\n", fn); return 0; }
