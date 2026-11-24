# 1 "odr_system.h" 1 3
struct S { char c; int a; } __attribute__((packed));
struct S { char c; int a; };
# 4 "odr_main.c" 2
_Static_assert(sizeof(struct S) == 5, "first");
struct S s;
