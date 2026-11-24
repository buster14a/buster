#define S(x, y) #x y #y
#define T(x) x ## _ #x
#define W(x, y) S(x, y) T(x)
S(a, b) S(, ) S(a  +  b, "q\\") T(p) W(a, b) W(, ) W(q, "y")
