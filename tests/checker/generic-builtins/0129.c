static const void* p = __builtin___CFStringMakeConstantString("x");
static long l = (long)__builtin___CFStringMakeConstantString("y");
static const char* q = (const char*)__builtin___CFStringMakeConstantString("w") + 1;
const void* f(void)
{
static const void* s = __builtin___NSStringMakeConstantString("v");
(void)p;
(void)l;
(void)q;
return s;
}
