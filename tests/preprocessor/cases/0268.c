#define X 1
#pragma push_macro("X")
#undef X
#define X 2
X
#pragma pop_macro("X")
X
#pragma pop_macro("X")
X
#pragma push_macro("Y") tail
