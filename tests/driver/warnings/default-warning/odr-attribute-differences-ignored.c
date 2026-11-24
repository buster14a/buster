struct S { char c; int a; } __attribute__((packed));
struct S { char c; int a; };
_Static_assert(sizeof(struct S) == 5, "first");
struct U { char c; int a __attribute__((aligned(16))); };
struct U { char c; int a; };
_Static_assert(sizeof(struct U) == 32, "first u");
struct S s;
struct U u;
