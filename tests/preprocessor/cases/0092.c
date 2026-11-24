#include "other.h"
#define EMPTY
#define ZERO 0
#define F() 1
#define P 1 ## 0
#if EMPTY 1
a
#endif
#if EMPTY_LAST 2 == 2 && ZERO == 0
b
#endif
#if __LINE__ > 0
c
#endif
#if F() == 1
d
#endif
#if P == 10
e
#endif
