void take(int n, int *to);
void f(__attribute__((address_space(2))) int *p) { take(0, p); }
