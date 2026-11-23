// Pops the two macros the predefines push, pushes predefined ones and leaves them pushed, and pops one of its own.
#include "probe.h"
#pragma pop_macro("__GCC_DESTRUCTIVE_SIZE")
#pragma pop_macro("__GCC_CONSTRUCTIVE_SIZE")
#pragma pop_macro("__GCC_CONSTRUCTIVE_SIZE")
#pragma push_macro("__linux__")
#undef __linux__
#pragma push_macro("__STDC_HOSTED__")
#pragma push_macro("PUSHES_OWN")
#define PUSHES_OWN 3
int pushes_own = PUSHES_OWN;
#pragma pop_macro("PUSHES_OWN")
#ifdef PUSHES_OWN
int pushes_left;
#endif
#pragma push_macro("PUSHES_LEFT")
#define PUSHES_LEFT 4
int pushes_value = PUSHES_LEFT;
