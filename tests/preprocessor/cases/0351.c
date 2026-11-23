#define C(a, b) a ## b
#define D(a, b) < a ## b >
#define D3(a, b, c) < a ## b ## c >
#define WP(a) <D a>
#define WQ(a) <D3 a>
#define WC(a) C a
WP(
1) WP((,
y))
WQ(
1) WQ((, ,
r))
WP(
1) WP((
x, ))
WP(
1) WP((
x,
y))
WQ(
1) WQ((,
q, r))
WC(
1) WC((,
y))
WP(1) WP((,
y))
WP((,
y))
D(,
y)
