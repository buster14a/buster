int (*f(struct T *p))(struct U *u) {
 struct T *q = p;
 struct U *v = 0;
 int (*k)(struct U *) = f(q);
 return k(v); }
