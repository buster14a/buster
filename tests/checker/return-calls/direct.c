// #1393: the direct-call cases of the issue, a falling path after an ordinary call, and the noreturn controls.
void v(void);
int gi(int);
void (*fp)(void);
_Noreturn void die(void);
__attribute__((noreturn)) void die2(void);
typedef void nr_t(void) __attribute__((noreturn));
nr_t *nfp;
void abort(void);
void exit(int);
int q1(int x) { if (x) return 1; v(); }
int q2(int x) { if (x) return 1; gi(x); }
int q3(int x) { if (x) return 1; fp(); }
int q4(int x) { if (x) return 1; x++; }
int q5(int x) { if (x) return 1; }
int q6(int x) { v(); }
int q7(int x) { if (x) return 1; v(); x++; }
int q8(int x) { if (x) { v(); return 1; } v(); }
int n1(int x) { if (x) return 1; die(); }
int n2(int x) { if (x) return 1; die2(); }
int n3(int x) { if (x) return 1; nfp(); }
int n4(int x) { if (x) return 1; abort(); }
int n5(int x) { if (x) return 1; exit(1); }
int n6(int x) { if (x) return 1; __builtin_trap(); }
int n7(int x) { if (x) return 1; __builtin_unreachable(); }
int n8(int x) { if (x) return 1; (*die)(); }
