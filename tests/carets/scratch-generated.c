#define STR(x) #x
int a = STR(abc);
int b = __FILE__;
int counter(void) { return 1 / __COUNTER__; }
