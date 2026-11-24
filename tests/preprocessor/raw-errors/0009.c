#define DROP(x) 1
#if DROP(R"bad space")
kept
#endif
