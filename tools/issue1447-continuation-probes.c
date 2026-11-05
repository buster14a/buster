// Original diagnostic probes for issue #1447.
#if CASE == 1
[[__clang__::x, __clang__::y]] int a;
#elif CASE == 2
[[x, __clang__]] int a;
#elif CASE == 3
[[__clang__::noinline(1)]] void f(void);
#elif CASE == 4
[[x, __clang__::y, __clang__::z]] int a;
#elif CASE == 5
void f(int p [[gnu::cold]]) __attribute__((stdcall));
#elif CASE == 6
void f(void) [[gnu::stdcall]];
#elif CASE == 7
void ([[gnu::stdcall]] *p)(void);
#elif CASE == 8
int * const [[gnu::unused]] p;
#elif CASE == 9
int (*p) __attribute__((stdcall));
#elif CASE == 10
__attribute__((stdcall)) void f(int a __attribute__((cold)));
#elif CASE == 11
void (__attribute__((stdcall)) f)(int a __attribute__((cold)));
#elif CASE == 12
__attribute__((stdcall)) void (*f)(int a __attribute__((cold)));
#elif CASE == 13
[[gnu::annotate("tag", __clang__)]] int a;
#elif CASE == 14
int a[] = {1, __clang__};
#elif CASE == 15
int g(int,int); [[gnu::annotate("tag",g(1,__clang__))]] int a;
#elif CASE == 16
#define WRAP __clang__
[[WRAP::unused]] int a;
#elif CASE == 17
#undef __clang__
#define __clang__ 7
[[__clang__::unused]] int a;
#elif CASE == 18
typedef void F(void) [[gnu::stdcall]];
#elif CASE == 19
void f(void) [[gnu::stdcall, gnu::cold, x]];
#elif CASE == 20
_Noreturn void g(void);_Noreturn void f(int x){if(x)g();}
#elif CASE == 21
_Noreturn void g(void);int f(int x){if(x)g();}
#elif CASE == 22
void g(void);_Noreturn void f(int x){if(x)g();}
#elif CASE == 23
_Noreturn void g(void);int f(int x){if(x)g();else return 1;}
#elif CASE == 24
void f(int p [[__clang__::unused]]) __attribute__((stdcall));
#elif CASE == 25
[[__clang__::x]] int a [[__clang__::y]], b [[__clang__::z]];
#elif CASE == 26
[[__clang__::x]] void f(int a [[__clang__::y]]);
#elif CASE == 27
int (*f)(void) [[gnu::stdcall]];
#elif CASE == 28
void ([[gnu::stdcall, x]] *p)(void);
#elif CASE == 29
int * const [[__clang__::x]] p;
#endif
