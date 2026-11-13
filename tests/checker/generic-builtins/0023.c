#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);
struct S { int a; } s; int g; void fn(void); int arr[3];
int* pg = __builtin_addressof(g); void* ps = __builtin_function_start(fn); int (*pa)[3] = __builtin_addressof(arr);
struct S* pss = __builtin_addressof(s); int* pm = __builtin_addressof(s.a); void (*pf)(void) = __builtin_addressof(fn);
void f(unsigned long n, int x, long y)
{
T(__builtin_alloca_uninitialized(n), void*) T(__builtin_alloca_with_align(n, 64), void*) T(__builtin_alloca_with_align_uninitialized(n, 8 * 4096), void*)
T(__builtin_allow_runtime_check("x"), _Bool) T(__builtin_allow_sanitize_check("address"), _Bool) T(__builtin_annotation(y, "a"), long)
T(__builtin_annotation((char)x, "a"), char) T(__builtin_addressof(x), int*) T(__builtin_function_start(f), void*)
int* p = __builtin_addressof(x); (void)p; (void)__builtin_allow_sanitize_check("kernel-hwaddress");
}
