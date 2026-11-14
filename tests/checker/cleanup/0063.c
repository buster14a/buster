int g(int n) { { int a[n]; a[0] = 1; asm goto("jmp %l0" : : : : out); }
out: return 1; }
