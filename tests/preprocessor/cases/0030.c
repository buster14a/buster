#define C(a, b) a ## b
#define W(a, b) [C(a, b)]
#define V(a, ...) a , ## __VA_ARGS__
#define O(a, ...) a ## __VA_OPT__(x)
C(a b, c) C(a, b c) C(<, <) C(L, 'x') C(u8, "s") C(a$, b) C(., 5)
W(a b, c) W(<, <) W(L, 'x')
V(1) V(1, 2) O(a) O(a, b)
