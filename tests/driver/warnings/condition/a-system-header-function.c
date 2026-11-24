# 1 "sys.h" 1 3
static inline int header_wrapper(int n) __attribute__((diagnose_if((n>8),("too large"),"warning"))){return n;}
int opaque(char *p, int n) __attribute__((diagnose_if(__builtin_object_size(p, 1) < n, "small", "warning")));
# 4 "user.c" 2
int main(void){ char b[4]; return header_wrapper(3) + header_wrapper(9) + opaque(b, 2); }
