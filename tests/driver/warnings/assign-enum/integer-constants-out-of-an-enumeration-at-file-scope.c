enum A { A1, A2 = sizeof(enum B { B1, B2 = 5 }) } a;
enum A x = { 9 };
enum B y = 3;
struct S { enum A m[2]; enum B n; } s = { { 1, 7 }, 4 };
#define SET(v) a = v
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wassign-enum"
enum A ig = 9;
#pragma clang diagnostic pop
enum A f(void) { SET(3); return 0 ? 2 : 3; }
