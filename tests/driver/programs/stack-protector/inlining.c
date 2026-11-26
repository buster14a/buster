void use(void*);
static inline __attribute__((always_inline)) int inlined(void) { char b[16]; use(b); return b[0]; }
__attribute__((no_stack_protector)) int f_exempt_caller(void) { return inlined(); }
int f_caller(int x) { return inlined() + x; }
int f_plain_caller(int x) { return x * 2; }
