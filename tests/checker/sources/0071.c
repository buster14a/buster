struct T { int a[]; }; struct U { int : 0; char c[]; }; struct V { char : 3; long l[]; };
struct W { struct T t; int b; }; struct T *p; int n = sizeof(struct T) + sizeof(struct U);
_Static_assert(sizeof(struct T) == 0 && _Alignof(struct T) == 4, "T");
_Static_assert(sizeof(struct U) == 0 && _Alignof(struct U) == 1, "U");
_Static_assert(sizeof(struct V) == 8 && _Alignof(struct V) == 8 && __builtin_offsetof(struct V, l) == 8, "V");
_Static_assert(sizeof(struct W) == 4, "W");
