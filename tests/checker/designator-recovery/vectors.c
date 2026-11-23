// A static vector member's value that is no constant, in a list with a designator that names no member: clang checks no
// constants there (#1842).
typedef int v4 __attribute__((vector_size(16)));
typedef int e4 __attribute__((ext_vector_type(4)));
struct V { int a; v4 v; };
struct E { int a; e4 v; };
v4 gv;
int gi;
struct V s1 = { .nope = 1, 0, gv };
struct V s2 = { 0, gv, .nope = 1 };
struct E s3 = { .nope = 1, 0, (e4)gi };
struct V s4 = { 0, gv };
