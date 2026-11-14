enum E : unsigned char { A = 255, B = 0 }; enum E : unsigned char; typedef unsigned int U;
_Static_assert(sizeof(enum E) == 1 && _Alignof(enum E) == 1 && _Generic(A, enum E: 1, default: 0) && _Generic(+A, int: 1, default: 0), "E");
enum F : long long { C = -1, D }; enum G : short; enum G : short { H = sizeof(enum G) }; enum I : const U { J = 4294967295u };
_Static_assert(sizeof(enum F) == 8 && D == 0 && H == 2 && J == 4294967295u && sizeof(enum I) == 4, "F G I");
enum K : _Bool { L, M }; enum N : signed char { O = -128, P = 127 }; enum : unsigned long { Q = 1ul << 63 }; enum R : unsigned { S = -1 };
_Static_assert(M == 1 && sizeof(enum K) == 1 && Q == 9223372036854775808ul && S == 4294967295u, "K Q S");
enum __attribute__((aligned(8))) T : char { V }; enum __attribute__((packed)) W : int { X }; enum Y : short { Z } __attribute__((packed));
_Static_assert(_Alignof(enum T) == 8 && sizeof(enum T) == 1 && sizeof(enum W) == 4 && sizeof(enum Y) == 2, "T W Y");
struct St { enum E e : 4; enum E f : 8; enum F g; }; _Static_assert(sizeof(struct St) == 16, "St");
int f(enum E e) { switch (e) { case A: return 1; case B: return 2; default: return 3; } }
unsigned g(void) { enum E e = A; enum E : unsigned char; return e + 1u; }
int h(enum N n) { return n == O ? 1 : n; }
