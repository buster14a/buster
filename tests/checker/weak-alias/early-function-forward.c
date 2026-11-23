#pragma weak a = t1
#pragma weak a = t2
int t1(void){return 1;}
int t2(void){return 2;}
int a(void);
int u(void){return a();}
