#define F(a) <a>
#define P(x, y) x ## y
#define G(x) [x]
F(x P(, ) y) F(P(, )) F(P(, ) y) F(x P(, )) F(G(1)G(2)) F( G(1) G(2) ) F(G(1)z)
