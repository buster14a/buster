typedef unsigned int U2 __attribute__((mode(HI)));
_Static_assert((U2)-1 > 0 && sizeof(U2) == 2, "unsigned");
typedef unsigned char U4 __attribute__((mode(SI)));
_Static_assert((U4)-1 > 0 && sizeof(U4) == 4, "unsigned char");
typedef int S2 __attribute__((mode(HI)));
_Static_assert((S2)-1 < 0, "signed");
typedef char C1 __attribute__((mode(QI)));
_Static_assert((C1)-1 < 0, "plain char");
