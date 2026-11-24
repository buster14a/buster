#define O __attribute__((overloadable))
int f(int) O __attribute__((unavailable)); double f(double) O; int take(int(*)(int)) O; double take(char*) O; _Static_assert(_Generic(take(_Generic(0,int:f,default:0)),int:1,default:0),"selected argument"); int g(void){return ((int(*)(int))f)(1);}
