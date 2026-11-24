#define F(a) (a + 1)
#define G(a, b) [a | b | a]
#define X 2
#define T unsigned long
#define E
#define N F(1)
F(X) F(T X) G(X, T) G(T, 3) F(X X) F(E) F(N) G(X, N) G(N, X)
