int c1(int x, volatile int a) __attribute__((enable_if(x && a, "")));
int c2(int x, volatile int a) __attribute__((enable_if(x || a, "")));
int c3(int x, volatile int a) __attribute__((enable_if(x ? a : 1, "")));
int c4(volatile int a) __attribute__((enable_if(sizeof(a) == 4, "")));
int c5(volatile int *a) __attribute__((enable_if(a != 0, "")));
void g(int* p) { c1(1, 2); c1(0, 2); c2(0, 3); c2(1, 3); c3(0, 1); c4(1); c5(p); }
