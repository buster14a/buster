#pragma clang diagnostic warning "-Wunused-macros"
#define A 1
#define B 2
#if 0 && defined(A)
#elif defined(B)
#endif
#define P 1
#pragma push_macro("P")
#undef P
#pragma pop_macro("P")
#define V 1
#pragma push_macro("V")
#pragma pop_macro("V")
#error stop
