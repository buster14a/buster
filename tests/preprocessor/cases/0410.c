#if __has_include(<dir/*x.h>) && __has_embed(<dir/*x.bin> limit(1)) == 1
found
#else
wrong
#endif
#if __has_include(<dir/*x.h>) && 0
wrong
#else
false_branch
#endif
#if 0
wrong
#elif __has_include(<dir/*x.h>)
elif_branch
#endif
after
