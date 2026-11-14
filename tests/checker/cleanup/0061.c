void f(int *p);
int g(void) { asm goto("jmp %l0" : : : : in);
{ int a __attribute__((cleanup(f))) = 7;
in: return a; } }
