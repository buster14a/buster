// A designator that names no member ends a brace elision's list: the list's later values take the members after the
// elided one, as clang's (#1842).
struct P { int x; char y; };
struct Q { struct P a; struct P b; };
struct T { int x, y; };
struct M { struct T a; char *s; };
struct T t[3] = { 1, 2, 3, [2].nope = 4, 5 };
struct Q q = { 1, .nope = 2, 3 };
struct M m1 = { 1, .nope = 2, "s" };
struct M m2 = { 1, .nope = 2, 3 };
