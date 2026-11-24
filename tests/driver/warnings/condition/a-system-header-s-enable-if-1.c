# 1 "sys.h" 1 3
int w(int n) __attribute__((enable_if(1, "")));
# 3 "user.c" 2
int main(void){ int (*p)(int) = w; return w(1) + p(2); }
