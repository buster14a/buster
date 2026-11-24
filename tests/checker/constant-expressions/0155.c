typedef int I; int f(void) { return _Generic(1,int:1,I:2); }
