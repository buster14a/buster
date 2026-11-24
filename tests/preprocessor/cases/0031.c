#define C(a, b) a ## b
#define W(a, b) [C(a, b)]
#define foobar 42
#define fn(x) <x>
#define G(x) G ## x
#define A B(A, )
#define B(x, y) x ## y
C(foo, bar) W(foo, bar) C(f, n)(1) W(f, n)(2) G() G(1) A W(A, ) C(fo, obar)
