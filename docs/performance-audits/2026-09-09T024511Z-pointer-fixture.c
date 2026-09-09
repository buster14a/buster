int pointer_join(int *a, int *b, int n) { int *p; if (n) p = b; else p = a; return p[2]; }
int pointer_walk(int *a, int *b, int n) { int *p = a; int sum = 0; while (n > 0) { sum += *p; p += 1; n -= 1; } return sum; }
int pointer_backwards(int *a, int *b, int n) { int *p = a + n; int sum = 0; while (p != a) { p -= 1; sum += p[0]; } return sum; }
int pointer_swap(int *a, int *b, int n) { int sum = 0; while (n > 0) { int *tmp = a; a = b; b = tmp; sum += a[0]; n -= 1; } return sum; }
int pointer_length(int *a, int *b, int n) { int *p = a; while (*p) p += 1; return (int)(p - a); }
