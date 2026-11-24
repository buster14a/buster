typedef typeof(nullptr) np;
int g(np n) {
  int a = *(int *)n; int b = *(int *)(nullptr); int c = *(volatile int *)nullptr; int d = *(int *)(void *)nullptr;
  *(int *)nullptr = 1;
  return a + b + c + d;
}
