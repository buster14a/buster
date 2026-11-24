int *q;
unsigned long g(void) { return sizeof(({ *q; 1; })); }
unsigned long k(void) { return sizeof(({ 1 + 2; 1; })); }
