void f(int x) { l: x++; int y = undeclared; }
void g(int x) { l: x++; }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-label"
void k(void) { q: ; }
#pragma clang diagnostic pop
void k2(void) { q: ; }
