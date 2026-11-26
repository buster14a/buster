void* b[5];
void f(void) { __builtin_longjmp(b, 1); }
