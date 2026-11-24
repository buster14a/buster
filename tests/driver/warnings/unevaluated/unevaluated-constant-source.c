int x;
int arr[sizeof(x++)];
enum { E = sizeof(x = 1) };
_Static_assert(sizeof(x++) == 4, "s");
int y = sizeof(x--);
struct S { int a[sizeof(++x)]; };
int f(void) { int v[sizeof(x++)]; return sizeof v + _Alignof(int) + (int)sizeof(typeof(x++)); }
int g = _Generic(x++, int: 1);
char c[_Generic(x = 2, int: 3)];
