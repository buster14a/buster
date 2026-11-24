struct S { int a; } s;
void g(void) { (void)__builtin_strlen(1); }
void h(void) { (void)__builtin_strlen(L"a"); }
void k(void) { (void)__builtin___CFStringMakeConstantString(1); }
void m(char *p, int *ip) { (void)__builtin_strlen(s); (void)__builtin_memcpy(p, 1, 2); (void)__builtin___CFStringMakeConstantString(ip); }
