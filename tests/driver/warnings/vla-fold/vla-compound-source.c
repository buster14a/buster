int a[(int){3}];
int b[(long){2 * 4} + 1];
struct S { int m[(int){2}]; };
void f(void) { static int s[(int){4}]; int t[(int){5}]; int *p = &(int){1}; (void)s; (void)t; (void)p; }
int c[(const int){2}];
enum { E = (int){1} };
int e = (int){3} + E;
int sz = sizeof((int){1});
_Static_assert(sizeof(a) == 12 && sizeof(b) == 36, "");
