enum E : char { A = 200 };
_Static_assert(A == 200, "fixed base");
_Static_assert((enum E)-1 > 0, "unsigned base");
_Static_assert(u8'\xff' == 255, "u8");
