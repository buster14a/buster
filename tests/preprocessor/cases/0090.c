#define EMPTY
#define S(x) #x
#define EXPAND(x) S(x)
EXPAND(a EMPTY+b)
#define F(x) a x+b
EXPAND(F())
#define G()
EXPAND(a G()+b)
#define H(x) a x
EXPAND(H()+b)
#define E2 EMPTY
EXPAND(a E2+b)
#define J(x, y) [x y]
EXPAND(J(, +))
#define K(x) x
EXPAND(a K()+b)
#define V(...) a __VA_OPT__(b)c
EXPAND(V() V(1))
