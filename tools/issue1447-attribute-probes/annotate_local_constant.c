int f(void) { const int n = 3; int x __attribute__((annotate("a", n))); return 0; }
