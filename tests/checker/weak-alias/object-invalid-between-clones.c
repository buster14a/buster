int t1 = 1;
int t2 = 2;
int t3 = 3;
extern int a;
#pragma weak a = t1
extern int a;
#pragma weak a = t2
extern int a;
extern int a;
#pragma weak a = t3
extern int a;
int u(void) { return a; }
