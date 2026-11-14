struct P { char x; int y; };
const struct P p = {};
enum { E = *((const unsigned char*)&p + 1) };
