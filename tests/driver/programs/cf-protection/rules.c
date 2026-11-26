#include <setjmp.h>
static int s_called(int x) { return x + 1; }
static int s_designated(int x) { return x + 2; }
static int s_stored(int x) { return x + 3; }
static int s_compared(int x) { return x + 4; }
static int s_aliased(int x) { return x + 5; }
static int s_resolved(int x) { return x + 6; }
static void* s_resolver(void) { return (void*)s_resolved; }
static void s_constructor(void) __attribute__((constructor));
static void s_constructor(void) {}
static void s_destructor(void) __attribute__((destructor));
static void s_destructor(void) {}
static void s_used(void) __attribute__((used));
static void s_used(void) {}
static void s_cleanup(int* p) { (void)p; }
int (*stored)(int) = s_stored;
int f_alias(int) __attribute__((alias("s_aliased")));
int f_ifunc(int) __attribute__((ifunc("s_resolver")));
int f_external(int x) { return s_called(x) + (&s_designated)(x) + (*s_designated)(x); }
int f_compare(void* p) { return p == (void*)s_compared; }
int f_cleanup(void) { int v __attribute__((cleanup(s_cleanup))) = 0; return v; }
__attribute__((weak)) int f_weak(int x) { return x; }
jmp_buf buffer;
int f_setjmp(void) { if (setjmp(buffer)) return 1; return 0; }
int f_sigsetjmp(sigjmp_buf b) { return sigsetjmp(b, 1); }
__attribute__((returns_twice)) int twice(void);
int f_twice(void) { return twice() + 1; }
#ifndef NO_BUILTIN_SETJMP
void* builtin_buffer[5];
int f_builtin_setjmp(void) { if (__builtin_setjmp(builtin_buffer)) return 1; return 0; }
#endif
int f_switch(int x) { switch (x) { case 0: return 5; case 1: return 7; case 2: return 9; case 3: return 11; case 4: return 13; case 5: return 2; } return 0; }
int f_goto(int x) { static void* t[] = { &&a, &&b }; goto *t[x & 1]; a: return 1; b: return 2; }
int f_indirect(int (*p)(int)) { return p(3); }
__attribute__((naked)) void f_naked(void) { __asm__("ret"); }
struct frame;
__attribute__((interrupt)) void f_interrupt(struct frame* f) { (void)f; }
__attribute__((patchable_function_entry(2, 1))) int f_patched(int x) { return x; }
