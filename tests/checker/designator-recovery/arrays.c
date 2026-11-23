// An array's designator whose member designator fails leaves the array's place where it was (#1842).
struct P { int x; char y; };
struct P gp;
struct P ap[3] = { [2].nope = 1, { "s" }, { 1, gp } };
struct P aq[3] = { { 1 }, [2].nope = 1, { 2, gp } };
