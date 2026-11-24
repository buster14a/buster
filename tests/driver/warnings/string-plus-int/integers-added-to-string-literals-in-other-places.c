const char* g = "abc" + 1;
const char* h(int i) { return "abc" + (1 << 40) + i; }
#define P(x) (x + 1)
const char* k(void) { return P("abc"); }
const char* m(void) { return 1 + "x" "y"; }
const char* n(int i) { return "abc" + (i); }
