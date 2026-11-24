#define C(a, b) a ## b
#define C3(a, b, c) a ## b ## c
#define W(a, b) [C(a, b)]
#define W3(a, b, c) [C3(a, b, c)]
#pragma GCC poison foobar
W(foo, bar) W3(foo, bar, baz) W(foo, ba)
