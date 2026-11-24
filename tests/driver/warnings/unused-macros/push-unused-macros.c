#define P 1
#pragma push_macro("P")
#undef P
#pragma pop_macro("P")
#undef P
#define Q 1
#pragma push_macro("Q")
#define Q 2
#pragma pop_macro("Q")
#define V 1
#pragma push_macro("V")
#pragma pop_macro("V")
int z;
