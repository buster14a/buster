struct P { int x; };
const struct P p = {};
const float* const q = (const float*)&p.x;
enum { E = (int)*q };
