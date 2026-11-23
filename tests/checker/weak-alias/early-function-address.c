int t1(void){return 1;}
#pragma weak a = t1
int t2(void){return 2;}
#pragma weak a = t2
int a(void);
int (*p)(void) = a;
void *q = a;
