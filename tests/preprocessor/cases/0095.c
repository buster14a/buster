#define ID(x) x
#if ID(__has_include("other.h")) && ID(!__has_include("missing.h"))
present
#else
absent
#endif
