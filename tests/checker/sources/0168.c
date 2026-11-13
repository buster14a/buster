int f(void) {
 typeof(int (*)(struct Z *)) h = 0;
 struct Z *z = 0;
 return h(z); }
