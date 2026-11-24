int f(void) { typeof((int[2]){0}) a = {1}; _Static_assert(sizeof a == 2 * sizeof(int), "C23 array"); return a[0]; }
