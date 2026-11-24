#define F(a) (a + 1)
#define C(x, y) x ## y
#define G(x) [x]
#define S(x) #x
#define Z() zz
#define V(x, ...) x __VA_ARGS__
#define X 2
#define foo 7
F(C(a, b)) F(G(1)) F(S(a  b)) F(Z()) F(V(1)) F(V(1, 2, 3)) F(C(a, b) G(c) X) F(X C(p, q)) F(C(fo, o)) F(G(X))
