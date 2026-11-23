#define M(x, y) < x ## _end y pre_ ## x ## _post #x y >
#define N(x) x ## 1 x 1 ## x
#define W(x, y) M(x, y) N(x)
M(a, b) M(, b) M(a, ) N(z) N()
W(a, b) W(, ) W(q r, s)
#define ONE 1
#define K(x) ONE ## x ONE
#define WK(x) [K(x)]
K(2) K() WK(3) WK()
