static const char s[] = "x";
const void* d = __builtin___CFStringMakeConstantString(s);
void f(int x, const char *p) { (void)__builtin___CFStringMakeConstantString(p); (void)__builtin___CFStringMakeConstantString((const char*)"a"); }
void g(void) { (void)__builtin___CFStringMakeConstantString(("a")); (void)__builtin___CFStringMakeConstantString(u8"a"); }
