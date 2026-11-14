enum E : int { A }; enum F : int { C }; _Static_assert(_Generic(A, __typeof__(C): 1, default: 0) == 0, "C17");
