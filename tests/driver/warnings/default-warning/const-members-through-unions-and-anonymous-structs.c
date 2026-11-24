struct M { const int m; };
union V { struct M s; int i; };
struct W { union V v; };
struct X { int a; struct { const int deep; }; };
void f(void) { union V v; struct W w; struct X x; }
