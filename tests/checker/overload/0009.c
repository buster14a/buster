#define O __attribute__((overloadable))
int f(int) O; double f(double) O; void g(void){{int f(int) O;} {int f(int) O; _Static_assert(_Generic(f(1.0),int:1,default:0),"sibling");} _Static_assert(_Generic(f(1.0),double:1,default:0),"restored");}
