__attribute__((cpu_specific(generic))) int h(void) { return 1; }
__attribute__((cpu_specific(core_2_duo_ssse3))) int h(void) { return 2; }
__attribute__((cpu_dispatch(generic, core_2_duo_ssse3))) int h(void);
int caller(void) { return h(); }
