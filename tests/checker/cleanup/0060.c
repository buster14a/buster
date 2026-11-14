void f(int *p);
int g(void) { { int a __attribute__((cleanup(f))) = 7;
asm goto("" : : : : in, out); in: a++; }
out: return 0; }
