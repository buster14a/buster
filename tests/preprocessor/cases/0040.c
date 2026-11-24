#define D(a, b) a b a b
#define X x
#define P(a) [a]
#define W(a, b) D(a, b) P(a)
D(X, z) D(z, X) W(X, X) W( X ,z)
