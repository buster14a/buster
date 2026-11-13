void f(int *p);
int g(void) { void *p = &&far; { int x __attribute__((cleanup(f))) = 7;
asm goto("jmp %l0" : : : : in); in: return x; } far: return 0; }
