#define O __attribute__((overloadable))
int f(int,long,long) O; double f(long,int,int) O;
void g(void){f(1,1,1);}
