void *f(void *p __attribute__((pass_object_size(0))), unsigned long n) __attribute__((enable_if(n < 100, "small")));
char gb[4];
int h(char *p __attribute__((pass_object_size(0))), int k) __attribute__((enable_if(k == 3, "three")));
int h2(int k, char *p __attribute__((pass_object_size(0)))) __attribute__((enable_if(k == 3, "three")));
void t(char *q) { char b[4]; f(0, 8); f(gb, 8); f(&gb[1], 8); f(q, 8); h(b, 3); h(gb, 3); h2(3, b); }
