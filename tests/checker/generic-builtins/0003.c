#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);
struct S { int a; }; int g; struct S* sp; const int* cp; int a[4];
int* laundered = __builtin_launder(&g);
void f(void)
{
T(__builtin_launder(&g), int*) T(__builtin_launder(sp), struct S*) T(__builtin_launder(cp), const int*)
T(__builtin_launder(a), int*) T(__builtin_dwarf_cfa(), void*)
}
