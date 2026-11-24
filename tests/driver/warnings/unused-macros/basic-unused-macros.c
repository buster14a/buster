#define USED 1
#define UNUSED 2
#define ONLY_IF 3
#define IN_BODY 4
#define OUTER IN_BODY
#define FN(x) (x)
#define TESTED 6
#define LIVE 7
#define DEAD 8
#define BEFORE 9
#undef BEFORE
#define TWICE 10
#define TWICE 10
#define AGAIN 11
#undef AGAIN
#define AGAIN 12
#if ONLY_IF
#endif
#ifdef TESTED
#endif
#if defined(LIVE) || defined(DEAD)
#endif
#if 0 && defined(DEAD)
#endif
int FN;
int x = USED + FN(1) + AGAIN;
