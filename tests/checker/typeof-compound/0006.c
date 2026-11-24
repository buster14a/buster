int f(void) { __typeof__((int[]){[3]=1}) a = {1}; _Static_assert(sizeof a == 4 * sizeof(int), "designated bound"); return a[0]; }
