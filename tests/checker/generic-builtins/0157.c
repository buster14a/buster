typedef unsigned long uintptr_t;
typedef uintptr_t handle;
struct S { int a; char* p; };
struct W { handle h; };
struct Q { int a[3]; long b; };
typedef struct { short s; } TS;
_Static_assert(__builtin_infer_alloc_token(sizeof(int)) == 2689373973731826898u, "");
_Static_assert(__builtin_infer_alloc_token(sizeof(struct S)) == 9965185003513084084u, "");
_Static_assert(__builtin_infer_alloc_token(sizeof(char*) * 4) == 11473864704255292954u, "");
_Static_assert(__builtin_infer_alloc_token(3 + sizeof(TS)) == 2947234827507279461u, "");
_Static_assert(__builtin_infer_alloc_token(sizeof(struct W)) > 9223372036854775807u, "");
_Static_assert(__builtin_infer_alloc_token(sizeof(uintptr_t)) > 9223372036854775807u, "");
_Static_assert(__builtin_infer_alloc_token(sizeof(struct Q)) == 6602487442970685230u, "");
_Static_assert(sizeof(__builtin_infer_alloc_token(1)) == 8, "");
enum { E = __builtin_infer_alloc_token(sizeof(int[4])) != 0 };
unsigned long f(int i)
{
unsigned long s = sizeof(struct Q) * 2;
static unsigned long t = __builtin_infer_alloc_token(sizeof(_Bool));
return __builtin_infer_alloc_token(i++, sizeof(char)) + __builtin_infer_alloc_token(s) + __builtin_infer_alloc_token(i) + t;
}
