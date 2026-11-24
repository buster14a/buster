#define S(x) #x
#define T(x) S(x)
#define D(c, ...) #__VA_OPT__( c)
T((D(x)))
#define E(c, ...) [__VA_OPT__( c)]
T(E(q))
