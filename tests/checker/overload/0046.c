__attribute__((overloadable())) int f(int); __attribute__((overloadable)) double f(double); _Static_assert(_Generic(f(1.0),double:1,default:0),"empty");
