#define F(a) <a>
#define C(x, y) x ## y
#define W(a) F(a) [F(a)]
F(
C(a, b)) F(C(a,
b)
) W(
C(c, d)) F(
  C(e, f) g)
W(x
  C(g, h)
  z)
