extern int a;
#pragma weak a = t1
#pragma weak a = t2
int t2 = 2;
int t1 = 1;
int u(void) { return a; }
