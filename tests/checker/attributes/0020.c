typedef int V __attribute__((vector_size(16))); _Static_assert(sizeof(V)==16 && _Alignof(V)==16,"vector");
