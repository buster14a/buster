#define A 1
#define F(a, ...) a + __VA_ARGS__
#undef A
#undef NEVER
#define A 2 /* c */
int x = A;
#include "other.h"
#include "other.h"
int y = F(1, 2);
#define  G( x )  x
#undef G
