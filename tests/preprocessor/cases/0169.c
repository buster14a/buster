#define DROP(x) 1
#if DROP(R"(a
b)")
kept
#endif
after
