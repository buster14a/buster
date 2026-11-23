#define O __attribute__((overloadable))
void f(...) O __attribute__((unavailable)); int f(int) O; void g(void){f((void*)1);}
