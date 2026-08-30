// A GNU aligned attribute on the extern declaration reaches a bare
// definition: attributes merge across declarations, so mimalloc's
// `extern mi_decl_cache_align mi_stats_t _mi_stats_main;` aligns the
// unadorned definition the way GCC and Clang align it.  C11 6.7.5p7 makes
// the same shape an error for _Alignas, and that refusal stands.
typedef struct stats { long counts[8]; } stats_t;
extern __attribute__((aligned(64))) stats_t shared_stats;
stats_t shared_stats;
int main(void) { return ((unsigned long)&shared_stats % 64) == 0 ? 0 : 1; }
