__attribute__((no_builtin("memset"))) void f(void) {}
__attribute__((no_builtin)) void g(void) {}
__attribute__((no_builtin("memset", "memcpy"))) void h(void) {}
__attribute__((no_builtin("not_a_builtin"))) void k(void) {}
int x __attribute__((no_builtin("memset")));
