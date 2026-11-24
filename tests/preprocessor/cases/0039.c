#define F(x) x
#define Y y
#define G(x) F(G Y)(x)
#define H(x) F(x H Y)
G(1) H(2) F(Y G) F(Y)(3)
