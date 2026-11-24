struct T { char a; char b; };
int z = (1 << 40, 3), y = (2, 1 << 41);
int w = (2 << 42, 1 << 43, 3);
char c = (1 << 42, 300);
struct T s = { (1 << 43, 1), (4, 1 << 44) };
int arr[2] = { (5, 6), 7 };
struct T g = { (5, 302), (6, 303) };
void f(int x) { struct T t = { (1, 300), (2, 301) }; struct T u = { (3, x << 40), 1.5 }; struct T v = { x / 0, (4, x) }; (void)t; (void)u; (void)v;
    static int k = (1 << 45, 3); (void)k; int q = (1 << 46, 2); (void)q; }
