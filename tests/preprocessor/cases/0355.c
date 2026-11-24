#define c(x) x
#define m 1
#pragma push_macro("m") x c(
#undef m
#define m 2
#pragma pop_macro("m") y c(
m
