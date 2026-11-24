struct S { long x; }; typedef int T [[gnu::aligned(sizeof(struct __attribute__((aligned(sizeof(struct S)))) S))]]; _Static_assert(_Alignof(T)==8, "raw nested tag attributes");
