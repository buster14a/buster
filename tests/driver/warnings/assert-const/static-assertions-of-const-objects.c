const int k = 0;
_Static_assert(k == 45, "");
const long m = 7;
_Static_assert(m + 1 == 3, "");
struct P { int a; }; const struct P p = { 4 };
_Static_assert(p.a == 5, "");
const int arr[2] = { 1, 2 };
_Static_assert(arr[1] == 5, "");
void f(void) { static const int s = 3; _Static_assert(s == 4, ""); }
const char c = 'x';
_Static_assert(c == 'y', "");
