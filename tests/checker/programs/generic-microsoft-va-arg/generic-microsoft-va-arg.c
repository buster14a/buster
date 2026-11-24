void *memcpy(void *, const void *, unsigned long); static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }
struct S3 { char c[3]; }; struct S4 { short a, b; }; struct S8 { int a, b; }; struct S16 { long a, b; };
static unsigned long long slots[16]; static struct S3 s3 = { { 1, 2, 3 } }; static struct S16 s16 = { 11, 12 };
static long double ld = 2.5L; static _Complex double cd = 1.0 + 2.0i; static __int128 wide = ((__int128)7 << 64) | 9;
static double take(char** list) { return __builtin_va_arg(*list, double); }
static void copy_end(__builtin_ms_va_list* to, __builtin_ms_va_list from) { __builtin_ms_va_copy(*to, from); __builtin_ms_va_end(from); }
int tested(int n) { h = (unsigned)n; struct S4 s4 = { 5, 6 }; struct S8 s8 = { 7, 8 }; float f = 1.5f; double d = 3.25;
unsigned long long k = 0; slots[k++] = (unsigned)n; memcpy(&slots[k++], &f, sizeof f); memcpy(&slots[k++], &d, sizeof d);
slots[k++] = (unsigned long long)&s3; memcpy(&slots[k++], &s4, sizeof s4); memcpy(&slots[k++], &s8, sizeof s8);
slots[k++] = (unsigned long long)&s16; slots[k++] = (unsigned long long)&ld; slots[k++] = (unsigned long long)&cd;
slots[k++] = (unsigned long long)&wide; slots[k++] = (unsigned long long)(long long)-3; memcpy(&slots[k++], &d, sizeof d);
slots[k++] = 0x1234; char* ap = (char*)slots; __builtin_ms_va_list copy; mix((unsigned)__builtin_va_arg(ap, int));
mix((unsigned long long)(__builtin_va_arg(ap, float) * 4)); mix((unsigned long long)(__builtin_va_arg(ap, double) * 4));
struct S3 r3 = __builtin_va_arg(ap, struct S3); mix(r3.c[0] + r3.c[1] * 10 + r3.c[2] * 100); struct S4 r4 = __builtin_va_arg(ap, struct S4);
mix(r4.a * 10 + r4.b); struct S8 r8 = __builtin_va_arg(ap, struct S8); mix(r8.a * 10 + r8.b);
struct S16 r16 = __builtin_va_arg(ap, struct S16); mix(r16.a * 100 + r16.b);
mix((unsigned long long)(__builtin_va_arg(ap, long double) * 8)); _Complex double rc = __builtin_va_arg(ap, _Complex double);
mix((unsigned long long)(__real__ rc * 10 + __imag__ rc)); __int128 rw = __builtin_va_arg(ap, __int128);
mix((unsigned long long)(rw >> 64) * 100 + (unsigned long long)rw); mix((unsigned long long)__builtin_va_arg(ap, long long));
copy_end(&copy, ap); mix((unsigned long long)(take(&copy) * 4)); mix((unsigned)__builtin_va_arg(copy, short));
mix((unsigned long long)(copy - (char*)slots)); return (int)(h ^ (h >> 32)); }
