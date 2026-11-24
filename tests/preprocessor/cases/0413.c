/* lead */ int a; // tail
#define F(x) [x]
#define O /* in body */ 1
F /* not invoked */ (1)
F(2 /* dropped */) O
/* c */ #define NOT 9
NOT
#if 0
/* skipped */ #endif
/* still skipped */
#endif /* after endif */
#include "other.h"
#include "other.h"
/* multi
   line */ int m; // splice \
continued
int last; /* end */
