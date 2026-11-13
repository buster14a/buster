typedef unsigned long size_t;
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
char *strcpy(char *, const char *);
char *strncpy(char *, const char *, size_t);
int abs(int);
long labs(long);
double fabs(double);
float fabsf(float);
void nn(int *) __attribute__((nonnull(1)));
void all(int *, int *) __attribute__((nonnull));
void repeated(int *, int *) __attribute__((nonnull(1)));
void repeated(int *, int *) __attribute__((nonnull(2)));
void normal(int *);
void nonnull_cases(void) {
  nn(0);
  nn((void *)0);
  all(0, 0);
  repeated(0, 0);
  normal(0);
  if (0) nn(0);
  (void)sizeof(nn(0));
}
void nonnull_scope(void) {
  { extern void normal(int *) __attribute__((nonnull(1))); normal(0); }
  normal(0);
}
void memory_sizes(int *p, int *q, char *s) {
  memset(p, 0, sizeof(p));
  memcpy(p, q, sizeof(p));
  memcpy(p, q, sizeof(q));
  memset(p, 0, sizeof(*p));
  memset(s, 0, sizeof(s));
  memset((void *)p, 0, sizeof(p));
  if (0) memset(p, 0, sizeof(p));
  (void)sizeof(memset(p, 0, sizeof(p)));
}
void overflow(void) {
  char b[4];
  char c[8];
  memcpy(b, "123456789", 9);
  strcpy(c, "123456789");
  strncpy(b, "x", 9);
  __builtin_memcpy(b, "123456789", 9);
  __builtin_strcpy(c, "123456789");
  memset(b + 1, 0, 4);
  memcpy(b, "123", 4);
  if (0) memcpy(b, "123456789", 9);
  (void)sizeof(memcpy(b, "123456789", 9));
}
int absolute(double d, long l, unsigned u) {
  int x = abs(d);
  x += abs(l);
  x += abs(u);
  x += fabs(1);
  x += fabsf(d);
  x += __builtin_abs(d);
  return x;
}
int absolute_context(double d) {
  if (0) return abs(d);
  return sizeof(abs(d));
}
