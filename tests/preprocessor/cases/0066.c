#define EMPTY
#define V(...) 1 __VA_OPT__(+ 2)
V()
V(EMPTY)
V(x)
