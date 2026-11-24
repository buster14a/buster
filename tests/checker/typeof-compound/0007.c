typedef float V __attribute__((vector_size(16))); __typeof__((V){0}) v; _Static_assert(__builtin_types_compatible_p(__typeof__(v), V), "vector");
