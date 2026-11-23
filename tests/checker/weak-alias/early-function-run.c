int t1(void){return 1;}
#pragma weak a = t1
int t2(int x){return x + 10;}
#pragma weak a = t2
int a(int);
int u(void){return a() * 100 + a(3);}
int main(void){return u() == 1 * 100 + 1 ? 0 : 1;}
