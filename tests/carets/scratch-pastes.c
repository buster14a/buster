#define CAT(a, b) a ## b
#define NEST(a, b) CAT(a, b)
#define SUFFIX(x) pre ## x
#define ID(a) a
#define CHAIN a ## b ## c
#define BOTH(a, b, c) a ## b + b ## c
int a = CAT(un, declared);
int b = NEST(miss, ing);
int c = SUFFIX(fix);
int d = ID(CAT(un, known));
int e = CHAIN;
int f = BOTH(m, is, sing);
