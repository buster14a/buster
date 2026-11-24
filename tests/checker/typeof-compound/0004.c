int f(void) { __typeof__((int[2]){0}) a = {1}; _Static_assert(sizeof a == 2 * sizeof(int), "array"); return a[0]; }
