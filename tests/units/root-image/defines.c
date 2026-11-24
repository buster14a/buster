// Leaves macros of its own: an object-like one, a function-like one that is compiled and expanded, and a redefinition
// of a predefined one.
#include "probe.h"
#define ROOT_IMAGE_LEFT_OBJECT 7
#define ROOT_IMAGE_LEFT_FUNCTION(x) ((x) + ROOT_IMAGE_LEFT_OBJECT)
#define DEFINES_TWICE(x, y) ROOT_IMAGE_LEFT_FUNCTION(x) * ROOT_IMAGE_LEFT_FUNCTION(y)
int defines_value = DEFINES_TWICE(2, DEFINES_TWICE(3, 4));
#undef __STDC_HOSTED__
#define __STDC_HOSTED__ 42
int defines_hosted = __STDC_HOSTED__;
#define __x86_64__ 2
int defines_x86_64 = __x86_64__;
