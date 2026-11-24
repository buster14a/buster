#define A 1
#define A 2
#define B(x) x
#define B(y) y
#define C 1
#define C  1
#define D a/**/b
#define D a b
#define E <:
#define E [
#define G 1 // c
#define G 1
#define H /* a */
#define H
#define I(x) x
#undef I
#define I(y) y
#define J(...) __VA_OPT__(a)
#define J(...) __VA_OPT__(a )
#define K(x) #x
#define K(x) # x
#define L 01
#define L 1
