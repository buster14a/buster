#define A 1
#define PASTE_(a,b) a ## b
#define PASTE(a,b) PASTE_(a,b)
PASTE(value, A)
#undef A
#define A 2
#define B A
#define TAKE(x) x
TAKE(
#define A 3
#define C B
C)
#if 0
#define A ignored
#define C ignored
#endif
A C
