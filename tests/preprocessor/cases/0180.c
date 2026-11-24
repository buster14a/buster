%:define D 1
D __STDC_VERSION__ <%= <::>
x // y
#define C(a, b) a %:%: b
#define F(x) %:x
#define P(a, b) a ## b
C(p, q) F(y) P(%:, %:) P(<, :)
