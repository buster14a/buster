// Values left over in a list whose designator named no member, or in one around it, give no excess warning (#1842).
struct P { int x; char y; };
struct Q { struct P a; struct P b; };
struct T { int x, y; };
struct T t1 = { .nope = 1, 1, 2, 3, 4 };
struct T t2 = { .y = 1, .nope = 2, 5, 6, 7 };
struct Q q1 = { .nope = 1, 1, 2, 3, 4, 5 };
struct Q q2[1] = { { .nope = 1 }, { 1 } };
struct Q q3[1] = { { .a = { .nope = 1 }, 1, 2, 3 } };
union U { int i; struct P p; };
union U u1 = { .nope = 1, 5, 6 };
struct T t3[2] = { [0].nope = 1, 2, 3, 4, 5, 6 };
