#pragma clang diagnostic warning "-Wc++-compat"
void f(void) { const int a; }
#pragma clang diagnostic ignored "-Wc++-compat"
void g(void) { const int b; }
