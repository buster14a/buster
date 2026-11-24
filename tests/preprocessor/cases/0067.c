#define F(x, ...) f(x __VA_OPT__(,) __VA_ARGS__)
F(1) F(1,2) F(1,)
#define C(a,...) g(a, ## __VA_ARGS__)
C(1) C(1,) C(1,2)
