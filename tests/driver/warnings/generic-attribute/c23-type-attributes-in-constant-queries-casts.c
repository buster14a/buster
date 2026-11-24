int x = _Generic((int *[[clang::address_space(1)]])0, int *: 1, default: 2);
int y = _Generic((int [[clang::annotate_type("a")]])0, int: 1, default: 2);
int z = _Generic((int *__attribute__((address_space(1))))0, int *: 1, default: 2);
void f(void) { int w = _Generic((int *[[clang::address_space(1)]])0, int *: 1, default: 2); (void)w; }
_Static_assert(_Generic((int *[[clang::address_space(1)]])0, int *: 1, default: 0) == 1, "as");
_Static_assert(_Generic((int *[[clang::annotate_type("a")]])0, int *: 1, default: 0), "anp");
_Static_assert(sizeof((int [[clang::annotate_type("b")]] *)0) == 8, "sz");
typedef typeof((int *[[clang::address_space(2)]])0) T;
_Static_assert(_Generic((T)0, int __attribute__((address_space(2))) *: 1, default: 0) == 0, "t");
int v = _Generic((char (*[[clang::annotate_type("c")]])[3])0, char (*)[3]: 1, default: 2);
