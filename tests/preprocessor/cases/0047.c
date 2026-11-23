#define F(x) S(x)
#define S(x) #x
#define E(x,y) x ## y
#define G(x) [x]
F(a E(,)b G(1))
