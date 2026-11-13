struct L { unsigned i; unsigned t; };
unsigned f(unsigned a) {
 const struct L r[] = { a, a + 1, 2, 3 };
 _Static_assert(sizeof r == 2 * sizeof(struct L), "two elements");
 return r[1].t; }
