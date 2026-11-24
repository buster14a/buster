typedef int V __attribute__((vector_size(2*sizeof(int)),aligned(1<<4))); _Static_assert(sizeof(V)==8 && _Alignof(V)==16,"ICE");
