int f(void) { __typeof__((int[2]){0}) a = {1}; int *p = 0;
 a = p; return 0; }
