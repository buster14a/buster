// Original diagnostic probes for issue #1447; compile one CASE at a time.
#if CASE == 1
const const int x;
#elif CASE == 2
volatile volatile int x;
#elif CASE == 3
_Atomic _Atomic int x;
#elif CASE == 4
inline inline void f(void) {}
#elif CASE == 5
_Noreturn _Noreturn void f(void) { for (;;) {} }
#elif CASE == 6
extern extern int x;
#elif CASE == 7
static static int x;
#elif CASE == 8
typedef typedef int T;
#elif CASE == 9
const int f(const const int x) { return x; }
#elif CASE == 10
_Noreturn void f(void) {}
#elif CASE == 11
_Noreturn void f(void) { return; }
#elif CASE == 12
__attribute__((noreturn)) int f(void) { return 1; }
#elif CASE == 13
__attribute__((noreturn)) void f(int n) { if (n) for (;;) {} }
#elif CASE == 14
int f(void) {}
#elif CASE == 15
int f(int n) { if (n) return 1; }
#elif CASE == 16
void g(void); int f(void) { g(); }
#elif CASE == 17
_Noreturn void g(void); _Noreturn void f(void) { g(); }
#elif CASE == 18
__private_extern__ int x;
#elif CASE == 19
__private_extern__ int x = 7;
#elif CASE == 20
__private_extern__ int f(void) { return 1; }
#elif CASE == 21
void f(void) { __private_extern__ int x; }
#elif CASE == 22
__private_extern__ static int x;
#elif CASE == 23
__attribute__((loader_uninitialized)) struct S x;
#elif CASE == 24
__attribute__((loader_uninitialized)) int x;
int x = 1;
#elif CASE == 25
__attribute__((loader_uninitialized,section(".my_loader"))) int x;
#elif CASE == 26
__attribute__((loader_uninitialized,section(".my_loader"))) const int x;
#elif CASE == 27
void f(void) __attribute__((regparm())) {}
#elif CASE == 28
void f(void) __attribute__((pcs())) {}
#elif CASE == 29
int x; void f(void) { __attribute__(()) x = 1; }
#elif CASE == 30
int x; __attribute__(()) x = 1;
#elif CASE == 31
struct S; struct S x;
#elif CASE == 32
int x = 1; int x = 2;
#elif CASE == 33
unsigned unsigned int x;
#elif CASE == 34
long long long int x;
#elif CASE == 35
__attribute__((noreturn)) void f(void) { if (0) return; for (;;) {} }
#elif CASE == 36
void g(void); __attribute__((noreturn)) void f(void) { g(); }
#elif CASE == 37
__private_extern__ int x; int x;
#elif CASE == 38
__private_extern__ int x; int x = 3;
#elif CASE == 39
int x; __private_extern__ int x;
#elif CASE == 40
__private_extern__ __private_extern__ int x;
#endif
