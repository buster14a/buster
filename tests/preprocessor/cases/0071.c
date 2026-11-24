a __COUNTER__ __COUNTER__ __INCLUDE_LEVEL__ __has_attribute(noreturn) __has_builtin(__builtin_expect) __has_feature(c_static_assert)
#define X noreturn
#if __has_attribute(X) && __has_include("other.h") && !__has_include("missing.h") && defined X && !defined(Y)
b
#endif
#define G(x) __LINE__ x
int a = G(
1
);
