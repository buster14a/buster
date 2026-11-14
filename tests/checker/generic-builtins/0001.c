struct S { int a; }; union U { int a; }; enum E { X }; struct I; extern struct I inc;
int arr[3]; void fn(void); int g; _Atomic int ai; _Alignas(16) const volatile char cv;
#define C(e, v) _Static_assert(__builtin_classify_type(e) == (v), #e);
C(0, 1) C((char)0, 1) C((_Bool)0, 4) C(X, 1) C((enum E)0, 1) C((int*)0, 5) C(1.0, 8) C(1.0f, 8) C(1.0L, 8) C((__float128)1, 8)
C((_Float16)1, 8) C((_Complex double)0, 9) C((_Complex int)0, 9) C(fn, 5) C(&fn, 5) C((struct S){0}, 12) C((union U){0}, 13)
C(arr, 5) C("ab", 5) C((void)0, 0) C((_BitInt(7))0, 18) C((__attribute__((vector_size(16))) int){0}, 19) C((__int128)0, 1)
C(inc, 12) C(g++, 1) C(ai, 1) C(cv, 1) C(0UL, 1)
static int sizes[__builtin_classify_type(1.0)];
enum { K = __builtin_classify_type((struct S){0}) };
