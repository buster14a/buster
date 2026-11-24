#define F(a) a (2)
#define S(x) x S
#define G(x) [x]
#define H(x) F(G(H x))
#define K(x) F(G(x) K)
F(S(1)) H(3) K(4)
