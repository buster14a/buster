typedef int macos;
int s = sizeof(__builtin_available(*));
typeof(__builtin_available(ios 10, *)) t;
_Static_assert(_Generic(__builtin_available(*), _Bool: 1, default: 0), "");
int f(int x)
{
if (__builtin_available(macos 10.12, iOS 10_3, watchOS 3.1.2, *)) x += 1;
if (!__builtin_available(*)) x += 2;
_Bool b = __builtin_available(android 21, *);
int a[__builtin_available(*) + 1];
return x + b + (int)sizeof a + __builtin_available(anyAppleOS 26.1, *);
}
