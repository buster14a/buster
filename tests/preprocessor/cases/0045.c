#define F(a, b) {a | b}
#define G(x) [x]
F(G(1) G(2) G(3) G(4), G(5) G(6) G(7) G(8)) F(G(1) G(2) G(3) G(4), G(5) G(6) G(7) G(8) G(9))
