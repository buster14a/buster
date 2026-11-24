#define S(...) #__VA_ARGS__
#define T(...) S(__VA_ARGS__)
#define f(x, ...) [x , ## __VA_ARGS__]
T(f(a,b))
T(f(a, b))
#define g(p, q, ...) [p ,q ## __VA_ARGS__]
T(g(a,))
#define h(p, ...) [, __VA_OPT__(__VA_ARGS__ ## a) ## __VA_ARGS__ ## b]
T(h(u))
#define X(p, ...) [p ## , ## __VA_ARGS__]
T(X(a))
