typedef _Atomic(int) Atomic; enum E : _Atomic int { E0 }; enum F : Atomic { F0 };
_Static_assert(sizeof(enum E)==4 && sizeof(enum F)==4, "atomic base stripped");
