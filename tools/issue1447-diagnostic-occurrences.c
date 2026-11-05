#if CASE == 1
int (*p) __attribute__((stdcall));
#elif CASE == 2
int (*(p)) __attribute__((stdcall));
#elif CASE == 3
int *(p) __attribute__((stdcall));
#elif CASE == 4
int ((p)) __attribute__((stdcall));
#elif CASE == 5
int (**p) __attribute__((stdcall));
#elif CASE == 6
int *(*p) __attribute__((stdcall));
#elif CASE == 7
int (*p[3]) __attribute__((stdcall));
#elif CASE == 8
int (*(p[3])) __attribute__((stdcall));
#elif CASE == 9
int (*p)[3] __attribute__((stdcall));
#elif CASE == 10
typedef int I;
I (*p) __attribute__((stdcall));
#elif CASE == 11
typedef int *P;
P (p) __attribute__((stdcall));
#elif CASE == 12
int ((*p)) __attribute__((stdcall));
#elif CASE == 13
int (* const p) __attribute__((stdcall));
#elif CASE == 14
int (* __attribute__((stdcall)) p);
#elif CASE == 15
int (__attribute__((stdcall)) *p);
#elif CASE == 16
int __attribute__((stdcall)) (*p);
#elif CASE == 17
int (__attribute__((stdcall)) *p) __attribute__((fastcall));
#elif CASE == 18
void (__attribute__((stdcall)) *p)(int q __attribute__((cold))) [[gnu::fastcall]];
#elif CASE == 19
__attribute__((noinline)) void ([[gnu::stdcall]] *p)(void);
#elif CASE == 20
void (__attribute__((stdcall)) *p)(void) [[gnu::fastcall]];
#elif CASE == 21
void __attribute__((stdcall)) ([[gnu::noinline]] *p)(void);
#elif CASE == 22
void ([[gnu::x]] *p)(void) __attribute__((noinline));
#elif CASE == 23
void f(int p [[gnu::cold]]) [[gnu::stdcall]];
#elif CASE == 24
[[__clang__::x]] void (__attribute__((stdcall)) *p)(int q [[__clang__::y]]) [[gnu::fastcall]];
#endif
