// The counted_by lists after a designator that names no member (#1842's leftover of 3d): clang fills in the later braces'
// lists, but not those that the braces with the error close.
struct Inc;
struct B { int n; struct Inc *p __attribute__((counted_by(n))); };
struct O { struct B a; struct B b; struct B c; };
struct O o1 = { .zz = 1, { 0, 0 }, { 1 } };
struct O o2 = { .zz = 1, 0, 0, 1 };
struct O o3[1] = { { .zz = 1, 0, 0, 1 } };
struct O o4[2] = { { .a.zz = 1, 2 }, { 3 } };
