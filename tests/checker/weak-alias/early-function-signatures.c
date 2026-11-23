int t1(void){return 1;}
#pragma weak a = t1
int t2(int x){return x;}
#pragma weak a = t2
int a(int);
int u(void){return a() + a(3);}
