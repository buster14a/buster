#define P(a, b) a ## b
#define S(x) #x
#define T(x) S(x)
T(P(caf\u00e9, x) P(y, z))
T(P(caf\u00e9, x)P(y, z))
P(caf\u00e9, x)
