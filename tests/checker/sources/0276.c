unsigned _BitInt(1) a = 1;
_BitInt(1) unsigned b;
typedef unsigned _BitInt(1) B1;
B1 c;
int f(void) { unsigned _BitInt(1) d = 1; return d + (int)sizeof d + (int)sizeof(unsigned _BitInt(1)); }
