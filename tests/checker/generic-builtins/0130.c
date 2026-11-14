static const char m[] = "x";
const char* g(void);
static const void* p = __builtin___NSStringMakeConstantString(m);
static long l = (long)__builtin___NSStringMakeConstantString(m + 1) + 2;
static const void* c = __builtin___NSStringMakeConstantString(g());
const void* f(const char* s)
{
static const void* t = __builtin___NSStringMakeConstantString(s);
(void)p;
(void)l;
(void)c;
return t;
}
