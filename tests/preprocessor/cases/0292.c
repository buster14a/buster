#define F(y) x ## ## y z
F()
#define G(y, w) x ## ## y w
G(,q)
#define H(x, y) x ## ## y
H(,1)
