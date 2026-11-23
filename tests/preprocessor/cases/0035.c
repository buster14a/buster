#define P(a, b) [ a ## b]
#define Q(a, b) x a ## b
#define R(a, b) a ## b c
#define WP(a, b) P(a, b) Q(a, b)y R(a, b)
P(,) P(u,) Q(,)y Q(,v)y R(,) R(, d)
WP(,) WP(u, v)
