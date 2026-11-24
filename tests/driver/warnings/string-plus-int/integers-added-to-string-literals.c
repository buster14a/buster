typedef unsigned long size_t;
typedef int myint;
enum X { X1 };
const char* a(void) { return "abc" + 1; }
const char* b(int i) { return i + "abc"; }
const char* c(char ch, _Bool f) { (void)("ab" + f); return "abc" + ch; }
const char* d(void) { return ("abc") + -1; }
const char* e(void) { return "abc" + sizeof(int) * 3; }
const char* g(size_t n, myint m, enum X x, _BitInt(7) w) { (void)("ab" + m); (void)("ab" + x); (void)("ab" + w); return "ab" + n; }
const char* h(void) { return "a" "b" "c" + 2; }
const char* k(int i) { const char* r = "abc"; r += i; (void)r; return "abc" - i; }
const char* m(int* p) { return (const char*)p + 1; }
const char* n(int i) { return L"abc" + i ? "x" : "y"; }
