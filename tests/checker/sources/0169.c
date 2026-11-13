int f(typeof(int (*)(struct Z *)) g) {
 struct Z *z = 0;
 return g(z); }
