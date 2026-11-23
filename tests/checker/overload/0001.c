#define O __attribute__((overloadable))
static void choose(...) O __attribute__((unavailable)); static int choose(int x) O{return x;} static double choose(double x) O{return x;} _Static_assert(_Generic(choose(3),int:1,default:0),"int"); _Static_assert(_Generic(choose(3.0),double:1,default:0),"double"); int run(void){return choose(7);}
