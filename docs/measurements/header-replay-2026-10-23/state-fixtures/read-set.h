#if defined(CHOICE) && CHOICE == 1
#define RESULT one
#elif defined(CHOICE) && CHOICE == 2
#define RESULT two
#else
#define RESULT other
#endif
#undef OLD
#define OLD replacement
#define JOIN_INNER(a, b) a ## b
#define JOIN(a, b) JOIN_INNER(a, b)
#define STRING_INNER(a) #a
#define STRING(a) STRING_INNER(a)
RESULT OLD JOIN(RESULT, _suffix) STRING(RESULT)
