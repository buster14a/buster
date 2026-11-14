struct P { char x; int y; };
const struct P p = {};
enum { E = *(const int*)((const char*)&p + 4) };
