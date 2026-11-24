struct S { int a; };
int g(int);
int f(int n) {
  *(int *)0 = 1; *(int *)0 += 2; int x = *(int *)0; g(*(int *)0); int k = n ? *(int *)0 : 1;
  int c = *(const int *)(0); int l = *(int *)(1 - 1); int t = *(char *)(void *)0L;
  int m = (*(struct S *)0).a; int *q = &*(int *)0; (*(int *)0)++; *(volatile int *)0 = 3;
  int *p = 0; *p = 4; int y = ((int *)0)[0]; int s = sizeof(*(int *)0); ((struct S *)0)->a = 2;
  if (0) { int z = *(int *)0; (void)z; }
  (void)*(int *)0;
  return x + k + c + l + t + m + y + s + (q != 0);
  *(int *)0 = 9;
}
