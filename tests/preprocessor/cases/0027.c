#define C2(a, b) a ## b
#define C3(a, b, c) a ## b ## c
#define ID(x) x
#define W2(a, b) [C2(a, b)]
#define W3(a, b, c) [C3(a, b, c)]
[C2(x, y)] [C2(, y)] [C2(x, )] [C2(,)] [C3(p, q, r)] [C3(, q, r)] [C3(p, , r)] [C3(p, q, )] [C3(, , r)] [C3(,,)]
W2(x, y) W2(, y) W2(x, ) W2(,) W3(p, q, r) W3(, q, r) W3(p, , r) W3(p, q, ) W3(, , r) W3(,,)
a C2(b, c)d e C2( , f) g C2(h ,)i
