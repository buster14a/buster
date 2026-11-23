int t1=1;
int t2=2;
#pragma weak a=t1
#pragma weak a=t2
extern int a;
