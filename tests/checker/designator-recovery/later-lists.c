// A designator that names no member: the list's later values still initialize members from the list's place, and their
// errors are clang's (#1842).
struct P { int x; char y; };
struct Q { struct P a; struct P b; };
struct T { int x, y; };
struct P gp;
struct T t1 = { .nope = 1, "s", 2 };
struct Q q1 = { .nope = 1, { 1, 2 }, { gp } };
struct Q q2 = { { 1 }, .a.nope = 2, { "s" } };
struct Q q3 = { .nope = { 1, 2 }, { gp }, { 3 } };
