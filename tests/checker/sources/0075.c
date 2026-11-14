enum E : int { A, B = _Generic(A, int: 2, default: 3) }; enum F : int { C };
_Static_assert(B == 2 && _Generic(A, __typeof__(C): 1, default: 0) == 0 && _Generic(A, enum E: 1, default: 0), "C23");
enum G : unsigned char { D }; _Static_assert(_Generic(D, enum G: 1, default: 0) && _Generic(+D, int: 1, default: 0), "G");
