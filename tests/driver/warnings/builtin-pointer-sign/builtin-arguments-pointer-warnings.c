int g(unsigned char *p) { return __builtin_strlen(p) + __builtin_strcmp(p, "x") + __builtin_strncmp("y", p, 1); }
void h(unsigned char *d, const char *s, const unsigned char *cu) { __builtin_strcpy(d, s); __builtin_strcpy((char *)cu, cu); __builtin_strcat(d, cu); }
void k(const char *c) { __builtin_strcpy(c, "x"); __builtin_memset(c, 0, 1); }
#define S(x) __builtin_strlen(x)
unsigned long m(signed char *sp) { return S(sp); }
