int g(int n) { asm goto("jmp %l0" : : : : in);
{ int a[n]; a[0] = 1;
in: return a[0]; } }
