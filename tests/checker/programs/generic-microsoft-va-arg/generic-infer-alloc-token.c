typedef unsigned long uintptr_t; typedef uintptr_t handle; static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }
struct I; struct P { struct I* p; }; struct Q { int a[3]; long b; }; typedef struct { char c; } T; enum E { E0 }; union U { int a; float b; };
struct R { struct R* next; }; struct W { handle u; }; struct O { struct N { int x; } n; struct { char c; } a; }; struct V { struct Q q[2]; struct W w; };
int tested(int n) { int i = n; unsigned long s = sizeof(struct Q) * 2; h = (unsigned)n; mix(__builtin_infer_alloc_token(i++));
mix(__builtin_infer_alloc_token(sizeof(struct I*))); mix(__builtin_infer_alloc_token(sizeof(struct P))); mix(__builtin_infer_alloc_token(s));
mix(__builtin_infer_alloc_token(3 + sizeof(T))); mix(__builtin_infer_alloc_token(sizeof(enum E))); mix(__builtin_infer_alloc_token(sizeof(union U)));
mix(__builtin_infer_alloc_token(sizeof(struct R))); mix(__builtin_infer_alloc_token(sizeof(uintptr_t))); mix(__builtin_infer_alloc_token(sizeof(struct W)));
mix(__builtin_infer_alloc_token(sizeof(int[4]))); mix(__builtin_infer_alloc_token(sizeof(int (*)(int)))); mix(__builtin_infer_alloc_token(sizeof(const char*)));
mix(__builtin_infer_alloc_token(sizeof i)); mix(__builtin_infer_alloc_token(4, sizeof(double))); mix(__builtin_infer_alloc_token(sizeof(_Atomic(int))));
mix(__builtin_infer_alloc_token(sizeof(volatile int))); mix(__builtin_infer_alloc_token(sizeof(struct N)));
mix(__builtin_infer_alloc_token(n * sizeof(struct V) << 1)); mix(__builtin_infer_alloc_token((sizeof(struct V) / 2) - 1)); mix((unsigned)i);
return (int)(h ^ (h >> 32)); }
