// Takes predefined macros away, and one of its own.
#include "probe.h"
#undef __linux__
#undef __x86_64__
#undef __STDC_VERSION__
#undef __GCC_DESTRUCTIVE_SIZE
#define UNDEFINES_OWN 1
#undef UNDEFINES_OWN
#ifdef __linux__
int undefines_linux;
#endif
int undefines_done;
