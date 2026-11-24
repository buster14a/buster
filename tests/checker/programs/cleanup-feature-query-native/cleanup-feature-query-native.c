#if __has_attribute(cleanup) && __has_attribute(__cleanup__) && __has_c_attribute(gnu::cleanup)
#define CLEAN __attribute__((cleanup(finish)))
#else
#define CLEAN
#endif
#if __has_c_attribute(gnu::__cleanup__)
#define SCOPED_CLEAN [[gnu::cleanup(finish)]]
#else
#define SCOPED_CLEAN
#endif
static int seen;static void finish(int*p){seen+=*p+3;}int tested(int n){seen=0;{CLEAN int a=n&7;}{SCOPED_CLEAN int b=n&3;}return seen;}