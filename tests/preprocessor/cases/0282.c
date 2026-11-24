#define S(x) #x
#define T(x) S(x)
#define P(a, b) a ## b
T(P(\u00e9, \u00e9) P(\U000000E9x, y))
