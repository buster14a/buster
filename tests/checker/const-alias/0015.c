struct P { int x; };
struct P p = {};
enum { E = *(const int*)&p.x };
