#define O __attribute__((overloadable))
int f(int) O; double f(double) O; int take(int(*)(int)) O; double take(double(*)(double)) O;
void g(void){take(f);}
