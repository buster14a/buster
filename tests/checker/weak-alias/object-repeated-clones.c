int t1 = 1;
int t2 = 2;
extern int a;
#pragma weak a = t1
int v(void) { return a; }
#pragma weak a = t2
int u(void) { return a; }
