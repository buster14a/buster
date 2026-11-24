static const void* never_called(void) { return __builtin___CFStringMakeConstantString("only"); }
static const void* never_read = __builtin___CFStringMakeConstantString("initial");
static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }
typedef struct { const int* isa; int flags; const char* str; long length; } CF;
static const void* s1 = __builtin___CFStringMakeConstantString("hello");
static const void* s2 = __builtin___NSStringMakeConstantString("hello");
static long as_long = (long)__builtin___CFStringMakeConstantString("x");
static const char* shifted = (const char*)__builtin___CFStringMakeConstantString("y") + 8;
static const void* other(void) { return __builtin___CFStringMakeConstantString("hello"); }
const int* class_reference(void);
static void bytes(const CF* c)
{
    mix(c->isa == class_reference()); mix((unsigned)c->flags); mix((unsigned long)c->length);
    if (c->flags == 0x7d0) { const unsigned short* u = (const unsigned short*)c->str; for (long i = 0; i <= c->length; i++) mix(u[i]); }
    else { for (long i = 0; i <= c->length; i++) mix((unsigned char)c->str[i]); }
}
int tested(int n)
{
    h = (unsigned)n;
    const CF* a = (const CF*)__builtin___CFStringMakeConstantString("hello");
    mix(a == s1); mix(a == s2); mix(a == other()); mix((long)a == as_long);
    mix((const CF*)as_long == (const CF*)__builtin___CFStringMakeConstantString("x"));
    mix(shifted == (const char*)__builtin___CFStringMakeConstantString("y") + 8);
    bytes(a); bytes((const CF*)__builtin___CFStringMakeConstantString(""));
    bytes((const CF*)__builtin___CFStringMakeConstantString("h\xc3\xa9llo \xf0\x9f\x98\x80"));
    bytes((const CF*)__builtin___CFStringMakeConstantString("a\0b"));
    bytes((const CF*)__builtin___CFStringMakeConstantString("cut \xc3\x28 here"));
    bytes((const CF*)__builtin___CFStringMakeConstantString(("pa" "ren")));
    bytes((const CF*)__builtin___CFStringMakeConstantString((const char*)"cast"));
    bytes((const CF*)__builtin___NSStringMakeConstantString("ns"));
    bytes((const CF*)__builtin___CFStringMakeConstantString("only"));
    bytes((const CF*)__builtin___CFStringMakeConstantString("initial"));
    mix(__builtin___NSStringMakeConstantString("ns") == __builtin___CFStringMakeConstantString("ns"));
    mix(__builtin___CFStringMakeConstantString("a\0b") != __builtin___CFStringMakeConstantString("a"));
    mix(__builtin___CFStringMakeConstantString("abc")->length * 10 + sizeof(*__builtin___CFStringMakeConstantString("q")));
    return (int)(h ^ (h >> 32));
}
