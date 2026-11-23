#define L(x, y) x y ## z
#define W(x, y) L(x, y)
#define LONG a b c d e f g h i j
#define ONE 1
#define M(x, y) LONG x ## y LONG
#define N(x, y) ONE x ## y ONE
#define WM(x, y) M(x, y) N(x, y)
L(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15, a) L(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16, a)
W(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15, a) W(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16, a)
M(p, q) N(p, q) N(, ) WM(p, q)
