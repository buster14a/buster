#define C(...) .5 ## __VA_OPT__(e ## .5)
C(1)
#define V(...) __VA_OPT__(, ) ## __VA_OPT__(__VA_ARGS__ ## __VA_ARGS__)
V(u)
