#if __has_include(<dir//x.h>) /* comment
#else
#define IGNORED */ && 0
int wrong_branch;
#endif
#ifdef IGNORED
int wrong_define;
#endif
#if __has_include(<dir//x.h>) /* comment
#elifdef IGNORED
*/ && 0
wrong
#else
int real_else;
#endif
int end;
