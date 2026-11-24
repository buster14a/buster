#define F(x) [x]
#define G() 1
F(a
#if G()
#endif
#pragma omp parallel
#include "other.h"
b)
after
