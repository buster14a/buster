#define ONE 1
int g1(int a, long x) __attribute__((overloadable, enable_if(a > (1), ""), enable_if(a < 9, "")));
char g1(int b, long long y) __attribute__((overloadable, enable_if(b > 1, "")));
int g2(int a, long x) __attribute__((overloadable, enable_if(a > 1L, ""), enable_if(a < 9, "")));
char g2(int b, long long y) __attribute__((overloadable, enable_if(b > ONE, "")));
int g3(long a) __attribute__((overloadable, enable_if(a > 0, ""), enable_if(a < 9, "")));
char g3(unsigned long a) __attribute__((overloadable, enable_if(a > 0, "")));
int g4(int a) __attribute__((overloadable, enable_if(a > 0, ""), enable_if(a < 9, "")));
int g4(int a) __attribute__((overloadable, enable_if(a < 9, ""), enable_if(a > 0, "")));
void t(void) { g1(3, 0); g2(3, 0); g3(1); g4(4); g4(10); }
