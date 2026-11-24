struct S { char c; int a; } __attribute__((packed));
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wodr"
struct S { char c; int a; };
#pragma clang diagnostic pop
_Static_assert(sizeof(struct S) == 5, "first");
struct U { char c; int a __attribute__((aligned(16))); };
#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wodr"
struct U { char c; int a; };
#pragma clang diagnostic pop
_Static_assert(sizeof(struct U) == 32, "first u");
struct V { int a; } __attribute__((aligned(8)));
struct V { int a; } __attribute__((aligned(16)));
