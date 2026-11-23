#define L changed
#define S_(...) #__VA_ARGS__
#define S(...) S_(__VA_ARGS__)
#if __has_include(<dir//x.h>) && _Pragma(S(ppf $L"x")) 1
yes
#endif
