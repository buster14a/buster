#define C(a, b) a ## b
#define X(a, b) C(a, b)
#define W(a, b) [C(a, b)]
C(1, 2) C(x, 1) C(1, x) C(0x, 1fu) C(1e, 5) C(__LINE__, u)
W(1, 2) W(x, 1) W(1, x)
X(__LINE__, u) X(u, __LINE__) X(__LINE__, __LINE__)
