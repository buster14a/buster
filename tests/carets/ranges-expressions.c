// Expressions' highlighted ranges (#2376): an indirection's operand, a shift's count, a divisor; with their parentheses,
// through a macro, and over several lines.
int indirection(void) { return *1; }
int grouped(void) { return *(1 + 2); }
int folded(void) { return 1 << 32; }
int counted(int x) { return x << (16 + 16); }
int divided(void) { return 1 / 0 + 2 % (0); }
#define SHIFT(x) (1 << (x))
int shifted(void) { return SHIFT(40); }
int split(void) { return 1
    << 33; }
int lines(void) { return *(1 +
                       2); }
int count(int x) { return x << (
    16
    +
    17); }
#line 9998
int numbered(void) { return *(1 +
  2 +
  3); }
int many(int x) { return x << (1 +
2 +
3 +
4 +
5 +
6 +
7 +
8 +
9 +
10 +
11 +
12 +
13 +
14 +
15 +
16 +
17 +
18 +
19); }
