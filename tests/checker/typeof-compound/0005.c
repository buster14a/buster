int f(void) { __typeof__((int[]){1,2,3}) a = {1}; _Static_assert(sizeof a == 3 * sizeof(int), "bound"); return a[0]; }
