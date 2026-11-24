#define C(a, b) a ## b
#define W(a, b) [C(a, b)]
#define D(a, b) < a ## b >
#define WD(a, b) D(a, b)
C(
x, y) C(,
y) W(
x, y) W(,
y) C(
x,
y) W(
,
)
D(,
y) D(
x, ) D(
x, y) W(
x, ) WD(,
y) WD(
x, ) WD(
x, y)
#define WP(a) [C a] <D a>
WP((
x, )) WP((,
y)) WP((
x,
y))
#define C3(a, b, c) a ## b ## c
#define D3(a, b, c) < a ## b ## c >
#define WQ(a) [C3 a] [D3 a]
WQ((,
q, r)) WQ((
p,
q, r)) WQ((, ,
r))
