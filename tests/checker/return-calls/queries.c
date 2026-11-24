// #1393: query builtins return. __builtin_constant_p's operand is visited, so a noreturn call in it ends the path;
// the object size queries' operands are left out.
_Noreturn void die(void);
int die_int(void) __attribute__((noreturn));
int a(int x) { if (x) return 1; __builtin_constant_p(x); }
int b(int x) { if (x) return 1; __builtin_constant_p(die_int()); }
int c(int *p) { if (p) return 1; __builtin_object_size(p, 0); }
int d(int *p) { if (p) return 1; __builtin_object_size((die(), p), 0); }
int e(int *p) { if (p) return 1; __builtin_dynamic_object_size(p, 0); }
int f(int x) { if (x) return 1; (void)__builtin_constant_p(x); }
int g(int x) { if (x) return 1; return __builtin_constant_p(die_int()) ? 1 : 2; }
int h(int x) { if (x) return 1; x = __builtin_constant_p(x) ? die_int() : 0; }
int assumed_false(void) { __builtin_assume(0); }
int one_assumption(int x) { if (x) __builtin_assume(0); }
