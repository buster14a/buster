#define H "oth\
er.h"
#include H
#if __has_include(H) && __has_include("oth\
er.h")
found
#endif
