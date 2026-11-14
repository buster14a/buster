void f(int *p);
int g(void) { { int a __attribute__((cleanup(f))) = 7;
asm goto("jmp %l0" : : : : out); }
out: return 1; }
