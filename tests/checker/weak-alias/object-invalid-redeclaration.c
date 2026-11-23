int t1 = 1;
extern int a;
#pragma weak a = t1
extern int a;
int u(void) { return a; }
