enum G : char { C = sizeof(enum G) } __attribute__((aligned(8)));
_Static_assert(sizeof(enum G) == 1 && _Alignof(enum G) == 1, "G");
enum H : char { D = _Alignof(enum H) } __attribute__((aligned(4)));
_Static_assert(sizeof(struct { enum H h; char c; }) == 2, "H");
enum J : int { E = 1, K = (enum J)0 } __attribute__((aligned(8)));
_Static_assert(_Alignof(enum J) == 8, "J");
enum Q : char { R = sizeof(enum Q[2]) } __attribute__((aligned(8)));
_Static_assert(_Alignof(enum Q) == 1, "Q");
