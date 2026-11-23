int t1(void){return 1;}
#pragma weak a = t1
int t2(void){return 2;}
#pragma weak a = t2
int t3(void){return 3;}
#pragma weak a = t3
int a(void);
int u(void){return a();}
int (*p)(void) = a;
