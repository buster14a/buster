#define LONG a b c d e f g h i j
#define M(x) LONG x LONG
#define SEVEN 1 2 3 4 5 6 7
#define B16(x) SEVEN x SEVEN x
#define B17(x) SEVEN x SEVEN x x
M(1)
B16(.)
B17(.)
