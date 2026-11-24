int x __attribute__((aligned(32))); enum { A=__alignof__(x), B=__alignof__((x)) }; _Static_assert(A==32 && B==32, "object alignment");
