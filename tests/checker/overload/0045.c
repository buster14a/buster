int f(int) __attribute__((unused, overloadable)); double f(double) __attribute__((__overloadable__)); _Static_assert(_Generic(f(1.0),double:1,default:0),"listed"); int run(void){return f(2);}
