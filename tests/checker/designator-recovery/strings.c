// A string too long for its array still warns in a list with a designator that names no member, and a later valid
// sibling list keeps its excess warning (#1842, #2441).
struct S { char s[2]; int z; };
struct R { struct S a; struct S b; };
struct S x1 = { .nope = 1, "abc" };
struct R x2 = { { .nope = 1, "ab", 2, 3 }, { "a", 2, 3 } };
