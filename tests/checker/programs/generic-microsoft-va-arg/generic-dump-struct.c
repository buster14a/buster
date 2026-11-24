static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }
static const char* base;
static int pr(int tag, const char* f, ...) { __builtin_va_list ap; __builtin_va_start(ap, f); mix((unsigned)tag);
for (const char* p = f; *p; p++) { mix((unsigned char)*p); if (*p != '%') continue; p++;
while (*p == '.' || (*p >= '0' && *p <= '9')) { mix((unsigned char)*p); p++; }
int l = 0, L = 0; while (*p == 'h' || *p == 'l' || *p == 'z' || *p == 'j' || *p == 't' || *p == 'L') { l += *p == 'l' || *p == 'z' || *p == 'j' || *p == 't'; L |= *p == 'L'; mix((unsigned char)*p); p++; }
mix((unsigned char)*p);
if (*p == 's') { const char* s = __builtin_va_arg(ap, const char*); for (int i = 0; s && s[i] && i < 32; i++) mix((unsigned char)s[i]); }
else if (*p == 'p') { const char* q = __builtin_va_arg(ap, const char*); mix(q ? (unsigned long long)(q - base) : 7777); }
else if (*p == 'f' && L) { long double d = __builtin_va_arg(ap, long double); mix((unsigned long long)(long long)(d * 1000)); }
else if (*p == 'f') { double d = __builtin_va_arg(ap, double); mix((unsigned long long)(long long)(d * 1000)); }
else if (l) mix((unsigned long long)__builtin_va_arg(ap, long)); else mix((unsigned long long)__builtin_va_arg(ap, int)); }
__builtin_va_end(ap); return 0; }
typedef unsigned long size_t; typedef signed char i8; typedef struct { int q; short r; } Anon; enum Color { RED, GREEN }; enum Neg { MINUS = -1 };
struct In { int x; long y; };
struct S { _Bool b; char c; i8 sc; unsigned char uc; short s; unsigned short us; int i; unsigned u; long l; unsigned long long ull;
float f; double d; long double ld; const char* str; char* mstr; void* vp; int* np; int arr[2]; size_t z; enum Color col; enum Neg ng;
struct In in; Anon an; int bf : 3; unsigned ubf : 5; int : 4; _Bool bb : 1; long long lbf : 40; enum Color cbf : 2;
struct { int ia; union { int ib; float fc; }; }; struct { int named_in; } named; union U { int ux; char uy; } un; __int128 w; _Complex double cd; };
static int calls; static int next(void) { return calls++; }
int tested(int n) { h = (unsigned)n; calls = 0; struct S s = { 0 }; base = (const char*)&s;
s.b = n & 1; s.c = (char)-n; s.sc = (i8)(n - 100); s.uc = (unsigned char)(200 + n); s.s = (short)(-300 * n); s.us = (unsigned short)(65000 + n);
s.i = -7 * n; s.u = 4000000000u + (unsigned)n; s.l = -8L * n; s.ull = 11ULL << n; s.f = 1.5f * n; s.d = -2.25 * n; s.ld = 3.125L * n;
s.str = "hello"; s.mstr = "a string longer than thirty-two characters for sure"; s.vp = &s.i; s.z = (size_t)n * 3; s.col = GREEN; s.ng = MINUS;
s.in.x = n; s.in.y = -n; s.an.q = 3; s.an.r = (short)n; s.bf = -2; s.ubf = (unsigned)n & 31; s.bb = 1; s.lbf = -12345678901LL * n; s.cbf = GREEN;
s.ia = 13; s.ib = n; s.named.named_in = 15; s.un.ux = 0x41 + n;
__builtin_dump_struct(&s, pr, n);
int (*volatile through)(int, const char*, ...) = pr; __builtin_dump_struct(&s.in, through, next());
Anon a = { n, 6 }; __builtin_dump_struct(&a, pr, next() * 10);
union U u = { n }; __builtin_dump_struct(&u, pr, 3);
__builtin_dump_struct(&s.named, pr, 4); mix((unsigned)calls);
return (int)(h ^ (h >> 32)); }
