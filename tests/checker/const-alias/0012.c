struct P { int x; };
const struct P p = {};
const void* const v = &p.x;
const int* const q = v;
enum { E = *q };
