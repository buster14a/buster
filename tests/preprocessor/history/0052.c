#pragma clang diagnostic warning "-Wunused-macros"
#include "history.h"
#define A 1
#define B A
#define C 3
#undef C
#define D 4
#if D
#endif
#define E 5
#ifdef E
#endif
#define F(x) x
F
#undef HEADER
#define G 7
#define H 8
#error stop
