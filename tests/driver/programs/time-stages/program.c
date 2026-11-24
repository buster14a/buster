#include <stdio.h>
static int table[4] = { 1, 2, 3, 4 };
static int sum(int n) { int s = 0; for (int i = 0; i < n; i++) s += table[i & 3]; return s; }
int main(void) { printf("%d\n", sum(10)); return sum(4) == 10 ? 0 : 1; }
