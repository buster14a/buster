#define H __has_attribute
int a = H(noreturn) + H(
not_an_attribute
);
int b = __has_attribute(
#define Z 1
noreturn) + Z;
#define PR _Pragma
PR("message(\"hi\")") after
#define HDR "other.h"
#include HDR
#define N 100
#line N
int line = __LINE__;
# 50 "renamed.c"
int again = __LINE__;
#if __has_include(HDR) && !__has_include("missing.h")
both
#endif
