typedef __SIZE_TYPE__ size_t;
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
char *strcpy(char *, const char *);
char *strncpy(char *, const char *, size_t);
int snprintf(char *, size_t, const char *, ...);
int scanf(const char *, ...);

void param(int *p __attribute__((nonnull)));
void unnamed(int * __attribute__((nonnull)));
void redecl(int *p __attribute__((nonnull)));
void redecl(int *);
void typed(int * _Nonnull p);
void pointers(void (*f)(int *) __attribute__((nonnull(1)))) {
  param(0);
  unnamed(0);
  redecl(0);
  typed(0);
  f(0);
}
void builtin_contracts(void) {
  (void)memcpy((void *)0, (void *)0, 0);
  (void)__builtin_memcpy((void *)0, (void *)0, 0);
  (void)strcpy((void *)0, (void *)0);
  (void)__builtin_strcpy((void *)0, (void *)0);
}
void bounds(void) {
  char b[4];
  struct Pair { char first[2]; char second[4]; } object;
  char *p = b;
  static const char text[] = "123456";
  memcpy(&b[1], "012345", 4);
  memcpy(&b, "012345", 5);
  memcpy(object.first, "012345", 5);
  memcpy(object.first + 1, "012345", 6);
  memcpy(p, "012345", 5);
  strcpy(b, text);
  __builtin___strcpy_chk(b, "123456", 4);
  snprintf(b, 9, "%d", 1);
  scanf("%8s", b);
}
struct Item { long value; };
void sizes(struct Item *p, struct Item *q, int *a, int *b) {
  memset(p, 0, sizeof(struct Item *));
  memcpy(p, q, sizeof(struct Item *));
  memset(a, 0, sizeof(int *));
  memset(&a, 0, sizeof(&a));
  memcpy(a + 1, b, sizeof(a + 1));
  memcpy(a, b, sizeof(a + 0));
  (void)sizeof(memset(p, 0, sizeof(struct Item *)));
}
int abs(int);
long labs(long);
long long llabs(long long);
double fabs(double);
float fabsf(float);
long double fabsl(long double);
double cabs(double _Complex);
float cabsf(float _Complex);
long double cabsl(long double _Complex);
long long abs_width(__int128 x, long long y, _Bool b) {
  return abs(x) + labs(y) + abs(b);
}
double abs_complex(double _Complex z, float f) {
  return fabs(z) + abs(z) + cabs(f) + cabsf(z);
}
double with_replacement_shadow(double d) {
  int fabs(int);
  return abs(d);
}
void direct_wrappers(void) {
  param(0);
  (param)(0);
  (&param)(0);
  (*param)(0);
}
void constant_context(void) {
  int a[(param(0), 1)];
  int b[(__builtin_memcpy(a, "123456789", 100), 1)];
  (void)a;
  (void)b;
}
