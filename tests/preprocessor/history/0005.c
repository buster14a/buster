#define INNER MISSING
#define OUTER INNER
#define ID(x) x
#if ID(OUTER)(1)
#endif
