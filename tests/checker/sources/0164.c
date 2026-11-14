int f(struct T *p, int (*k)(struct U *)) {
 struct U *v = 0; return k(v) + (p != 0); }
