#define E1 z
#define EMPTY
#define F(...) __VA_OPT__(x) ## y
F() F(1)
#define G(...) prefix __VA_OPT__(x) ## y
G() G(1)
#define H(...) __VA_OPT__() ## y
H() H(1)
#define R(...) a ## __VA_OPT__(b c)
R() R(1)
#define S(...) a ## __VA_OPT__(__VA_ARGS__ q)
S() S(E1) S(1 2)
#define T(x, ...) __VA_OPT__(x) ## x
T(1) T(1, 2)
#define U(...) __VA_OPT__(a ## b) ## c
U() U(1)
#define W(...) __VA_OPT__(x)##__VA_OPT__(y)
W() W(1)
#define X(a, ...) a ## __VA_OPT__(a) ## a
X(1) X(1, 2)
#define Y(...) __VA_OPT__(__VA_ARGS__) ## y
Y() Y(E1) Y(1 2) Y(E1 E1)
#define Z(p, ...) p ## __VA_OPT__() ## p
Z(1) Z(1, 2)
#define P(...) __VA_OPT__(x y) ## __VA_OPT__(z w)
P() P(1)
#define P1(x, ...) __VA_OPT__(a x) ## y
P1(E1, 1) P1(EMPTY, 1) P1(, 1)
#define P2(x, ...) a ## __VA_OPT__(x b)
P2(, 1) P2(E1, 1) P2(c, 1)
#define P4(x, y, ...) a ## __VA_OPT__(x y) ## b
P4(, , 1) P4(c, , 1) P4(, d, 1) P4(c, d, 1)
#define S2(...) L ## #__VA_OPT__(a b)
S2() S2(1)
#define V2(...) w __VA_OPT__( __VA_ARGS__ ) ## v
V2() V2( 1 ) V2(1 2)
