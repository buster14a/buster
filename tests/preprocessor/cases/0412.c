#if __has_include(<dir//x.h>) && __has_include(<  x.h>)
int wrong_branch;
#else
int expected_branch;
#endif
#if __has_include(<dir//x.h>) && __has_include(<dir//x.h>) && __has_embed(<dir//x.h> limit(1)) == 1
int repeated;
#endif
#if __has_include(<dir//x.h>) && (1 + (2 * 3) == 7) && __has_include(<dir//x.h>)
int parentheses;
#endif
