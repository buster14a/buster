int p1(int *p) __attribute__((overloadable, enable_if(p != 0, "nn")));
int p1(int *p, int n) __attribute__((overloadable));
int e1(int *p) __attribute__((enable_if(p != 0, "nn")));
int x, arr[4]; int *g;
void t(int *q) { p1(&x); p1(arr); p1(0); p1(q); p1(g); p1((int *)0); e1(&x); e1(q); e1(g); }
