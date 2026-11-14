void f(int *p);
void g(int n) { switch (n) { case 1: { int a __attribute__((cleanup(f))); break; } case 2:; } }
