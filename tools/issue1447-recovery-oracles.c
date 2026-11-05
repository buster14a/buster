// Original diagnostic and recovery probes for #1447. CASE chooses one independent source.
#if CASE == 1
[[maybe_unused]];
#elif CASE == 2
__attribute__((unused));
#elif CASE == 3
__attribute__(());
#elif CASE == 4
int x; __attribute__(()) x = 1;
#elif CASE == 5
int h(a) int a const; { return a; }
#elif CASE == 6
restrict;
#elif CASE == 7
int v
#pragma GCC visibility push(hidden)
const;
#pragma GCC visibility pop
#elif CASE == 8
int a __attribute__((aligned(3)));
int b __attribute__((aligned(7)));
int c = undeclared;
#elif CASE == 9
struct S { int a[bad]; };
int z __attribute__((aligned(3)));
#elif CASE == 10
typedef struct S T;
T x;
int bad __attribute__((aligned(3)));
#elif CASE == 11
int x
int y __attribute__((unknown));
#elif CASE == 12
#pragma weak w
int w;
#elif CASE == 13
#pragma redefine_extname foo bar
int foo(void) { return 1; }
#elif CASE == 14
#pragma GCC scalar_storage_order big-endian
struct S { int x; };
#elif CASE == 15
#pragma ms_struct on
struct S { int x; };
#elif CASE == 16
#pragma float_control(precise, on)
float f(float x) { return x + 1; }
#elif CASE == 17
#pragma fenv_access(on)
float f(float x) { return x + 1; }
#elif CASE == 18
#pragma clang fp contract(off)
float f(float x) { return x + 1; }
#elif CASE == 19
#pragma data_seg("foo")
int x = 3;
#elif CASE == 20
__attribute__((unused)) const;
#elif CASE == 21
[[gnu::unused]];
#elif CASE == 22
[[deprecated]];
#elif CASE == 23
int f(a) int a const; int z; { return a; }
#elif CASE == 24
int a;
int b = a;
int c = undeclared;
#endif
