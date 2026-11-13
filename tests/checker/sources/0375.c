struct L { unsigned i; unsigned t; };
struct L g(int);
unsigned f(int s, unsigned a, struct L x) {
 const struct L r[] = { s ? (struct L){ 0, a } : x, (struct L){ .i = 1, .t = a }, g(s), (x), s ? x : (struct L){ 4, a }, { 2, 3 } };
 _Static_assert(sizeof r == 6 * sizeof(struct L), "six elements");
 return r[0].t; }
