struct P { volatile int x; };
const struct P p = {};
enum { E = *(const int*)&p.x };
