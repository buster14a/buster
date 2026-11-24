typedef const int CI;
typedef volatile int VI;
void f(void) { CI b[2][3] = { 0 }; b++; b[0]++; VI c[1][2][3]; c++; c[0]++; }
