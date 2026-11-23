#define EMPTY
#define LPAREN (
#define f(x) [x]
f EMPTY (1)
f LPAREN 2)
#define g f
g(3)
